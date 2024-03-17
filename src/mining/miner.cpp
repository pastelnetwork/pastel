// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <tuple>
#ifdef ENABLE_MINING
#include <functional>
#endif

#include <sodium.h>
#include <utils/scope_guard.hpp>

#include <mining/miner.h>
#include <mining/mining-settings.h>
#include <mining/eligibility-mgr.h>
#ifdef ENABLE_MINING
#include <pow/tromp/equi_miner.h>
#endif

#include <utils/util.h>
#include <utils/str_utils.h>
#include <amount.h>
#include <chainparams.h>
#include <chain_options.h>
#include <consensus/consensus.h>
#include <consensus/upgrades.h>
#include <consensus/validation.h>
#ifdef ENABLE_MINING
#include <crypto/equihash.h>
#endif
#include <hash.h>
#include <key_io.h>
#include <accept_to_mempool.h>
#include <metrics.h>
#include <net.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <random.h>
#include <timedata.h>
#include <ui_interface.h>
#include <utilmoneystr.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif
#include <netmsg/nodemanager.h>
#include <mnode/mnode-validation.h>
#include <mnode/mnode-controller.h>

using namespace std;

#ifdef ENABLE_MINING
atomic_bool gl_bEligibleForMiningNextBlock(false);
#endif // ENABLE_MINING

//////////////////////////////////////////////////////////////////////////////
//
// PastelMiner
//
//////////////////////////////////////////////////////////////////////////////
//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.
// The COrphan class keeps track of these 'temporary orphans' while
// CreateBlock is figuring out which transactions to include.
//
class COrphan
{
public:
    const CTransaction* ptx;
    set<uint256> setDependsOn;
    CFeeRate feeRate;
    double dPriority;

    COrphan(const CTransaction* ptxIn) noexcept: 
        ptx(ptxIn),
        feeRate(0),
        dPriority(0)
    {}
};

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

// We want to sort transactions by priority and fee rate, so:
typedef tuple<double, CFeeRate, const CTransaction*> TxPriority;
class TxPriorityCompare
{
    bool byFee;

public:
    TxPriorityCompare(bool _byFee) noexcept: 
        byFee(_byFee)
    {}

    bool operator()(const TxPriority& a, const TxPriority& b) noexcept
    {
        if (byFee)
        {
            if (get<1>(a) == get<1>(b))
                return get<0>(a) < get<0>(b);
            return get<1>(a) < get<1>(b);
        }
        else
        {
            if (get<0>(a) == get<0>(b))
                return get<1>(a) < get<1>(b);
            return get<0>(a) < get<0>(b);
        }
    }
};

void UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    pblock->nTime = static_cast<uint32_t>(max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime()));

    // Updating time can change work required on testnet:
    if (consensusParams.nPowAllowMinDifficultyBlocksAfterHeight.has_value())
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
}

CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn, const bool v5Block,
    const string& sEligiblePastelID)
{
    // Create new block
    auto pblocktemplate = make_unique<CBlockTemplate>();
    if (!pblocktemplate)
        return nullptr;
    auto pblock = &pblocktemplate->block; // pointer for convenience

    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gl_MiningSettings.getBlockVersion();
    else
        pblock->nVersion = v5Block ? CBlockHeader::VERSION_SIGNED_BLOCK : 4;

    // Add dummy coinbase tx as first transaction
    pblock->vtx.push_back(CTransaction());
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    // Collect memory pool transactions into the block
    CAmount nFees = 0;
    const uint32_t nBlockPrioritySize = gl_MiningSettings.getBlockPrioritySize();

    {
        LOCK2(cs_main, mempool.cs);
        CBlockIndex* pindexPrev = chainActive.Tip();
        const int nHeight = pindexPrev->nHeight + 1;
        uint32_t consensusBranchId = CurrentEpochBranchId(nHeight, chainparams.GetConsensus());
        pblock->nTime = static_cast<uint32_t>(GetAdjustedTime());
        const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();
        CCoinsViewCache view(gl_pCoinsTip.get());

        SaplingMerkleTree sapling_tree;
        assert(view.GetSaplingAnchorAt(view.GetBestAnchor(SAPLING), sapling_tree));

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        unordered_map<uint256, vector<COrphan*> > mapDependers;
        bool fPrintPriority = GetBoolArg("-printpriority", false);

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        for (auto mi = mempool.mapTx.cbegin(); mi != mempool.mapTx.cend(); ++mi)
        {
            const CTransaction& tx = mi->GetTx();

            int64_t nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                                    ? nMedianTimePast
                                    : pblock->GetBlockTime();

            if (tx.IsCoinBase())
				continue;
            if (!IsFinalTx(tx, nHeight, nLockTimeCutoff) || IsExpiredTx(tx, nHeight))
                continue;

            COrphan* porphan = nullptr;
            double dPriority = 0;
            CAmount nTotalIn = 0;
            bool fMissingInputs = false;
            for (const auto& txin : tx.vin)
            {
                // Read prev transaction
                if (!view.HaveCoins(txin.prevout.hash))
                {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    if (!mempool.mapTx.count(txin.prevout.hash))
                    {
                        LogPrintf("ERROR: mempool transaction missing input\n");
                        if (fDebug)
                            assert("mempool transaction missing input" == 0);
                        fMissingInputs = true;
                        if (porphan)
                            vOrphan.pop_back();
                        break;
                    }

                    // Has to wait for dependencies
                    if (!porphan)
                    {
                        // Use list for automatic deletion
                        vOrphan.push_back(COrphan(&tx));
                        porphan = &vOrphan.back();
                    }
                    mapDependers[txin.prevout.hash].push_back(porphan);
                    porphan->setDependsOn.insert(txin.prevout.hash);
                    nTotalIn += mempool.mapTx.find(txin.prevout.hash)->GetTx().vout[txin.prevout.n].nValue;
                    continue;
                }
                const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                assert(coins);

                CAmount nValueIn = coins->vout[txin.prevout.n].nValue;
                nTotalIn += nValueIn;

                int nConf = nHeight - coins->nHeight;

                dPriority += (double)nValueIn * nConf;
            }
            nTotalIn += tx.GetShieldedValueIn();

            if (fMissingInputs)
                continue;

            // Priority is sum(valuein * age) / modified_txsize
            const size_t nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            dPriority = tx.ComputePriority(dPriority, nTxSize);

            uint256 hash = tx.GetHash();
            mempool.ApplyDeltas(hash, dPriority, nTotalIn);

            CFeeRate feeRate(nTotalIn-tx.GetValueOut(), nTxSize);

            if (porphan)
            {
                porphan->dPriority = dPriority;
                porphan->feeRate = feeRate;
            }
            else
                vecPriority.emplace_back(dPriority, feeRate, &(mi->GetTx()));
        }

        // Collect transactions into block
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        const auto &consensusParams = chainparams.GetConsensus();
        while (!vecPriority.empty())
        {
            // Take highest priority transaction off the priority queue:
            double dPriority = get<0>(vecPriority.front());
            CFeeRate feeRate = get<1>(vecPriority.front());
            const CTransaction& tx = *(get<2>(vecPriority.front()));

            pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            const size_t nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= gl_MiningSettings.getBlockMaxSize())
                continue;

            // Legacy limits on sigOps:
            unsigned int nTxSigOps = GetLegacySigOpCount(tx);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            // Skip free transactions if we're past the minimum block size:
            const uint256& hash = tx.GetHash();
            double dPriorityDelta = 0;
            CAmount nFeeDelta = 0;
            mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
            if (fSortedByFee && (dPriorityDelta <= 0) && (nFeeDelta <= 0) && 
                (feeRate < gl_ChainOptions.minRelayTxFee) && (nBlockSize + nTxSize >= gl_MiningSettings.getBlockMinSize()))
                continue;

            // Prioritise by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority)))
            {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            if (!view.HaveInputs(tx))
                continue;

            CAmount nTxFees = view.GetValueIn(tx)-tx.GetValueOut();

            nTxSigOps += GetP2SHSigOpCount(tx, view);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            // Note that flags: we don't want to set mempool/IsStandard()
            // policy here, but we still have to ensure that the block we
            // create only contains transactions that are valid in new blocks.
            PrecomputedTransactionData txdata(tx);

            CValidationState state(TxOrigin::MINED_BLOCK);
            if (!ContextualCheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, txdata, consensusParams, consensusBranchId))
                continue;

            UpdateCoins(tx, view, nHeight);

            for (const auto &outDescription : tx.vShieldedOutput)
                sapling_tree.append(outDescription.cm);

            // Added
            pblock->vtx.push_back(tx);
            pblocktemplate->vTxFees.push_back(nTxFees);
            pblocktemplate->vTxSigOps.push_back(nTxSigOps);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fPrintPriority)
            {
                LogPrintf("priority %.1f fee %s txid %s\n",
                    dPriority, feeRate.ToString(), tx.GetHash().ToString());
            }

            // Add transactions that depend on this one to the priority queue
            if (mapDependers.count(hash))
            {
                for (auto porphan : mapDependers[hash])
                {
                    if (porphan->setDependsOn.empty())
                        continue;
                    porphan->setDependsOn.erase(hash);
                    if (porphan->setDependsOn.empty())
                    {
                        vecPriority.emplace_back(porphan->dPriority, porphan->feeRate, porphan->ptx);
                        push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                    }
                }
            }
        }

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LogFnPrintf("total size %" PRIu64, nBlockSize);

        // Create coinbase tx
        CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, nHeight);
        txNew.vin.resize(1);
        txNew.vin[0].prevout.SetNull();
        txNew.vout.resize(1);
        txNew.vout[0].scriptPubKey = scriptPubKeyIn;

        CAmount blockReward = nFees + GetBlockSubsidy(nHeight, consensusParams);        
        txNew.vout[0].nValue = blockReward;

        FillOtherBlockPayments(txNew, nHeight, blockReward, pblock->txoutMasternode, pblock->txoutGovernance);

        txNew.vin[0].scriptSig = CScript() << nHeight << OP_0;
        
        pblock->vtx[0] = txNew;
        pblocktemplate->vTxFees[0] = -nFees;

        // Randomise nonce
        arith_uint256 nonce = UintToArith256(GetRandHash());
        // Clear the top and bottom 16 bits (for local use as thread flags and counters)
        nonce <<= 32;
        nonce >>= 16;
        pblock->nNonce = ArithToUint256(nonce);

        bool bTxHasMnOutputs = false;
        if (masterNodeCtrl.IsSynced())
            bTxHasMnOutputs = masterNodeCtrl.masternodeManager.IsTxHasMNOutputs(txNew);

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        pblock->hashFinalSaplingRoot   = sapling_tree.root();
        UpdateTime(pblock, consensusParams, pindexPrev);
        pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
        pblock->nSolution.clear();
        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        if (bTxHasMnOutputs && !sEligiblePastelID.empty())
        {
            SecureString sPassPhrase;
            if (!gl_MiningSettings.getGenInfo(sPassPhrase))
            {
				LogPrintf("ERROR: PastelMiner: failed to get passphrase for PastelID '%s'\n", sEligiblePastelID);
				throw runtime_error(strprintf("PastelMiner: failed to access secure container for Pastel ID '%s'",
                    sEligiblePastelID));
			}
            string sPrevMerkleRoot(pindexPrev->hashMerkleRoot.cbegin(), pindexPrev->hashMerkleRoot.cend());
            string sPrevMerkelRootSignature = CPastelID::Sign(sPrevMerkleRoot, sEligiblePastelID, move(sPassPhrase));
            pblock->sPastelID = sEligiblePastelID;
            pblock->prevMerkleRootSignature = string_to_vector(sPrevMerkelRootSignature);
        }
        CValidationState state(TxOrigin::MINED_BLOCK);
        if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false))
        {
            string sStateMsg = strprintf("(code: %d", state.GetRejectCode());
            if (state.GetRejectReason().empty())
                sStateMsg += ")";
            else
                sStateMsg += ", reason: " + state.GetRejectReason() + ")";
			LogFnPrintf("WARNING: TestBlockValidity failed %s", sStateMsg);
        }
    }

    return pblocktemplate.release();
}

#ifdef ENABLE_WALLET
optional<CScript> GetMinerScriptPubKey(CReserveKey& reservekey, const CChainParams &chainparams)
#else
optional<CScript> GetMinerScriptPubKey(const CChainParams& chainparams)
#endif
{
    KeyIO keyIO(chainparams);
    CKeyID keyID;
    CTxDestination addr = keyIO.DecodeDestination(GetArg("-mineraddress", ""));
    if (IsValidDestination(addr)) {
        keyID = get<CKeyID>(addr);
    } else {
#ifdef ENABLE_WALLET
        CPubKey pubkey;
        if (!reservekey.GetReservedKey(pubkey)) {
            return optional<CScript>();
        }
        keyID = pubkey.GetID();
#else
        return optional<CScript>();
#endif
    }

    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
    return scriptPubKey;
}

#ifdef ENABLE_WALLET
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, const CChainParams& chainparams, const bool bV5Block, const string& sEligiblePastelID)
{
    optional<CScript> scriptPubKey = GetMinerScriptPubKey(reservekey, chainparams);
#else
CBlockTemplate* CreateNewBlockWithKey(const CChainParams& chainparams, const bool bV5Block, const string& sEligiblePastelID)
{
    optional<CScript> scriptPubKey = GetMinerScriptPubKey(chainparams);
#endif

    if (!scriptPubKey)
        return nullptr;
    return CreateNewBlock(chainparams, *scriptPubKey, bV5Block, sEligiblePastelID);
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//

#ifdef ENABLE_MINING

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = pblock->BuildMerkleTree();
}

#ifdef ENABLE_WALLET
static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams, CWallet& wallet, CReserveKey& reservekey)
#else
static bool ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams)
#endif // ENABLE_WALLET
{
    LogFnPrintf("PastelMiner new block [%s], generated %s", pblock->ToString(), FormatMoney(pblock->vtx[0].vout[0].nValue));

    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
            return error("PastelMiner: generated block is stale");
    }

#ifdef ENABLE_WALLET
    if (GetArg("-mineraddress", "").empty())
    {
        // Remove key from key pool
        reservekey.KeepKey();
    }

    const uint256 hashBlock = pblock->GetHashCurrent();
    // Track how many getdata requests this block gets
    {
        LOCK(wallet.cs_wallet);
        wallet.mapRequestCount[hashBlock] = 0;
    }
#endif

    // Process this block the same as if we had received it from another node
    CValidationState state(TxOrigin::MINED_BLOCK);
    if (!ProcessNewBlock(state, chainparams, nullptr, pblock, true, nullptr))
        return error("PastelMiner: ProcessNewBlock, block %s not accepted", hashBlock.ToString());

    TrackMinedBlock(hashBlock);

    return true;
}

#ifdef ENABLE_WALLET
void static PastelMiner(const int nThreadNo, CWallet *pwallet)
#else
void static PastelMiner(const int nThreadNo)
#endif
{
    LogPrintf("PastelMiner thread #%d started\n", nThreadNo);
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread(strprintf("psl-miner-%d", nThreadNo).c_str());

#ifdef ENABLE_WALLET
    // Each thread has its own key
    CReserveKey reservekey(pwallet);
#endif

    // Each thread has its own counter
    unsigned int nExtraNonce = 0;

    const auto& chainparams = Params();
    const auto& consensusParams = chainparams.GetConsensus();
    unsigned int n = consensusParams.nEquihashN;
    unsigned int k = consensusParams.nEquihashK;
    const double nMiningEligibilityThreshold = consensusParams.nMiningEligibilityThreshold;

    LogPrint("pow", "Using Equihash solver \"%s\" with n = %u, k = %u\n", 
        gl_MiningSettings.getEquihashSolverName(), n, k);

    mutex m_cs;
    bool cancelSolver = false;
    boost::signals2::connection c = uiInterface.NotifyBlockTip.connect(
        [&m_cs, &cancelSolver](const uint256& hashNewTip) mutable {
            lock_guard<mutex> lock{m_cs};
            cancelSolver = true;
        }
    );
    miningTimer.start();
    // add scope guard to disconnect the signal when the thread exits
    auto guardC = sg::make_scope_guard([&]() noexcept
    {
        c.disconnect();
        miningTimer.stop();
    });

    const auto fnWaitFor = [&](const int nSeconds) noexcept
    {
        for (int i = 0; i < nSeconds; ++i)
		{
			MilliSleep(1000);
			func_thread_interrupt_point();
		}
		return true;
	};
    try {
        while (true)
        {
            // check if we can use new mining 
            const size_t nNewMiningAllowedHeight = consensusParams.GetNetworkUpgradeActivationHeight(Consensus::UpgradeIndex::UPGRADE_VERMEER);
            const bool bNewMiningAllowed = chainparams.IsTestNet() || (nNewMiningAllowedHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) || 
                (gl_nChainHeight + 1 >= nNewMiningAllowedHeight + consensusParams.nNewMiningAlgorithmHeightDelay);
            const bool bV5Block = chainparams.IsTestNet() || (nNewMiningAllowedHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) || 
				(gl_nChainHeight + 1 >= nNewMiningAllowedHeight);

            gl_bEligibleForMiningNextBlock = !bNewMiningAllowed;
            if (bNewMiningAllowed && !masterNodeCtrl.IsMasterNode())
            {
                LogFnPrintf("Node is not running in MasterNode mode, exiting CPU miner thread...");
                break;
            }
            if (chainparams.MiningRequiresPeers())
            {
                // Busy-wait for the network to come online so we don't waste time mining
                // on an obsolete chain. In regtest mode we expect to fly solo.
                miningTimer.stop();
                LogFnPrint("mining", "Waiting for network to come online...");
                do {
                    const bool fvNodesEmpty = gl_NodeManager.GetNodeCount() == 0;
                    if (!fvNodesEmpty && !fnIsInitialBlockDownload(consensusParams))
                        break;
                    fnWaitFor(1);
                } while (true);
                LogFnPrint("mining", "Network is online");
                miningTimer.start();
            }
              
            //INGEST->!!!
            if (!chainparams.IsRegTest())
            {
                if (gl_nChainHeight < TOP_INGEST_BLOCK) {
                    n = 48;
                    k = 5;
                } else {
                    n = consensusParams.nEquihashN;
                    k = consensusParams.nEquihashK;
                }
            }
            //<-INGEST!!!


            // Check if MasterNode is eligible to mine next block - perform only after the masternodes are synced
            opt_string_t sEligiblePastelID;
            bool bInvalidMiningSettings = false;
            if (bNewMiningAllowed)
            {
                miningTimer.stop();
                LogFnPrint("mining", "Waiting for MasterNode sync...");
                do {
                    if (masterNodeCtrl.IsSynced())
                    {
                        LogFnPrint("mining", "MasterNode is synced");
						break;
                    }
                    fnWaitFor(5);
                } while (true);
                LogFnPrint("mining", "Waiting for active MasterNode ENABLED state...");
                while (!masterNodeCtrl.IsActiveMasterNode())
					fnWaitFor(5);
                LogFnPrint("mining", "MasterNode has been STARTED");
                do {
                    masternode_info_t mnInfo;
                    const bool bHaveMnInfo = masterNodeCtrl.masternodeManager.GetMasternodeInfo(true, masterNodeCtrl.activeMasternode.outpoint, mnInfo);
                    if (bHaveMnInfo && mnInfo.IsEnabled())
                    {
                        LogFnPrint("mining", "MasterNode is ENABLED");
                        break;
                    }
                    fnWaitFor(5);
                } while (true);
                string error;
                if (!gl_MiningSettings.CheckMNSettingsForLocalMining(error))
                {
					LogFnPrint("MasterNode settings are not valid for local mining. %s", error);
                    bInvalidMiningSettings = true;
					break;
				}
                LogFnPrint("mining", "Waiting for MasterNode mining eligibility...");
                do {
                    const auto sGenId = gl_MiningSettings.getGenId();
                    if (sGenId.empty() && chainparams.IsRegTest())
                        break;
                    {
						LOCK(cs_main);
                        if (gl_pMiningEligibilityManager && 
                            gl_pMiningEligibilityManager->IsCurrentMnEligibleForBlockReward(chainActive.Tip(), GetTime()))
						    sEligiblePastelID = gl_MiningSettings.getGenId();
                        else
                            sEligiblePastelID= nullopt;
					}   
                    if (sEligiblePastelID.has_value())
                        break;
                    fnWaitFor(5);
                } while (true);
                LogFnPrint("mining", "MasterNode with mnid='%s' is eligible for mining new block", sEligiblePastelID.value_or("not defined"));

                gl_bEligibleForMiningNextBlock = true;
                miningTimer.start();
            }
            
            if (bInvalidMiningSettings)
            {
                LogPrintf("Error in PastelMiner: Invalid MasterNode settings for local mining\n");
                return;
            }
            //
            // Create new block
            //
            unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            auto pindexPrev = chainActive.Tip();

#ifdef ENABLE_WALLET
            unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(reservekey, chainparams, bV5Block, sEligiblePastelID.value_or("")));
#else
            unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(chainparams, bV5Block, sEligiblePastelID.value_or("")));
#endif
            if (!pblocktemplate.get())
            {
                if (GetArg("-mineraddress", "").empty()) {
                    LogPrintf("Error in PastelMiner: Keypool ran out, please call keypoolrefill before restarting the mining thread\n");
                } else {
                    // Should never reach here, because -mineraddress validity is checked in init.cpp
                    LogPrintf("Error in PastelMiner: Invalid -mineraddress\n");
                }
                return;
            }
            CBlock *pblock = &pblocktemplate->block;
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

            LogPrintf("Running PastelMiner with %zu transactions in block (%zu bytes)\n", pblock->vtx.size(),
                ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

            //
            // Search
            //
            int64_t nStart = GetTime();
            arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

            while (true)
            {
                // Hash state
                crypto_generichash_blake2b_state state;
                EhInitialiseState(n, k, state);

                // I = the block header minus nonce and solution.
                CEquihashInput I{*pblock};
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss.reserve(I.GetReserveSize());
                ss << I;

                // H(I||...
                crypto_generichash_blake2b_update(&state, (unsigned char*)&ss[0], ss.size());

                // H(I||V||...
                crypto_generichash_blake2b_state curr_state;
                curr_state = state;
                crypto_generichash_blake2b_update(&curr_state,
                                                  pblock->nNonce.begin(),
                                                  pblock->nNonce.size());

                // (x_1, x_2, ...) = A(I, V, n, k)
                LogPrint("pow", "Running Equihash solver \"%s\" with nNonce = %s\n",
                         gl_MiningSettings.getEquihashSolverName(), pblock->nNonce.ToString());

                function<bool(v_uint8)> validBlock =
#ifdef ENABLE_WALLET
                        [&pblock, &hashTarget, &pwallet, &reservekey, &m_cs, &cancelSolver, &chainparams]
#else
                        [&pblock, &hashTarget, &m_cs, &cancelSolver, &chainparams]
#endif
                    (const v_uint8 &soln) {
                    // Write the solution to the hash and compute the result.
                    LogPrint("pow", "- Checking solution against target\n");
                    pblock->nSolution = soln;
                    solutionTargetChecks.increment();

                    const uint256 hashCanonical = pblock->GetHash(BLOCK_HASH_CANONICAL);
                    if (UintToArith256(hashCanonical) > hashTarget)
                        return false;

                    // Found a solution
                    SetThreadPriority(THREAD_PRIORITY_NORMAL);
                    LogPrintf("PastelMiner:\n");
                    LogPrintf(R"(proof-of-work found
    canonical hash: %s
	    block hash: %s
            target: %s
%s)", 
                        hashCanonical.GetHex(), 
                        pblock->GetHashCurrent().GetHex(),
                        hashTarget.GetHex(),
                        pblock->sPastelID.empty() ? "" : strprintf("    mnid: %s\n", pblock->sPastelID));
#ifdef ENABLE_WALLET
                    if (ProcessBlockFound(pblock, chainparams, *pwallet, reservekey))
#else
                    if (ProcessBlockFound(pblock, chainparams))
#endif
                    {
                        // Ignore chain updates caused by us
                        lock_guard<mutex> lock{m_cs};
                        cancelSolver = false;
                    }
                    SetThreadPriority(THREAD_PRIORITY_LOWEST);

                    // In regression test mode, stop mining after a block is found.
                    if (chainparams.MineBlocksOnDemand())
                    {
                        // Increment here because throwing skips the call below
                        ehSolverRuns.increment();
                        throw func_thread_interrupted();
                    }

                    return true;
                };
                function<bool(EhSolverCancelCheck)> cancelled = [&m_cs, &cancelSolver](EhSolverCancelCheck pos)
                {
                    lock_guard<mutex> lock{m_cs};
                    return cancelSolver;
                };

                // TODO: factor this out into a function with the same API for each solver.
                if (gl_MiningSettings.getEquihashSolver() == EquihashSolver::Tromp)
                {
                    // Create solver and initialize it.
                    equi eq(1);
                    eq.setstate(&curr_state);

                    // Initialization done, start algo driver.
                    eq.digit0(0);
                    eq.xfull = eq.bfull = eq.hfull = 0;
                    eq.showbsizes(0);
                    for (u32 r = 1; r < WK; r++) {
                        (r&1) ? eq.digitodd(r, 0) : eq.digiteven(r, 0);
                        eq.xfull = eq.bfull = eq.hfull = 0;
                        eq.showbsizes(r);
                    }
                    eq.digitK(0);
                    ehSolverRuns.increment();

                    // Convert solution indices to byte array (decompress) and pass it to validBlock method.
                    for (size_t s = 0; s < eq.nsols; s++)
                    {
                        LogPrint("pow", "Checking solution %d\n", s+1);
                        vector<eh_index> index_vector(PROOFSIZE);
                        for (size_t i = 0; i < PROOFSIZE; i++)
                            index_vector[i] = eq.sols[s][i];

                        v_uint8 sol_char = GetMinimalFromIndices(index_vector, DIGITBITS);

                        if (validBlock(sol_char))
                        {
                            // If we find a POW solution, do not try other solutions
                            // because they become invalid as we created a new block in blockchain.
                            break;
                        }
                    }
                } else {
                    try {
                        // If we find a valid block, we rebuild
                        bool found = EhOptimisedSolve(n, k, curr_state, validBlock, cancelled);
                        ehSolverRuns.increment();
                        if (found)
                            break;
                        
                    } catch (EhSolverCancelledException&) {
                        LogPrint("pow", "Equihash solver cancelled\n");
                        lock_guard<mutex> lock{m_cs};
                        cancelSolver = false;
                    }
                }

                // Check for stop or if block needs to be rebuilt
                func_thread_interrupt_point();
                // Regtest mode doesn't require peers
                size_t nNodeCount = gl_NodeManager.GetNodeCount();
                if ((nNodeCount == 0) && chainparams.MiningRequiresPeers())
                    break;
                if ((UintToArith256(pblock->nNonce) & 0xffff) == 0xffff)
                    break;
                if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60)
                    break;
                if (pindexPrev != chainActive.Tip())
                    break;

                // Update nNonce and nTime
                pblock->nNonce = ArithToUint256(UintToArith256(pblock->nNonce) + 1);
                UpdateTime(pblock, consensusParams, pindexPrev);
                if (consensusParams.nPowAllowMinDifficultyBlocksAfterHeight.has_value())
                {
                    // Changing pblock->nTime can change work required on testnet:
                    hashTarget.SetCompact(pblock->nBits);
                }
            }
        }
    }
    catch (const func_thread_interrupted&) {
        LogPrintf("PastelMiner terminated\n");
        throw;
    }
    catch (const runtime_error &e) {
        LogPrintf("PastelMiner runtime error: %s\n", e.what());
        return;
    }
}

#ifdef ENABLE_WALLET
void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads, const CChainParams& chainparams)
#else
void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams)
#endif
{
    static CServiceThreadGroup minerThreads;

    if (nThreads < 0)
        nThreads = GetNumCores();

    if (minerThreads.empty())
    {
        minerThreads.stop_all();
        minerThreads.join_all();
    }

    if (nThreads == 0 || !fGenerate)
        return;

    string error;
    for (int i = 0; i < nThreads; i++)
    {
#ifdef ENABLE_WALLET
        minerThreads.add_func_thread(error, "miner", bind(&PastelMiner, i + 1, pwallet));
#else
        minerThreads.add_func_thread(error, "miner", bind(&PastelMiner, i + 1));
#endif
    }
}

#endif // ENABLE_MINING
