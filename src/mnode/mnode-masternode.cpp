// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <inttypes.h>

#include <base58.h>
#include <init.h>
#include <netbase.h>
#include <key_io.h>
#include <script/standard.h>
#include <util.h>
#include <main.h>
#include <port_config.h>

#include <mnode/mnode-active.h>
#include <mnode/mnode-masternode.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-validation.h>
#include <mnode/mnode-controller.h>
#include <mnode/tickets/pastelid-reg.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using namespace std;

static constexpr std::array<MNStateInfo, to_integral_type<MASTERNODE_STATE>(MASTERNODE_STATE::COUNT)> MN_STATE_INFO =
{{
    { MASTERNODE_STATE::PRE_ENABLED,            "PRE_ENABLED" },
    { MASTERNODE_STATE::ENABLED,                "ENABLED" },
    { MASTERNODE_STATE::EXPIRED,                "EXPIRED" },
    { MASTERNODE_STATE::OUTPOINT_SPENT,         "OUTPOINT_SPENT" },
    { MASTERNODE_STATE::UPDATE_REQUIRED,        "UPDATE_REQUIRED" },
    { MASTERNODE_STATE::WATCHDOG_EXPIRED,       "WATCHDOG_EXPIRED" },
    { MASTERNODE_STATE::NEW_START_REQUIRED,     "NEW_START_REQUIRED" },
    { MASTERNODE_STATE::POSE_BAN,               "POSE_BAN"}
}};

string MasternodeStateToString(const MASTERNODE_STATE state) noexcept
{
    return MN_STATE_INFO[to_integral_type<MASTERNODE_STATE>(state)].szState;
}

CMasterNodePing::CMasterNodePing()
{
    m_bDefined = false;
}

CMasterNodePing& CMasterNodePing::operator=(const CMasterNodePing& mnp)
{
    m_vin = mnp.m_vin;
    m_blockHash = mnp.m_blockHash;
    m_sigTime = mnp.m_sigTime;
    m_vchSig = mnp.m_vchSig;
    m_bDefined = mnp.IsDefined();
    return *this;
}

CMasterNodePing::CMasterNodePing(const COutPoint& outpoint)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12)
        return;

    m_vin = CTxIn(outpoint);
    m_blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    m_sigTime = GetAdjustedTime();
    m_bDefined = true;
}

std::string CMasterNodePing::getMessage() const noexcept
{
    return m_vin.ToString() + m_blockHash.ToString() + to_string(m_sigTime);
}

bool CMasterNodePing::Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode)
{
    string strError;

    // TODO: add sentinel data
    m_sigTime = GetAdjustedTime();
    const string strMessage = getMessage();
    if (!CMessageSigner::SignMessage(strMessage, m_vchSig, keyMasternode))
    {
        LogFnPrintf("SignMessage() failed");
        return false;
    }

    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, m_vchSig, strMessage, strError))
    {
        LogFnPrintf("VerifyMessage() failed, error: %s", strError);
        return false;
    }

    m_bDefined = true;
    return true;
}

bool CMasterNodePing::CheckSignature(CPubKey& pubKeyMasternode, int &nDos)
{
    // TODO: add sentinel data
    string strError;
    nDos = 0;

    const string strMessage = getMessage();
    if (!CMessageSigner::VerifyMessage(pubKeyMasternode, m_vchSig, strMessage, strError))
    {
        LogFnPrintf("Got bad Masternode ping signature, masternode=%s, error: %s", GetDesc(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CMasterNodePing::SimpleCheck(int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (m_sigTime > GetAdjustedTime() + 60 * 60)
    {
        LogFnPrintf("Signature rejected, too far into the future, masternode=%s", GetDesc());
        nDos = 1;
        return false;
    }

    {
        AssertLockHeld(cs_main);
        const auto mi = mapBlockIndex.find(m_blockHash);
        if (mi == mapBlockIndex.cend())
        {
            LogFnPrint("masternode", "Masternode ping is invalid, unknown block hash: masternode=%s blockHash=%s", 
                GetDesc(), m_blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogFnPrint("masternode", "Masternode ping verified: masternode=%s  blockHash=%s  sigTime=%" PRId64 "", 
        GetDesc(), m_blockHash.ToString(), m_sigTime);
    return true;
}

bool CMasterNodePing::CheckAndUpdate(CMasternode* pmn, bool fFromNewBroadcast, int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos))
        return false;

    if (!pmn)
    {
        LogFnPrint("masternode", "Couldn't find Masternode entry, masternode=%s", GetDesc());
        return false;
    }

    if (!fFromNewBroadcast)
    {
        if (pmn->IsUpdateRequired())
        {
            LogFnPrint("masternode", "masternode protocol is outdated, masternode=%s", GetDesc());
            return false;
        }

        if (pmn->IsNewStartRequired())
        {
            LogFnPrint("masternode", "masternode is completely expired, new start is required, masternode=%s", GetDesc());
            return false;
        }
    }

    {
        LOCK(cs_main);
        const auto mi = mapBlockIndex.find(m_blockHash);
        if (mi->second && mi->second->nHeight < chainActive.Height() - 24)
        {
            LogFnPrintf("Masternode ping is invalid, block hash is too old: masternode=%s  blockHash=%s", GetDesc(), m_blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogFnPrint("masternode", "New ping: masternode=%s  blockHash=%s  sigTime=%" PRId64, 
        GetDesc(), m_blockHash.ToString(), m_sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", GetDesc());
    // update only if there is no known ping for this masternode or
    // last ping was more then masterNodeCtrl.MasternodeMinMNPSeconds-60 ago comparing to this one
    if (pmn->IsPingedWithin(masterNodeCtrl.MasternodeMinMNPSeconds - 60, m_sigTime))
    {
        LogFnPrint("masternode", "Masternode ping arrived too early, masternode=%s", GetDesc());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyMasternode, nDos))
        return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that masterNodeCtrl.MasternodeExpirationSeconds/2 should be enough to finish mn list sync)
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && !pmn->IsPingedWithin(masterNodeCtrl.MasternodeExpirationSeconds/2))
    {
        // let's bump sync timeout
        LogFnPrint("masternode", "ping sync timeout, masternode=%s", GetDesc());
        masterNodeCtrl.masternodeSync.BumpAssetLastTime("CMasternodePing::CheckAndUpdate");
    }

    // let's store this ping as the last one
    LogFnPrint("masternode", "Masternode ping accepted, masternode=%s", GetDesc());
    pmn->setLastPing(*this);

    // and update masterNodeCtrl.masternodeManager.mapSeenMasternodeBroadcast.lastPing which is probably outdated
    CMasternodeBroadcast mnb(*pmn);
    const uint256 hash = mnb.GetHash();
    if (masterNodeCtrl.masternodeManager.mapSeenMasternodeBroadcast.count(hash))
        masterNodeCtrl.masternodeManager.mapSeenMasternodeBroadcast[hash].second.setLastPing(*this);

    // force update, ignoring cache
    pmn->Check(true);
    // relay ping for nodes in ENABLED/EXPIRED/WATCHDOG_EXPIRED state only, skip everyone else
    if (!pmn->IsEnabled() && !pmn->IsExpired() && !pmn->IsWatchdogExpired())
        return false;

    LogFnPrint("masternode", "Masternode ping accepted and relayed, masternode=%s", GetDesc());
    Relay();

    return true;
}

bool CMasterNodePing::IsExpired() const noexcept
{
    return GetAdjustedTime() - m_sigTime > masterNodeCtrl.MasternodeNewStartRequiredSeconds;
}

void CMasterNodePing::Relay()
{
    // Do not relay until fully synced
    if (!masterNodeCtrl.masternodeSync.IsSynced())
    {
        LogFnPrint("masternode", "won't relay until fully synced");
        return;
    }

    CInv inv(MSG_MASTERNODE_PING, GetHash());
    CNodeHelper::RelayInv(inv);
}


/**
* Set new MasterNode's state.
* 
* \param newState - new MasterNode's state
* \param szMethodName - function name to trace MN's state changes
*/
void masternode_info_t::SetState(const MASTERNODE_STATE newState, const char *szMethodName)
{
    if (m_ActiveState == newState)
        return;
    MASTERNODE_STATE prevState = m_ActiveState;
    m_ActiveState = newState;
    if (!LogAcceptCategory("masternode"))
        return;
    string sMsg;
    if (szMethodName && *szMethodName)
    {
        sMsg = szMethodName;
        sMsg += " -- ";
    }
    sMsg += strprintf("Masternode %s has changed state [%s] -> [%s]",
        GetDesc(), MasternodeStateToString(prevState), GetStateString());
    LogPrintf("%s\n", sMsg);
}


CMasternode::CMasternode() : 
    masternode_info_t{ MASTERNODE_STATE::ENABLED, PROTOCOL_VERSION, GetAdjustedTime()},
    m_chainparams(Params())
{
    m_nPoSeBanScore.store(0);
}

CMasternode::CMasternode(const CService &addr, const COutPoint &outpoint, const CPubKey &pubKeyCollateralAddress, const CPubKey &pubKeyMasternode, 
                            const string& strExtraLayerAddress, const string& strExtraLayerP2P, 
                            const string& strExtraLayerKey, const string& strExtraLayerCfg,
                            const int nProtocolVersionIn) :

    masternode_info_t{ MASTERNODE_STATE::ENABLED, nProtocolVersionIn, GetAdjustedTime(),
                       outpoint, addr, pubKeyCollateralAddress, pubKeyMasternode,
                       strExtraLayerAddress, strExtraLayerP2P, strExtraLayerKey, strExtraLayerCfg
                       },
    m_chainparams(Params())
{
    m_nPoSeBanScore.store(0);
}

CMasternode::CMasternode(const CMasternode& other) :
    masternode_info_t{other},
    m_chainparams(Params()),
    vchSig(other.vchSig),
    nCollateralMinConfBlockHash(other.nCollateralMinConfBlockHash),
    nBlockLastPaid(other.nBlockLastPaid),
    fUnitTest(other.fUnitTest),
    aMNFeePerMB(other.aMNFeePerMB),
    aNFTTicketFeePerKB(other.aNFTTicketFeePerKB)
{
    setLastPing(other.getLastPing());
    m_nPoSeBanScore.store(other.m_nPoSeBanScore.load());
    m_nPoSeBanHeight.store(other.m_nPoSeBanHeight.load());
}

CMasternode::CMasternode(const CMasternodeBroadcast& mnb) :
    masternode_info_t{ mnb.GetActiveState(), mnb.nProtocolVersion, mnb.sigTime,
                       mnb.vin.prevout, mnb.addr, mnb.pubKeyCollateralAddress, mnb.pubKeyMasternode,
                       mnb.strExtraLayerAddress, mnb.strExtraLayerP2P, mnb.strExtraLayerKey, mnb.strExtraLayerCfg,
                       mnb.sigTime /*nTimeLastWatchdogVote*/},
    m_chainparams(Params()),
    vchSig(mnb.vchSig)
{
    setLastPing(mnb.getLastPing());
    m_nPoSeBanScore = 0;
}

/**
 * Check and update last ping.
 * 
 * \param nDos - output Denial-of-Service code
 * \param bSimpleCheck - if true, perform simple check only
 * 
 * \return true of ping is passed the specified check
 */
bool CMasternode::CheckAndUpdateLastPing(int &nDos, const bool bSimpleCheck)
{
    return bSimpleCheck ? m_lastPing.SimpleCheck(nDos) : m_lastPing.CheckAndUpdate(this, true, nDos);
}

//
// When a new masternode broadcast is sent, update our information
//
bool CMasternode::UpdateFromNewBroadcast(CMasternodeBroadcast& mnb)
{
    if (mnb.sigTime <= sigTime && !mnb.fRecovery)
        return false;

    pubKeyMasternode = mnb.pubKeyMasternode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    strExtraLayerAddress = mnb.strExtraLayerAddress;
    strExtraLayerP2P = mnb.strExtraLayerP2P;
    strExtraLayerKey = mnb.strExtraLayerKey;
    strExtraLayerCfg = mnb.strExtraLayerCfg;
    aMNFeePerMB = 0;
    aNFTTicketFeePerKB = 0;
    m_nPoSeBanScore = 0;
    m_nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.IsLastPingDefined() || mnb.CheckAndUpdateLastPing(nDos, false))
    {
        setLastPing(mnb.getLastPing());
        masterNodeCtrl.masternodeManager.mapSeenMasternodePing.emplace(m_lastPing.GetHash(), m_lastPing);
    }
    // if it matches our Masternode public key...
    if (masterNodeCtrl.IsMasterNode() && pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode)
    {
        PoSeUnBan();
        if (nProtocolVersion == PROTOCOL_VERSION)
        {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            masterNodeCtrl.activeMasternode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogFnPrintf("wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Masternode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CMasternode::CalculateScore(const uint256& blockHash)
{
    if (nCollateralMinConfBlockHash.IsNull())
    {
        LogFnPrint("masternode", "Masternode %s has nCollateralMinConfBlockHash NOT set, will try to set it now", GetDesc());
        CollateralStatus collateralStatus = CollateralStatus::OK;
        VerifyCollateral(collateralStatus);
    }
    
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin.prevout << nCollateralMinConfBlockHash << blockHash;
    return UintToArith256(ss.GetHash());
}

CMasternode::CollateralStatus CMasternode::CheckCollateral(const COutPoint& outpoint)
{
    int nHeight;
    return CheckCollateral(outpoint, nHeight);
}

CMasternode::CollateralStatus CMasternode::CheckCollateral(const COutPoint& outpoint, int& nHeightRet)
{
    AssertLockHeld(cs_main);

    CCoins coins;
    if (!GetUTXOCoin(outpoint, coins))
        return CollateralStatus::UTXO_NOT_FOUND;

    if (coins.vout[outpoint.n].nValue != masterNodeCtrl.MasternodeCollateral*COIN)
        return CollateralStatus::INVALID_AMOUNT;

    nHeightRet = coins.nHeight;
    return CollateralStatus::OK;
}

/**
 * Check & update Masternode's state.
 * 
 * \param fForce
 */
void CMasternode::Check(const bool fForce)
{
    LOCK(cs);

    if (ShutdownRequested())
        return;

    if (!fForce && (GetTime() - nTimeLastChecked < masterNodeCtrl.MasternodeCheckSeconds))
        return;
    nTimeLastChecked = GetTime();

    // once MN outpoint is spent, stop doing the checks
    if (IsOutpointSpent())
        return;

    uint32_t nCurrentHeight = 0;
    if (!fUnitTest)
    {
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain)
            return;

        CollateralStatus err = CheckCollateral(vin.prevout);
        if (err == CollateralStatus::UTXO_NOT_FOUND)
        {
            LogFnPrint("masternode", "Failed to find Masternode UTXO, masternode=%s", GetDesc());
            SetState(MASTERNODE_STATE::OUTPOINT_SPENT, __METHOD_NAME__);
            return;
        }

        const int nHeight = chainActive.Height();
        if (nHeight > 0)
            nCurrentHeight = static_cast<uint32_t>(nHeight);
    }

    // PoSe (Proof of Service) ban score feature
    if (IsPoSeBanned())
    {
        // MN is banned till nPoSeBanHeight
        if (nCurrentHeight < m_nPoSeBanHeight)
            return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Masternode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogFnPrintf("Masternode %s is unbanned and back in list now", GetDesc());
        DecrementPoSeBanScore();
    } else
        if (IsPoSeBannedByScore())
        {
            // ban for the whole payment cycle
            m_nPoSeBanHeight = nCurrentHeight + static_cast<uint32_t>(masterNodeCtrl.masternodeManager.size());
            LogFnPrintf("Masternode %s is banned till block %u now", GetDesc(), m_nPoSeBanHeight.load());
            // change MN's state to POSE_BAN
            SetState(MASTERNODE_STATE::POSE_BAN, __METHOD_NAME__);
            return;
        }

    const bool fOurMasterNode = masterNodeCtrl.IsMasterNode() && masterNodeCtrl.activeMasternode.pubKeyMasternode == pubKeyMasternode;

    // change status to UPDATE_REQUIRED if:
    //   - masternode doesn't meet payment protocol requirements ... or
    //   - we're masternode and our current protocol version is less than latest
    const bool bUpdateRequired = (nProtocolVersion < masterNodeCtrl.GetSupportedProtocolVersion()) ||
        (fOurMasterNode && (nProtocolVersion < PROTOCOL_VERSION));
    if (bUpdateRequired)
    {
        SetState(MASTERNODE_STATE::UPDATE_REQUIRED, __METHOD_NAME__);
        return;
    }

    // keep old masternodes on start, give them a chance to receive updates...
    const bool fWaitForPing = !masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && !IsPingedWithin(masterNodeCtrl.MasternodeMinMNPSeconds);

    if (fWaitForPing && !fOurMasterNode)
    {
        // ...but if it was already expired before the initial check - return right away
        if (IsExpired() || IsWatchdogExpired() || IsNewStartRequired())
        {
            LogFnPrint("masternode", "Masternode %s is in %s state, waiting for ping", GetDesc(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own masternode
    if (!fWaitForPing || fOurMasterNode)
    {

        if (!IsPingedWithin(masterNodeCtrl.MasternodeNewStartRequiredSeconds))
        {
            SetState(MASTERNODE_STATE::NEW_START_REQUIRED, __METHOD_NAME__);
            return;
        }

        const bool fWatchdogActive = masterNodeCtrl.masternodeSync.IsSynced() && masterNodeCtrl.masternodeManager.IsWatchdogActive();
        const bool fWatchdogExpired = (fWatchdogActive && ((GetAdjustedTime() - nTimeLastWatchdogVote) > masterNodeCtrl.MasternodeWatchdogMaxSeconds));

        LogFnPrint("masternode", "outpoint=%s, nTimeLastWatchdogVote=%d, GetAdjustedTime()=%d, fWatchdogExpired=%d",
            GetDesc(), nTimeLastWatchdogVote, GetAdjustedTime(), fWatchdogExpired);

        if (fWatchdogExpired)
        {
            SetState(MASTERNODE_STATE::WATCHDOG_EXPIRED, __METHOD_NAME__);
            return;
        }

        if (!IsPingedWithin(masterNodeCtrl.MasternodeExpirationSeconds))
        {
            SetState(MASTERNODE_STATE::EXPIRED, __METHOD_NAME__);
            return;
        }
    }

    // if ping was received less than MasternodeMinMNPSeconds since last broadcast - can't enabled MN
    if (m_lastPing.IsPingedWithin(masterNodeCtrl.MasternodeMinMNPSeconds, sigTime))
    {
        SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__);
        return;
    }

    // check that this MN has registered Pastel ID (mnid)
    CPastelIDRegTicket mnidTicket;
    mnidTicket.setSecondKey(vin.prevout.ToStringShort());
    if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(mnidTicket))
    {
        LogFnPrint("masternode", "Masternode %s does not have registered Pastel ID", GetDesc());
        SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__);
        return;
    }
    if (fOurMasterNode)
    {
        // if we're running in MasterNode mode - check that MNID actually exists locally
        const auto mapIDs = CPastelID::GetStoredPastelIDs(true, mnidTicket.getPastelID());
        if (mapIDs.empty())
        {
            LogFnPrint("masternode", "Masternode %s registered Pastel ID '%s' is not stored locally", GetDesc(), mnidTicket.getPastelID());
            SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__);
            return;
        }
    }

    SetState(MASTERNODE_STATE::ENABLED, __METHOD_NAME__); // OK
}

bool CMasternode::IsInputAssociatedWithPubkey()
{
    CScript payee;
    payee = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    CTransaction tx;
    uint256 hash;
    if (GetTransaction(vin.prevout.hash, tx, m_chainparams.GetConsensus(), hash, true))
    {
        for (const auto & out : tx.vout)
        {
            if (out.nValue == masterNodeCtrl.MasternodeCollateral * COIN && out.scriptPubKey == payee)
                return true;
        }
    }

    return false;
}

bool CMasternode::IsValidNetAddr()
{
    return IsValidNetAddr(addr);
}

bool CMasternode::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().IsRegTest() ||
            (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

masternode_info_t CMasternode::GetInfo() const noexcept
{
    masternode_info_t info{*this};
    info.nTimeLastPing = m_lastPing.getSigTime();
    info.fInfoValid = true;
    return info;
}

string CMasternode::GetStatus() const
{
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

void CMasternode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex)
        return;

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    LogFnPrint("masternode", "searching for block with payment to %s", GetDesc());

    LOCK(cs_mapMasternodeBlockPayees);

    const auto &consensusParams = m_chainparams.GetConsensus();
    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
        if(masterNodeCtrl.masternodePayments.mapMasternodeBlockPayees.count(BlockReading->nHeight) &&
            masterNodeCtrl.masternodePayments.mapMasternodeBlockPayees[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2))
        {
            CBlock block;
            if (!ReadBlockFromDisk(block, BlockReading, consensusParams)) // shouldn't really happen
                continue;

            CAmount nMasternodePayment = masterNodeCtrl.masternodePayments.GetMasternodePayment(BlockReading->nHeight, block.vtx[0].GetValueOut());

            for (const auto & txout : block.vtx[0].vout)
            {
                if(mnpayee == txout.scriptPubKey && nMasternodePayment == txout.nValue) {
                    nBlockLastPaid = BlockReading->nHeight;
                    nTimeLastPaid = BlockReading->nTime;
                    LogFnPrint("masternode", "searching for block with payment to %s -- found new %d",
                        GetDesc(), nBlockLastPaid);
                    return;
                }
            }
        }

        if (!BlockReading->pprev)
        { 
            assert(BlockReading); 
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this masternode wasn't found in latest mnpayments blocks
    // or it was found in mnpayments blocks but wasn't found in the blockchain.
    // LogFnPrint("masternode", "searching for block with payment to %s -- keeping old %d", GetDesc(), nBlockLastPaid);
}

void CMasternode::UpdateWatchdogVoteTime(const uint64_t nVoteTime)
{
    LOCK(cs);
    nTimeLastWatchdogVote = (nVoteTime == 0) ? GetAdjustedTime() : nVoteTime;
}

bool CMasternode::IsPingedWithin(const int nSeconds, int64_t nTimeToCheckAt) const noexcept
{
    if (!m_lastPing.IsDefined())
        return false;
    if (nTimeToCheckAt == -1)
        nTimeToCheckAt = GetAdjustedTime();
    return m_lastPing.IsPingedWithin(nSeconds, nTimeToCheckAt);
}

CMasternode& CMasternode::operator=(CMasternode const& from)
{
    static_cast<masternode_info_t&>(*this)=from;
    setLastPing(from.getLastPing());
    vchSig = from.vchSig;
    nCollateralMinConfBlockHash = from.nCollateralMinConfBlockHash;
    nBlockLastPaid = from.nBlockLastPaid;
    m_nPoSeBanScore.store(from.m_nPoSeBanScore.load());
    m_nPoSeBanHeight.store(from.m_nPoSeBanHeight.load());
    fUnitTest = from.fUnitTest;
    aMNFeePerMB = from.aMNFeePerMB;
    aNFTTicketFeePerKB = from.aNFTTicketFeePerKB;
    return *this;
}

bool CMasternode::IsPoSeVerified()
{
    return m_nPoSeBanScore <= -masterNodeCtrl.getPOSEBanMaxScore();
}

/**
 * Increment PoSe ban score.
 * 
 * \return new PoSe ban score
 */
int CMasternode::IncrementPoSeBanScore()
{
    int nPoSeBanScore;
    if (m_nPoSeBanScore < masterNodeCtrl.getPOSEBanMaxScore())
        nPoSeBanScore = ++m_nPoSeBanScore;
    else
        nPoSeBanScore = m_nPoSeBanScore.load();
    return nPoSeBanScore;
}

/**
 * Decrement PoSe ban score.
 * 
 * \return new PoSe ban score
 */
int CMasternode::DecrementPoSeBanScore()
{
    int nPoSeBanScore;
    if (m_nPoSeBanScore > -masterNodeCtrl.getPOSEBanMaxScore())
        nPoSeBanScore = --m_nPoSeBanScore;
    else
        nPoSeBanScore = m_nPoSeBanScore.load();
    return nPoSeBanScore;
}

// ban node
void CMasternode::PoSeBan()
{
    m_nPoSeBanScore.store(masterNodeCtrl.getPOSEBanMaxScore());
}

// unban node
void CMasternode::PoSeUnBan()
{
    m_nPoSeBanScore.store(-masterNodeCtrl.getPOSEBanMaxScore());
}

/**
 * Check if MN is banned by PoSe score.
 * 
 * \return 
 */
bool CMasternode::IsPoSeBannedByScore() const noexcept
{
    return m_nPoSeBanScore >= masterNodeCtrl.getPOSEBanMaxScore();
}

bool CMasternode::VerifyCollateral(CollateralStatus& collateralStatus)
{
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
    {
        // not mnb fault, let it to be checked again later
        LogFnPrint("masternode", "Failed to aquire lock, addr=%s", addr.ToString());
        return false;
    }
    
    int nHeight;
    collateralStatus = CheckCollateral(vin.prevout, nHeight);
    if (collateralStatus == CollateralStatus::UTXO_NOT_FOUND)
    {
        LogFnPrint("masternode", "Failed to find Masternode UTXO, masternode=%s", GetDesc());
        return false;
    }
    
    if (collateralStatus == CollateralStatus::INVALID_AMOUNT)
    {
        LogFnPrint("masternode", "Masternode UTXO should have %d PASTEL, masternode=%s",
                 masterNodeCtrl.MasternodeCollateral, GetDesc());
        return false;
    }
    
    const int nConfirmations = chainActive.Height() - nHeight + 1;
    if (nConfirmations < 0 || static_cast<uint32_t>(nConfirmations) < masterNodeCtrl.nMasternodeMinimumConfirmations)
    {
        LogFnPrintf("Masternode UTXO must have at least %u confirmations, masternode=%s",
                    masterNodeCtrl.nMasternodeMinimumConfirmations, GetDesc());
        // maybe we miss few blocks, let this mnb to be checked again later
        return false;
    }
    // remember the hash of the block where masternode collateral had minimum required confirmations
    nCollateralMinConfBlockHash = chainActive[nHeight + masterNodeCtrl.nMasternodeMinimumConfirmations-1]->GetBlockHash();
    LogFnPrintf("Masternode UTXO CollateralMinConfBlockHash is [%s], masternode=%s",
            nCollateralMinConfBlockHash.ToString(), GetDesc());
    
    LogFnPrint("masternode", "Masternode UTXO verified");
    return true;
}

#ifdef ENABLE_WALLET
bool CMasternodeBroadcast::Create(const string &strService, const string &strKeyMasternode, const string &strTxHash,
    const string &strOutputIndex, const string &strExtraLayerAddress, const string &strExtraLayerP2P,
    const string &strExtraLayerKey, const string &strExtraLayerCfg,
    string& strErrorRet, CMasternodeBroadcast &mnbRet, bool fOffline)
{
    COutPoint outpoint;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyMasternodeNew;
    CKey keyMasternodeNew;

    const auto fnLog = [&](const string &sErr) -> bool
    {
        strErrorRet = sErr;
        LogPrintf("[CMasternodeBroadcast::Create] %s\n", sErr);
        return false;
    };

    //need correct blocks to send ping
    if (!fOffline && !masterNodeCtrl.masternodeSync.IsBlockchainSynced())
        return fnLog("Sync in progress. Must wait until sync is complete to start Masternode");

    if (!CMessageSigner::GetKeysFromSecret(strKeyMasternode, keyMasternodeNew, pubKeyMasternodeNew))
        return fnLog(strprintf("Invalid masternode key %s", strKeyMasternode));

    string error;
    if (!GetMasternodeOutpointAndKeys(pwalletMain, error, outpoint, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex))
        return fnLog(strprintf("Could not allocate outpoint %s:%s for masternode %s. %s", strTxHash, strOutputIndex, strService, error));
    
    const int outpointConfirmations = GetUTXOConfirmations(outpoint);
    if (outpointConfirmations < 0 || (static_cast<uint32_t>(outpointConfirmations) < masterNodeCtrl.nMasternodeMinimumConfirmations))
    {
        string sMsg = strprintf("Masternode UTXO must have at least %u confirmations", masterNodeCtrl.nMasternodeMinimumConfirmations);
        if (outpointConfirmations >= 0)
            sMsg += strprintf(", has only %d", outpointConfirmations);
        return fnLog(sMsg);
    }
    
    CService service;
    if (!Lookup(strService.c_str(), service, 0, false))
        return fnLog(strprintf("Invalid address %s for masternode.", strService));
    if (Params().IsMainNet())
    {
        if (service.GetPort() != MAINNET_DEFAULT_PORT)
            return fnLog(strprintf("Invalid port %u for masternode %s, only %hu is supported on mainnet.", service.GetPort(), strService, MAINNET_DEFAULT_PORT));
    } else if (service.GetPort() == MAINNET_DEFAULT_PORT)
        return fnLog(strprintf("Invalid port %u for masternode %s, %hu is the only supported on mainnet.", service.GetPort(), strService, MAINNET_DEFAULT_PORT));

    return Create(outpoint, 
                    service, 
                    keyCollateralAddressNew, pubKeyCollateralAddressNew, 
                    keyMasternodeNew, pubKeyMasternodeNew,
                    strExtraLayerAddress, strExtraLayerP2P, strExtraLayerKey, strExtraLayerCfg,
                    strErrorRet, mnbRet);
}

bool CMasternodeBroadcast::Create(const COutPoint& outpoint, 
                                    const CService& service, 
                                    const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, 
                                    const CKey& keyMasternodeNew, const CPubKey& pubKeyMasternodeNew, 
                                    const string& strExtraLayerAddress, const string& strExtraLayerP2P,
                                    const string& strExtraLayerKey, const string& strExtraLayerCfg,
                                    string &strErrorRet, CMasternodeBroadcast &mnbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex)
    {
        strErrorRet = strprintf("Cannot create MasterNode broadcast message - %s", fImporting ? "importing blocks" : "reindexing blocks");
        return false;
    }

    KeyIO keyIO(Params());
    const CTxDestination dest = pubKeyCollateralAddressNew.GetID();
    string address = keyIO.EncodeDestination(dest);

    LogFnPrint("masternode", "pubKeyCollateralAddressNew = %s, pubKeyMasternodeNew.GetID() = %s",
             address, pubKeyMasternodeNew.GetID().ToString());

    const auto fnLog = [&](const string &sErr) -> bool
    {
        strErrorRet = sErr;
        LogPrintf("[CMasternodeBroadcast::Create] %s\n", sErr);
        mnbRet = CMasternodeBroadcast();
        return false;
    };

    CMasterNodePing mnp(outpoint);
    if (!mnp.Sign(keyMasternodeNew, pubKeyMasternodeNew))
        return fnLog(strprintf("Failed to sign ping, masternode=%s", outpoint.ToStringShort()));

    mnbRet = CMasternodeBroadcast(service, outpoint, pubKeyCollateralAddressNew, pubKeyMasternodeNew, 
                                    strExtraLayerAddress, strExtraLayerP2P, strExtraLayerKey, strExtraLayerCfg,
                                    PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr())
        return fnLog(strprintf("Invalid IP address, masternode=%s", outpoint.ToStringShort()));

    mnbRet.setLastPing(mnp);
    if (!mnbRet.Sign(keyCollateralAddressNew))
        return fnLog(strprintf("Failed to sign broadcast, masternode=%s", outpoint.ToStringShort()));

    return true;
}
#endif // ENABLE_WALLET

bool CMasternodeBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    // make sure addr is valid
    if(!IsValidNetAddr())
    {
        LogFnPrintf("Invalid addr, rejected: masternode=%s  addr=%s", GetDesc(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60)
    {
        LogFnPrintf("Signature rejected, too far into the future: masternode=%s", GetDesc());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if (!IsLastPingDefined() || !CheckAndUpdateLastPing(nDos, true))
    {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        SetState(MASTERNODE_STATE::EXPIRED, __METHOD_NAME__);
    }

    if (nProtocolVersion < masterNodeCtrl.GetSupportedProtocolVersion())
    {
        LogFnPrintf("ignoring outdated Masternode: masternode=%s  nProtocolVersion=%d", 
            GetDesc(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25)
    {
        LogFnPrintf("pubKeyCollateralAddress has the wrong size");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyMasternode.GetID());

    if (pubkeyScript2.size() != 25)
    {
        LogFnPrintf("pubKeyMasternode has the wrong size");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty())
    {
        LogFnPrintf("Ignore Not Empty ScriptSig %s",vin.ToString());
        nDos = 100;
        return false;
    }

    if(Params().IsMainNet())
    {
        if(addr.GetPort() != MAINNET_DEFAULT_PORT)
            return false;
    } else if (addr.GetPort() == MAINNET_DEFAULT_PORT)
        return false;

    return true;
}

bool CMasternodeBroadcast::Update(CMasternode* pmn, int& nDos)
{
    nDos = 0;

    if (pmn->sigTime == sigTime && !fRecovery)
    {
        // mapSeenMasternodeBroadcast in CMasternodeMan::CheckMnbAndUpdateMasternodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if (pmn->sigTime > sigTime)
    {
        LogFnPrintf("Bad sigTime %" PRId64 " (existing broadcast is at %" PRId64 ") for Masternode %s %s",
                      sigTime, pmn->sigTime, GetDesc(), addr.ToString());
        return false;
    }

    pmn->Check();

    // masternode is banned by PoSe
    if (pmn->IsPoSeBanned())
    {
        LogFnPrintf("Banned by PoSe, masternode=%s", GetDesc());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if (pmn->pubKeyCollateralAddress != pubKeyCollateralAddress)
    {
        LogFnPrintf("Got mismatched pubKeyCollateralAddress and vin");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos))
    {
        LogFnPrintf("CheckSignature() failed, masternode=%s", GetDesc());
        return false;
    }

    // if ther was no masternode broadcast recently or if it matches our Masternode privkey...
    if (!pmn->IsBroadcastedWithin(masterNodeCtrl.MasternodeMinMNBSeconds) || 
        (masterNodeCtrl.IsMasterNode() && pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode))
    {
        // take the newest entry
        LogFnPrintf("Got UPDATED Masternode entry: addr=%s", addr.ToString());
        if (pmn->UpdateFromNewBroadcast(*this))
        {
            pmn->Check();
            Relay();
        }
        masterNodeCtrl.masternodeSync.BumpAssetLastTime("CMasternodeBroadcast::Update");
    }

    return true;
}

/*
Check collateral tnx in the Anounce message is correct - 
    - it exists
    - it has correct amount
    - there were right number of confirmations (number of blocks after)
    - verify signature
*/
bool CMasternodeBroadcast::CheckOutpoint(int& nDos)
{
    // we are a masternode with the same vin (i.e. already activated) and this mnb is ours (matches our Masternode privkey)
    // so nothing to do here for us
    if (masterNodeCtrl.IsMasterNode() && vin.prevout == masterNodeCtrl.activeMasternode.outpoint && pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode)
        return false;

    if (!CheckSignature(nDos))
    {
        LogFnPrintf("CheckSignature() failed, masternode=%s", GetDesc());
        return false;
    }
    
    CollateralStatus collateralStatus = CollateralStatus::OK;
    if (!VerifyCollateral(collateralStatus))
    {
        // if error but collateral itself is OK, let this mnb to be checked again later
        if (collateralStatus == CollateralStatus::OK)
            masterNodeCtrl.masternodeManager.mapSeenMasternodeBroadcast.erase(GetHash());
        return false;
    }
    
    // make sure the input that was signed in masternode broadcast message is related to the transaction
    // that spawned the Masternode - this is expensive, so it's only done once per Masternode
    if (!IsInputAssociatedWithPubkey())
    {
        LogFnPrintf("Got mismatched pubKeyCollateralAddress and vin");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 PASTEL tx got nMasternodeMinimumConfirmations
    uint256 hashBlock;
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, m_chainparams.GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        const auto mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.cend() && mi->second)
        {
            CBlockIndex* pMNIndex = mi->second; // block for 1000 PASTEL tx -> 1 confirmation
            CBlockIndex* pConfIndex = chainActive[pMNIndex->nHeight + masterNodeCtrl.nMasternodeMinimumConfirmations - 1]; // block where tx got nMasternodeMinimumConfirmations
            if (pConfIndex->GetBlockTime() > sigTime)
            {
                LogFnPrintf("Bad sigTime %" PRId64 " (%u conf block is at %" PRId64 ") for Masternode %s %s",
                     sigTime, masterNodeCtrl.nMasternodeMinimumConfirmations, pConfIndex->GetBlockTime(), GetDesc(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CMasternodeBroadcast::Sign(const CKey& keyCollateralAddress)
{
    string strError;
    string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString(false) + to_string(sigTime) +
                    pubKeyCollateralAddress.GetID().ToString() + pubKeyMasternode.GetID().ToString() +
                    to_string(nProtocolVersion);

    if (!CMessageSigner::SignMessage(strMessage, vchSig, keyCollateralAddress))
    {
        LogFnPrintf("SignMessage() failed");
        return false;
    }

    if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError))
    {
        LogFnPrintf("VerifyMessage() failed, error: %s", strError);
        return false;
    }

    return true;
}

bool CMasternodeBroadcast::CheckSignature(int& nDos)
{
    nDos = 0;
    string strError;
    string strMessage = addr.ToString(false) + to_string(sigTime) +
                    pubKeyCollateralAddress.GetID().ToString() + pubKeyMasternode.GetID().ToString() +
                    to_string(nProtocolVersion);

    KeyIO keyIO(Params());
    const CTxDestination dest = pubKeyCollateralAddress.GetID();
    string address = keyIO.EncodeDestination(dest);

    LogFnPrint("masternode", "strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s", 
                strMessage, address, EncodeBase64(&vchSig[0], vchSig.size()));

    if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError))
    {
        LogFnPrintf("Got bad Masternode announce signature, error: %s", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CMasternodeBroadcast::Relay()
{
    // Do not relay until fully synced
    if (!masterNodeCtrl.masternodeSync.IsSynced())
    {
        LogFnPrint("masternode", "won't relay until fully synced");
        return;
    }

    CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash());
    CNodeHelper::RelayInv(inv);
}

// check if pinged after mnb
bool CMasternodeBroadcast::IsPingedAfter(const CMasternodeBroadcast& mnb) const noexcept
{
    if (!IsLastPingDefined())
        return false;
    return m_lastPing.IsPingedAfter(mnb.getLastPing().getSigTime());
}

void CMasternodeVerification::Relay() const
{
    CInv inv(MSG_MASTERNODE_VERIFY, GetHash());
    CNodeHelper::RelayInv(inv);
}

