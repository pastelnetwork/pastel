// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include <string>
#include <mutex>

#include <utils/utiltime.h>
#include <chain.h>
#include <chainparams.h>
#include <chain_options.h>
#include <script/standard.h>
#include <txdb/burntxindex.h>
#include <txdb/index_defs.h>
#include <txdb/txdb.h>
#include <blockscanner.h>
#include <rpc/protocol.h>
#include <main.h>

using namespace std;

void ProcessBurnTxIndexTask(BlockScannerTask* pTask,
    const uint160& destBurnAddress, const bool bScanAllAddresses, 
    const CTxDestination &destTrackingAddress, 
    const process_burntx_item_func_t &fnProcessItem)
{
    // read blocks from disk and process
    for (size_t i = pTask->nBlockOffsetIndexStart;
        i < min(pTask->nBlockOffsetIndexStart + pTask->nBlockOffsetIndexCount, pTask->vBlockOffsets.size()); ++i)
    {
        CDiskBlockPos blockPos(pTask->nBlockFile, pTask->vBlockOffsets[i]);
        CBlock block;
        if (!ReadBlockFromDisk(block, blockPos, pTask->consensusParams))
            throw JSONRPCError(RPC_MISC_ERROR, "ReadBlockFromDisk failed");

        if (block.vtx.empty())
            continue;

        uint256 hashBlock;
        uint32_t nBlockHeight = 0;
        for (const auto& tx : block.vtx)
        {
            if (tx.IsCoinBase())
                continue;

            for (const auto& txout : tx.vout)
            {
                ScriptType scriptType = txout.scriptPubKey.GetType();
                if (scriptType == ScriptType::UNKNOWN)
                    continue;

                uint160 addressHash = txout.scriptPubKey.AddressHash();
                if (addressHash != destBurnAddress)
                    continue;

                for (uint32_t nTxInIndex = 0; nTxInIndex < tx.vin.size(); ++nTxInIndex)
                {
                    const CTxIn& txin = tx.vin[nTxInIndex];
                    if (txin.prevout.IsNull())
						continue;

                    CTransaction prevTx;
                    uint256 hashInputBlock;
                    if (!GetTransaction(txin.prevout.hash, prevTx, pTask->consensusParams, hashInputBlock, true))
                        continue;

                    if (prevTx.vout.size() <= txin.prevout.n)
						continue;

                    const auto &prevTxOut = prevTx.vout[txin.prevout.n];
                    scriptType = prevTxOut.scriptPubKey.GetType();
                    if (scriptType == ScriptType::UNKNOWN)
                        continue;

                    CTxDestination address;
                    if (!ExtractDestination(prevTxOut.scriptPubKey, address))
                        continue;

                    if (!bScanAllAddresses && (address != destTrackingAddress))
                        continue;

                    if (hashBlock.IsNull())
                    {
                        hashBlock = block.GetHash();
                        LOCK(cs_main);
                        const CBlockIndex* pindex = mapBlockIndex[hashBlock];
                        if (pindex)
                            nBlockHeight = pindex->nHeight;
                    }

                    fnProcessItem(tx.GetHash(), nTxInIndex, hashBlock, nBlockHeight,
                        block.GetBlockTime(), address, txout.nValue);
                }
            }
        }
    }
}

bool GenerateBurnTxIndex(const CChainParams &chainparams, string &error)
{
    if (fBurnTxIndex)
        return true;

    static mutex burnTxIndexMutex;
	unique_lock lock(burnTxIndexMutex);

    if (fBurnTxIndex)
        return true;

    LogFnPrintf("Generating burn transaction index");
    CSimpleTimer timer(true);
    const uint160& destBurnAddress = chainparams.getPastelBurnAddressHash();
    atomic_bool bCreateBurnTxIndexFailed = false;
    string errorCreateBurnTxIndex;
    mutex errorMutex;

    CBlockScanner blockScanner(chainparams.GenesisBlock().GetHash());
    blockScanner.execute("burn-txidx", [&](BlockScannerTask* pTask)
    {
        burn_txindex_vector_t vBurnTxIndex;

        ProcessBurnTxIndexTask(pTask, destBurnAddress, true, CNoDestination(),
            [&](const uint256& txid, const uint32_t nTxInIndex, 
                const uint256& blockHash, const uint32_t nBlockHeight, const int64_t nBlockTime, 
                const CTxDestination& address, const CAmount nValuePat)
        {
            uint160 addressHash;
            ScriptType addressType;
            if (!GetTxDestinationHash(address, addressHash, addressType))
            {
                bCreateBurnTxIndexFailed = true;
                unique_lock errorLock(errorMutex);
                errorCreateBurnTxIndex = strprintf("Invalid address %s", addressHash.GetHex());
				return;
            }

			vBurnTxIndex.emplace_back(
                CBurnTxIndexKey(addressType, addressHash, nBlockHeight, txid, nTxInIndex),
                CBurnTxIndexValue(nValuePat * -1, blockHash, nBlockTime));
		});

        if (!vBurnTxIndex.empty() && !gl_pBlockTreeDB->UpdateBurnTxIndex(vBurnTxIndex))
        {
            bCreateBurnTxIndexFailed = true;
            unique_lock errorLock(errorMutex);
            errorCreateBurnTxIndex = "Failed to update burn tx index database";
            return;
        }
    });

    if (bCreateBurnTxIndexFailed)
    {
        error = move(errorCreateBurnTxIndex);
        LogFnPrintf("Failed to generate burn transaction index. %s", error);
        return false;
    }

    LogFnPrintf("Burn transaction index has been generated in %s", timer.elapsed_time_str());
    fBurnTxIndex = true;
    gl_pBlockTreeDB->WriteFlag(TXDB_FLAG_BURXTXINDEX, true);
    return true;
}
