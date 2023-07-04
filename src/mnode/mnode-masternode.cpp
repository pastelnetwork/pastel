// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
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

//
//  ----------------- Masternode Ping  ------------------------------------------------------------------------------
//
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

bool CMasterNodePing::CheckSignature(CPubKey& pubKeyMasternode, int &nDos) const
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

bool CMasterNodePing::SimpleCheck(int& nDos) const noexcept
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

bool CMasterNodePing::CheckAndUpdate(CMasternode* pmn, bool fFromNewBroadcast, int& nDos) const
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
        if (mi->second && static_cast<uint32_t>(mi->second->nHeight) < gl_nChainHeight - 24)
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
        masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__);
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

void CMasterNodePing::Relay() const
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


//
//  ----------------- masternode_info_t  ------------------------------------------------------------------------------
//
masternode_info_t::masternode_info_t(const MASTERNODE_STATE activeState, const int protoVer, const int64_t sTime,
                    const COutPoint &outpoint, const CService &addr,
                    const CPubKey &pkCollAddr, const CPubKey &pkMN,
                    const string& extAddress, const string& extP2P, const string& extCfg,
                    int64_t tWatchdogV) :
    m_ActiveState{activeState}, 
    nProtocolVersion{protoVer}, 
    sigTime{sTime},
    m_vin(outpoint),
    m_addr{addr},
    pubKeyCollateralAddress{pkCollAddr}, pubKeyMasternode{pkMN},
    strExtraLayerAddress{extAddress}, strExtraLayerP2P{extP2P}, 
    strExtraLayerCfg{extCfg},
    nTimeLastWatchdogVote{tWatchdogV}
{}

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

bool CMasternode::fCompatibilityReadMode = false;

//
//  ----------------- CMasternode  ------------------------------------------------------------------------------
//
CMasternode::CMasternode() : 
    masternode_info_t{ MASTERNODE_STATE::ENABLED, PROTOCOL_VERSION, GetAdjustedTime()},
    m_chainparams(Params())
{
    m_nPoSeBanScore.store(0);
}

CMasternode::CMasternode(const CService &addr, const COutPoint &outpoint, const CPubKey &pubKeyCollateralAddress, const CPubKey &pubKeyMasternode, 
                            const string& strExtraLayerAddress, const string& strExtraLayerP2P, const string& strExtraLayerCfg,
                            const int nProtocolVersionIn) :
    masternode_info_t
    { 
        MASTERNODE_STATE::ENABLED, nProtocolVersionIn, GetAdjustedTime(),
        outpoint, addr, pubKeyCollateralAddress, pubKeyMasternode,
        strExtraLayerAddress, strExtraLayerP2P, strExtraLayerCfg
    },
    m_chainparams(Params())
{
    m_nPoSeBanScore = 0;
}

CMasternode::CMasternode(const CMasternode& other) :
    masternode_info_t{other},
    m_chainparams(Params()),
    vchSig(other.vchSig),
    m_collateralMinConfBlockHash(other.m_collateralMinConfBlockHash),
    m_nBlockLastPaid(other.m_nBlockLastPaid),
    fUnitTest(other.fUnitTest),
    m_nMNFeePerMB(other.m_nMNFeePerMB),
    m_nTicketChainStorageFeePerKB(other.m_nTicketChainStorageFeePerKB),
    m_nSenseComputeFee(other.m_nSenseComputeFee),
    m_nSenseProcessingFeePerMB(other.m_nSenseProcessingFeePerMB)
{
    setLastPing(other.getLastPing());
    m_nPoSeBanScore.store(other.m_nPoSeBanScore.load());
    m_nPoSeBanHeight.store(other.m_nPoSeBanHeight.load());
}

CMasternode::CMasternode(const CMasternodeBroadcast& mnb) :
    masternode_info_t{ mnb.GetActiveState(), mnb.nProtocolVersion, mnb.sigTime,
                       mnb.m_vin.prevout, mnb.get_addr(), mnb.pubKeyCollateralAddress, mnb.pubKeyMasternode,
                       mnb.strExtraLayerAddress, mnb.strExtraLayerP2P, mnb.strExtraLayerCfg,
                       mnb.sigTime /*nTimeLastWatchdogVote*/},
    m_chainparams(Params()),
    vchSig(mnb.vchSig)
{
    setLastPing(mnb.getLastPing());
    m_nPoSeBanScore = 0;
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
    m_addr = mnb.m_addr;
    strExtraLayerAddress = mnb.strExtraLayerAddress;
    strExtraLayerP2P = mnb.strExtraLayerP2P;
    strExtraLayerCfg = mnb.strExtraLayerCfg;
    if (mnb.GetVersion() >= 1)
    {
        m_nMNFeePerMB = mnb.m_nMNFeePerMB;
        m_nTicketChainStorageFeePerKB = mnb.m_nTicketChainStorageFeePerKB;
        m_nSenseComputeFee = mnb.m_nSenseComputeFee;
        m_nSenseProcessingFeePerMB = mnb.m_nSenseProcessingFeePerMB;
    }
    else
    {
        m_nMNFeePerMB = 0;
        m_nTicketChainStorageFeePerKB = 0;
        m_nSenseComputeFee = 0;
        m_nSenseProcessingFeePerMB = 0;
    }
    m_nPoSeBanScore = 0;
    m_nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.IsLastPingDefined() || mnb.CheckAndUpdateLastPing(nDos))
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

// Deterministically calculate a given "score" for a Masternode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
arith_uint256 CMasternode::CalculateScore(const uint256& blockHash)
{
    if (m_collateralMinConfBlockHash.IsNull())
    {
        LogFnPrint("masternode", "Masternode %s has nCollateralMinConfBlockHash NOT set, will try to set it now", GetDesc());
        CollateralStatus collateralStatus = CollateralStatus::OK;
        VerifyCollateral(collateralStatus);
    }
    
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << m_vin.prevout << m_collateralMinConfBlockHash << blockHash;
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
 * Check and update MasterNode's Pastel ID.
 * MasterNode should have Pastel ID (MNID) registered in a blockchain with Pastel ID Registration ticket.
 * Masternode pays for this transaction with a collateral ammount.
 * This collateral transaction is defined by transaction id + index (txid-index) - collateral id.
 * Collateral ID is used as a secondary key for Pastel ID Registration tickets.
 * Sets MN's Pastel ID (m_sMNPastelID) in case mnid was found and not empty.
 *  
 * \return true if MasterNode's Pastel ID is registered
 */
bool CMasternode::CheckAndUpdateMNID(string &error)
{
    // check that this MN has registered Pastel ID (mnid)
    CPastelIDRegTicket mnidTicket;
    mnidTicket.setSecondKey(m_vin.prevout.ToStringShort());
    if (!masterNodeCtrl.masternodeTickets.FindTicketBySecondaryKey(mnidTicket))
    {
        error = strprintf("Masternode %s does not have registered Pastel ID", GetDesc());
        return false;
    }

    // set MN Pastel ID which is registered using collateral transaction (txid-index)
    m_sMNPastelID = mnidTicket.getPastelID();
    if (m_sMNPastelID.empty())
    {
        error = strprintf("Masternode %s has empty registered Pastel ID", GetDesc());
        return false;
    }
    return true;
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

        CollateralStatus err = CheckCollateral(m_vin.prevout);
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
    // sets m_sMNPastelID if successfull
    string error;
    if (!CheckAndUpdateMNID(error))
    {
        LogFnPrint("masternode", "%s", error);
        SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__);
        return;
    }
    if (fOurMasterNode)
    {
        // if we're running in MasterNode mode - check that MNID actually exists locally
        const auto mapIDs = CPastelID::GetStoredPastelIDs(true, m_sMNPastelID);
        if (mapIDs.empty())
        {
            LogFnPrint("masternode", "Masternode %s registered Pastel ID '%s' is not stored locally", GetDesc(), m_sMNPastelID);
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
    if (GetTransaction(m_vin.prevout.hash, tx, m_chainparams.GetConsensus(), hash, true))
    {
        for (const auto & out : tx.vout)
        {
            if (out.nValue == masterNodeCtrl.MasternodeCollateral * COIN && out.scriptPubKey == payee)
                return true;
        }
    }

    return false;
}

bool CMasternode::IsValidNetAddr(const CService &addr)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().IsRegTest() ||
            (addr.IsIPv4() && IsReachable(addr) && addr.IsRoutable());
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

/**
 * Update the most recent block where this masternode received a payment.
 * Scan the blockchain backward from a given point, looking for the most recent block where this masternode got paid.
 * Update the masternode's last paid information when it finds such a block.
 * 
 * \param pindex - the block to start scanning from
 * \param nMaxBlocksToScanBack - the maximum number of blocks to scan back
 */
void CMasternode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if (!pindex)
        return;

    const CBlockIndex *BlockReading = pindex;

    const CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    LogFnPrint("masternode", "searching for block with payment to %s", GetDesc());

    if (masterNodeCtrl.masternodePayments.SearchForPaymentBlock(m_nBlockLastPaid, nTimeLastPaid,
        pindex, nMaxBlocksToScanBack, mnpayee))
    {
        LogFnPrint("masternode", "searching for block with payment to %s -- found new %d",
            GetDesc(), m_nBlockLastPaid);
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
    m_collateralMinConfBlockHash = from.m_collateralMinConfBlockHash;
    m_nBlockLastPaid = from.m_nBlockLastPaid;
    m_nPoSeBanScore.store(from.m_nPoSeBanScore.load());
    m_nPoSeBanHeight.store(from.m_nPoSeBanHeight.load());
    fUnitTest = from.fUnitTest;
    m_nMNFeePerMB = from.m_nMNFeePerMB;
    m_nTicketChainStorageFeePerKB = from.m_nTicketChainStorageFeePerKB;
    m_nSenseComputeFee = from.m_nSenseComputeFee;
    m_nSenseProcessingFeePerMB = from.m_nSenseProcessingFeePerMB;
    return *this;
}

CAmount CMasternode::GetMNFee(const MN_FEE mnFee) const noexcept
{
    CAmount nFee = 0;
    switch (mnFee)
	{
        case MN_FEE::StorageFeePerMB:
            nFee = m_nMNFeePerMB == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFee) : m_nMNFeePerMB;
            break;

        case MN_FEE::TicketChainStorageFeePerKB:
            nFee = m_nTicketChainStorageFeePerKB == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFee) : m_nTicketChainStorageFeePerKB;
            break;

        case MN_FEE::SenseComputeFee:
            nFee = m_nSenseComputeFee == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFee) : m_nSenseComputeFee;
            break;

        case MN_FEE::SenseProcessingFeePerMB:
            nFee = m_nSenseProcessingFeePerMB == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFee) : m_nSenseProcessingFeePerMB;
            break;

        default:
            break;
    }
    return nFee;
}

void CMasternode::SetMNFee(const MN_FEE mnFee, const CAmount nNewFee) noexcept
{
    switch (mnFee)
    {
        case MN_FEE::StorageFeePerMB:
            m_nMNFeePerMB = nNewFee;
            break;

        case MN_FEE::TicketChainStorageFeePerKB:
            m_nTicketChainStorageFeePerKB = nNewFee;
            break;

        case MN_FEE::SenseComputeFee:
            m_nSenseComputeFee = nNewFee;
            break;

        case MN_FEE::SenseProcessingFeePerMB:
            m_nSenseProcessingFeePerMB = nNewFee;
            break;

        default:
            break;
    }
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
        LogFnPrint("masternode", "Failed to aquire lock, addr=%s", m_addr.ToString());
        return false;
    }
    
    int nHeight;
    collateralStatus = CheckCollateral(m_vin.prevout, nHeight);
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
    m_collateralMinConfBlockHash = chainActive[nHeight + masterNodeCtrl.nMasternodeMinimumConfirmations-1]->GetBlockHash();
    LogFnPrintf("Masternode UTXO CollateralMinConfBlockHash is [%s], masternode=%s",
            m_collateralMinConfBlockHash.ToString(), GetDesc());
    
    LogFnPrint("masternode", "Masternode UTXO verified");
    return true;
}

string GetListOfMasterNodes(const vector<CMasternode>& mnList)
{
    string s;
    s.reserve(mnList.size() * 75);
    for (const auto& mn : mnList)
        str_append_field(s, mn.GetDesc().c_str(), ", ");
	return s;
}

#ifdef ENABLE_WALLET
// initialize from MN configuration entry
bool CMasternodeBroadcast::InitFromConfig(string &error, const CMasternodeConfig::CMasternodeEntry& mne, const bool bOffline)
{
    string strService = mne.getIp();

    bool bRet = false;
    do
    {
        //need correct blocks to send ping
        if (!bOffline && !masterNodeCtrl.masternodeSync.IsBlockchainSynced())
        {
            error = "Sync in progress. Must wait until sync is complete to start Masternode";
            break;
        }

        COutPoint outpoint;
        CPubKey pubKeyCollateralAddressNew, pubKeyMasternodeNew;
        CKey keyCollateralAddressNew, keyMasternodeNew;
        if (!CMessageSigner::GetKeysFromSecret(mne.getPrivKey(), keyMasternodeNew, pubKeyMasternodeNew))
        {
            error = strprintf("Invalid masternode key %s", mne.getPrivKey());
            break;
        }

        if (!GetMasternodeOutpointAndKeys(pwalletMain, error, outpoint, pubKeyCollateralAddressNew, keyCollateralAddressNew, mne.getTxHash(), mne.getOutputIndex()))
        {
            error = strprintf("Could not allocate outpoint %s-%s for masternode %s. %s", mne.getTxHash(), mne.getOutputIndex(), strService, error);
            break;
        }

        const int nOutpointConfirmations = GetUTXOConfirmations(outpoint);
        if (nOutpointConfirmations < 0 || (static_cast<uint32_t>(nOutpointConfirmations) < masterNodeCtrl.nMasternodeMinimumConfirmations))
        {
            error = strprintf("Masternode UTXO must have at least %u confirmations", masterNodeCtrl.nMasternodeMinimumConfirmations);
            if (nOutpointConfirmations >= 0)
                error += strprintf(", has only %d", nOutpointConfirmations);
            break;
        }

        CService addr;
        if (!Lookup(strService.c_str(), addr, 0, false))
        {
            error = strprintf("Invalid address %s for masternode.", strService);
            break;
        }

        const auto& chainParams = Params();
        if (chainParams.IsMainNet())
        {
            if (addr.GetPort() != MAINNET_DEFAULT_PORT)
            {
                error = strprintf("Invalid port %u for masternode %s, only %hu is supported on mainnet.", addr.GetPort(), strService, MAINNET_DEFAULT_PORT);
                break;
            }
        }
        else if (addr.GetPort() == MAINNET_DEFAULT_PORT)
        {
            error = strprintf("Invalid port %u for masternode %s, %hu is the only supported on mainnet.", addr.GetPort(), strService, MAINNET_DEFAULT_PORT);
            break;
        }

        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
        {
            error = strprintf("Cannot initialize MasterNode broadcast message - %s", fImporting ? "importing blocks" : "reindexing blocks");
            break;
        }

        KeyIO keyIO(chainParams);
        const CTxDestination dest = pubKeyCollateralAddressNew.GetID();
        const string address = keyIO.EncodeDestination(dest);

        LogFnPrint("masternode", "pubKeyCollateralAddressNew = %s, pubKeyMasternodeNew.GetID() = %s",
                 address, pubKeyMasternodeNew.GetID().ToString());

        CMasterNodePing mnp(outpoint);
        if (!mnp.Sign(keyMasternodeNew, pubKeyMasternodeNew))
        {
            error = strprintf("Failed to sign ping, masternode=%s", outpoint.ToStringShort());
            break;
        }

        m_addr = addr;
        m_vin.prevout = outpoint;
        pubKeyCollateralAddress = pubKeyCollateralAddressNew;
        pubKeyMasternode = pubKeyMasternodeNew;
        strExtraLayerAddress = mne.getExtIp();
        strExtraLayerP2P = mne.getExtP2P();
        strExtraLayerCfg = mne.getExtCfg();
        nProtocolVersion = PROTOCOL_VERSION;

        if (!IsValidNetAddr())
        {
            error = strprintf("Invalid IP address, masternode=%s", outpoint.ToStringShort());
            break;
        }

        setLastPing(mnp);
        if (!Sign(keyCollateralAddressNew))
        {
            error = strprintf("Failed to sign broadcast, masternode=%s", outpoint.ToStringShort());
            break;
        }

        // MNID is not registered on first run, will be checked later on
        if (!CheckAndUpdateMNID(error))
            m_ActiveState = MASTERNODE_STATE::PRE_ENABLED;

        bRet = true;
    } while (false);
    if (!error.empty())
        LogFnPrintf("%s", error);
    return bRet;
}
#endif // ENABLE_WALLET

bool CMasternodeBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    // make sure addr is valid
    if (!IsValidNetAddr())
    {
        LogFnPrintf("Invalid addr, rejected: masternode=%s  addr=%s", GetDesc(), m_addr.ToString());
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
    if (!IsLastPingDefined() || !CheckLastPing(nDos))
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

    if (!m_vin.scriptSig.empty())
    {
        LogFnPrintf("Ignore Not Empty ScriptSig %s", m_vin.ToString());
        nDos = 100;
        return false;
    }

    if (Params().IsMainNet())
    {
        if (m_addr.GetPort() != MAINNET_DEFAULT_PORT)
            return false;
    } else if (m_addr.GetPort() == MAINNET_DEFAULT_PORT)
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
                      sigTime, pmn->sigTime, GetDesc(), m_addr.ToString());
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

    // if there was no masternode broadcast recently or if it matches our Masternode privkey...
    if (!pmn->IsBroadcastedWithin(masterNodeCtrl.MasternodeMinMNBSeconds) || 
        (masterNodeCtrl.IsMasterNode() && pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode))
    {
        // take the newest entry
        LogFnPrintf("Got UPDATED Masternode %s entry: addr=%s", pmn->GetDesc(), m_addr.ToString());
        if (pmn->UpdateFromNewBroadcast(*this))
        {
            pmn->Check();
            Relay();
        }
        masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__);
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
    if (masterNodeCtrl.IsMasterNode() && m_vin.prevout == masterNodeCtrl.activeMasternode.outpoint && pubKeyMasternode == masterNodeCtrl.activeMasternode.pubKeyMasternode)
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
    GetTransaction(m_vin.prevout.hash, tx2, m_chainparams.GetConsensus(), hashBlock, true);
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
                     sigTime, masterNodeCtrl.nMasternodeMinimumConfirmations, pConfIndex->GetBlockTime(), GetDesc(), m_addr.ToString());
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

    strMessage = m_addr.ToString(false) + to_string(sigTime) +
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
    string strMessage = m_addr.ToString(false) + to_string(sigTime) +
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

void CMasternodeBroadcast::Relay() const
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

