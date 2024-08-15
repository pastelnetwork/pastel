// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <config/port_config.h>
#include <utils/enum_util.h>
#include <utils/base58.h>
#include <utils/util.h>
#include <init.h>
#include <netbase.h>
#include <key_io.h>
#include <script/standard.h>
#include <main.h>

#include <mining/mining-settings.h>
#include <mnode/mnode-active.h>
#include <mnode/mnode-masternode.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-manager.h>
#include <mnode/mnode-msgsigner.h>
#include <mnode/mnode-sync.h>
#include <mnode/mnode-validation.h>
#include <mnode/mnode-controller.h>
#include <mnode/tickets/pastelid-reg.h>
#include <netmsg/nodemanager.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

using namespace std;

constexpr array<MNStateInfo, to_integral_type<MASTERNODE_STATE>(MASTERNODE_STATE::COUNT)> MN_STATE_INFO =
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
CMasterNodePing::CMasterNodePing() noexcept
{
    m_bDefined = false;
}

CMasterNodePing& CMasterNodePing::operator=(const CMasterNodePing& mnp)
{
    if (this == &mnp)
		return *this;
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
    if (!chainActive.Tip() || chainActive.Height() < MN_PING_HEIGHT_OFFSET)
        return;

    m_vin = CTxIn(outpoint);
    m_blockHash = chainActive[chainActive.Height() - MN_PING_HEIGHT_OFFSET]->GetBlockHash();
    m_sigTime = GetAdjustedTime();
    m_bDefined = true;
}

string CMasterNodePing::getMessage() const noexcept
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
        LogFnPrintf("Got bad Masternode ping signature, masternode='%s', error: %s", GetDesc(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

CMasterNodePing::MNP_CHECK_RESULT CMasterNodePing::SimpleCheck(int& nDos) const noexcept
{
    // don't ban by default
    nDos = 0;

    if (m_sigTime > GetAdjustedTime() + 60 * 60)
    {
        LogFnPrintf("Signature rejected, too far into the future, masternode='%s'", GetDesc());
        nDos = 1;
        return MNP_CHECK_RESULT::SIGNED_IN_FUTURE;
    }

    uint32_t nBlockHeight = 0;
    {
        AssertLockHeld(cs_main);

        const auto mi = mapBlockIndex.find(m_blockHash);
        if (mi == mapBlockIndex.cend())
        {
            LogFnPrint("masternode", "Unknown block hash in masternode ping: masternode='%s' blockHash=%s", 
                GetDesc(), m_blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return MNP_CHECK_RESULT::UNKNOWN_BLOCK_HASH;
        }

        const auto pindex = mi->second;
        if (!pindex)
        {
			LogFnPrint("masternode", "Invalid block index: masternode='%s' blockHash=%s", 
                				GetDesc(), m_blockHash.ToString());
			return MNP_CHECK_RESULT::INVALID_BLOCK_INDEX;
		}

        nBlockHeight = pindex->GetHeight();
        // Check ping expiration by block height (should be within last 24 (MN_PING_HEIGHT_EXPIRATION) blocks).
        if (nBlockHeight < gl_nChainHeight - MN_PING_HEIGHT_EXPIRATION)
        {
            if (m_nExpiredErrorCount % 20 == 0)
				LogFnPrintf("Masternode '%s' ping is outdated, block hash (%s, height=%u) is older than %u blocks (%u)",
					GetDesc(), m_blockHash.ToString(), nBlockHeight, MN_PING_HEIGHT_EXPIRATION, gl_nChainHeight.load());
            return MNP_CHECK_RESULT::EXPIRED_BY_HEIGHT;
        }
    }
    LogFnPrint("masternode", "Masternode ping verified: masternode='%s'  blockHash='%s' (height=%u)  sigTime=%" PRId64, 
        GetDesc(), m_blockHash.ToString(), nBlockHeight, m_sigTime);
    return MNP_CHECK_RESULT::OK;
}

int64_t CMasterNodePing::getAgeInSecs() const noexcept
{
	return GetAdjustedTime() - m_sigTime;
}

bool CMasterNodePing::IsExpired() const noexcept
{
    return getAgeInSecs() > masterNodeCtrl.MasternodeNewStartRequiredSeconds;
}

void CMasterNodePing::Relay() const
{
    const uint256 hash = GetHash();
    // Do not relay until fully synced
    if (!masterNodeCtrl.IsSynced())
    {
        LogFnPrint("masternode", "won't relay ping '%s' until fully synced", hash.ToString());
        masterNodeCtrl.masternodeManager.ScheduleMnpForRelay(hash, getOutPoint());
        return;
    }

    LogFnPrint("masternode", "Relaying ping '%s' for masternode '%s'", hash.ToString(), GetDesc());
    CInv inv(MSG_MASTERNODE_PING, hash);
    gl_NodeManager.RelayInv(inv);
}

void CMasterNodePing::HandleCheckResult(const MNP_CHECK_RESULT result)
{
    if (result == MNP_CHECK_RESULT::EXPIRED_BY_HEIGHT)
        ++m_nExpiredErrorCount;
    else
        m_nExpiredErrorCount = 0;
}

//
//  ----------------- masternode_info_t  ------------------------------------------------------------------------------
//
masternode_info_t::masternode_info_t(const MASTERNODE_STATE activeState, const int protoVer, const int64_t sTime,
                    const COutPoint &outpoint, const CService &addr,
                    const CPubKey &pkCollAddr, const CPubKey &pkMN,
                    const string& extAddress, const string& extP2P, const string& extCfg,
                    int64_t tWatchdogV, const bool bIsEligibleForMining) noexcept :
    m_ActiveState{activeState}, 
    nProtocolVersion{protoVer}, 
    sigTime{sTime},
    m_vin(outpoint),
    m_addr{addr},
    pubKeyCollateralAddress{pkCollAddr}, pubKeyMasternode{pkMN},
    strExtraLayerAddress{extAddress}, strExtraLayerP2P{extP2P}, 
    strExtraLayerCfg{extCfg},
    nTimeLastWatchdogVote{tWatchdogV},
    m_bEligibleForMining{bIsEligibleForMining}
{}

/**
* Set new MasterNode's state.
* 
* \param newState - new MasterNode's state
* \param szMethodName - optional function name to trace MN's state changes
* \param szReason - optional reason for state change
*/
void masternode_info_t::SetState(const MASTERNODE_STATE newState, const char *szMethodName, const char *szReason)
{
    if (m_ActiveState == newState)
        return;
    MASTERNODE_STATE prevState = m_ActiveState;
    m_ActiveState = newState;
    if (!LogAcceptCategory("masternode"))
        return;
    string sMsg;
    if (szMethodName && *szMethodName)
        sMsg = strprintf("[%s] -- ", szMethodName);
    sMsg += strprintf("Masternode '%s' has changed state [%s] -> [%s]",
        GetDesc(), MasternodeStateToString(prevState), GetStateString());
    if (szReason && *szReason)
        sMsg += strprintf(" (%s)", szReason);
    LogPrintf("%s\n", sMsg);
}

bool CMasternode::fCompatibilityReadMode = false;

//
//  ----------------- CMasternode  ------------------------------------------------------------------------------
//
CMasternode::CMasternode() noexcept: 
    masternode_info_t{ MASTERNODE_STATE::ENABLED, PROTOCOL_VERSION, GetAdjustedTime()},
    m_chainparams(Params())
{
    m_nPoSeBanScore.store(0);
}

CMasternode::CMasternode(const CMasternode& other) noexcept :
    masternode_info_t{other},
    m_chainparams(Params()),
    vchSig(other.vchSig),
    m_collateralMinConfBlockHash(other.m_collateralMinConfBlockHash),
    m_nBlockLastPaid(other.m_nBlockLastPaid),
    fUnitTest(other.fUnitTest),
    m_nMNFeePerMB(other.m_nMNFeePerMB),
    m_nTicketChainStorageFeePerKB(other.m_nTicketChainStorageFeePerKB),
    m_nSenseComputeFee(other.m_nSenseComputeFee),
    m_nSenseProcessingFeePerMB(other.m_nSenseProcessingFeePerMB),
    m_nVersion(other.m_nVersion)
{
    setLastPing(other.getLastPing());
    m_nPoSeBanScore.store(other.m_nPoSeBanScore.load());
    m_nPoSeBanHeight.store(other.m_nPoSeBanHeight.load());
}

CMasternode::CMasternode(const CMasternodeBroadcast& mnb) :
    masternode_info_t{ mnb.GetActiveState(), mnb.nProtocolVersion, mnb.sigTime,
                       mnb.m_vin.prevout, mnb.get_addr(), mnb.pubKeyCollateralAddress, mnb.pubKeyMasternode,
                       mnb.strExtraLayerAddress, mnb.strExtraLayerP2P, mnb.strExtraLayerCfg,
                       mnb.sigTime, mnb.IsEligibleForMining()},
    m_chainparams(Params()),
    vchSig(mnb.vchSig)
{
    m_nPoSeBanScore = 0;
    m_nVersion = mnb.GetVersion();
    setLastPing(mnb.getLastPing());
}

/**
 * MasterNode hash, includes only:
 *  - vin (input collateral transaction)
 *  - public key for collateral address
 *  - signature time
 * 
 * \return - MasterNode hash
 */
uint256 CMasternode::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << m_vin;
    ss << pubKeyCollateralAddress;
    ss << sigTime;
    return ss.GetHash();
}

bool CMasternode::NeedUpdateFromBroadcast(const CMasternodeBroadcast& mnb) const noexcept
{
    // check for version downgrade
    if (m_nVersion > mnb.GetVersion())
    {
		LogFnPrint("masternode", "masternode '%s' seen v%d, ignoring mnb v%hd", mnb.GetDesc(), m_nVersion, mnb.GetVersion());
        return false; 
    }
    if (mnb.GetVersion() < CMasternode::MASTERNODE_VERSION)
    {
	    LogFnPrint("masternode", "masternode '%s', received mnb v%hd with partial info, expecting mnb v%hd",
            mnb.GetDesc(), mnb.GetVersion(), CMasternode::MASTERNODE_VERSION);
        return false;
	}
    // update only to the higher or equal version of mnb
    if (m_nVersion < mnb.GetVersion())
        return true; // need to upgrade version
    // same version, check sigTime
    if (mnb.sigTime < sigTime)
    {
        LogFnPrint("masternode", "masternode '%s' seen sigTime=%" PRId64 ", ignoring mnb sigTime=%" PRId64, 
            			mnb.GetDesc(), sigTime, mnb.sigTime);
        return false; // got older mnb - ignore
    }
	return true;
}

//
// When a new masternode broadcast is sent, update our information
//
bool CMasternode::UpdateFromNewBroadcast(const CMasternodeBroadcast& mnb)
{
    if (mnb.sigTime <= sigTime && !mnb.fRecovery)
    {
        LogFnPrint("masternode", "masternode '%s' skip update from mnb", mnb.GetDesc());
        return false;
    }

    // disable version downgrade
    if (mnb.GetVersion() < m_nVersion)
    {
        LogFnPrint("masternode", "masternode '%s' ignoring mnb v%hd", mnb.GetDesc(), mnb.GetVersion());
        return false;
    }
    pubKeyMasternode = mnb.pubKeyMasternode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    m_addr = mnb.m_addr;
    strExtraLayerAddress = mnb.strExtraLayerAddress;
    strExtraLayerP2P = mnb.strExtraLayerP2P;
    strExtraLayerCfg = mnb.strExtraLayerCfg;
    if (mnb.GetVersion() >= 2)
    {
        const bool bEligibleForMining = m_bEligibleForMining;
        SetEligibleForMining(mnb.IsEligibleForMining());
        if (bEligibleForMining != mnb.IsEligibleForMining())
            LogFnPrint("masternode", "eligibleForMining=%d", mnb.IsEligibleForMining());
    }
    if (mnb.GetVersion() >= 1)
    {
        m_nMNFeePerMB = mnb.m_nMNFeePerMB;
        m_nTicketChainStorageFeePerKB = mnb.m_nTicketChainStorageFeePerKB;
        m_nSenseComputeFee = mnb.m_nSenseComputeFee;
        m_nSenseProcessingFeePerMB = mnb.m_nSenseProcessingFeePerMB;
    }
    m_nVersion = mnb.GetVersion();
    m_nPoSeBanScore = 0;
    m_nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.IsLastPingDefined())
        setLastPingAndCheck(mnb.getLastPing(), true, nDos);
    // if it matches our Masternode public key...
    if (masterNodeCtrl.IsOurMasterNode(pubKeyMasternode))
    {
        PoSeUnBan();
        if (nProtocolVersion == PROTOCOL_VERSION)
        {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            masterNodeCtrl.activeMasternode.ManageState(__FUNCTION__);
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
// Requires cs_main lock
arith_uint256 CMasternode::CalculateScore(const uint256& blockHash)
{
    if (m_collateralMinConfBlockHash.IsNull())
    {
        LogFnPrint("masternode", "Masternode '%s' has nCollateralMinConfBlockHash NOT set, will try to set it now", GetDesc());
        CollateralStatus collateralStatus = CollateralStatus::OK;
        VerifyCollateral(collateralStatus, m_collateralMinConfBlockHash);
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
        error = strprintf("Masternode '%s' does not have registered Pastel ID", GetDesc());
        return false;
    }

    // set MN Pastel ID which is registered using collateral transaction (txid-index)
    m_sMNPastelID = mnidTicket.getPastelID();
    if (m_sMNPastelID.empty())
    {
        error = strprintf("Masternode '%s' has empty registered Pastel ID", GetDesc());
        return false;
    }
    return true;
}

/**
 * Check & update Masternode's state.
 * 
 * \param fForce - force update
 * \param bLockMain - if true, try to lock cs_main
 */
void CMasternode::Check(const bool fForce, const bool bLockMain)
{
    LOCK(cs_mn);

    if (IsShutdownRequested())
        return;

    // check masternodes every 5 secs (MasternodeCheckSeconds) or in forced mode
    if (!fForce && (GetTime() - nTimeLastChecked < masterNodeCtrl.MasternodeCheckSeconds))
        return;
    nTimeLastChecked = GetTime();

    // once MN outpoint is spent, stop doing the checks
    if (IsOutpointSpent())
        return;

    if (!fUnitTest)
    {
        TRY_LOCK_COND(bLockMain, cs_main, lockMain);
        if (bLockMain && !lockMain)
            return;

        CollateralStatus err = CheckCollateral(m_vin.prevout);
        if (err == CollateralStatus::UTXO_NOT_FOUND)
        {
            LogFnPrint("masternode", "Failed to find Masternode UTXO, masternode=%s", GetDesc());
            SetState(MASTERNODE_STATE::OUTPOINT_SPENT, __METHOD_NAME__);
            return;
        }
    }

    const uint32_t nCurrentHeight = gl_nChainHeight;
    // PoSe (Proof of Service) ban score feature
    if (IsPoSeBanned())
    {
        // MN is banned till nPoSeBanHeight
        if (nCurrentHeight < m_nPoSeBanHeight)
            return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Masternode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogFnPrintf("Masternode '%s' is unbanned and back in list now", GetDesc());
        DecrementPoSeBanScore();
    } else
        if (IsPoSeBannedByScore())
        {
            // ban for the whole payment cycle
            m_nPoSeBanHeight = nCurrentHeight + static_cast<uint32_t>(masterNodeCtrl.masternodeManager.size());
            LogFnPrintf("Masternode '%s' is banned till block %u now", GetDesc(), m_nPoSeBanHeight.load());
            // change MN's state to POSE_BAN
            SetState(MASTERNODE_STATE::POSE_BAN, __METHOD_NAME__, strprintf("banned by score till block %u", m_nPoSeBanHeight.load()).c_str());
            return;
        }

    const bool fOurMasterNode = masterNodeCtrl.IsOurMasterNode(pubKeyMasternode);

    // change status to UPDATE_REQUIRED if:
    //   - masternode doesn't meet min protocol requirements for the current epoch
    const int nSupportedProtocolVersion = masterNodeCtrl.GetSupportedProtocolVersion();
    if (nProtocolVersion < nSupportedProtocolVersion)
    {
        SetState(MASTERNODE_STATE::UPDATE_REQUIRED, __METHOD_NAME__,
            strprintf("protocol version %d is less than required %d", nProtocolVersion, nSupportedProtocolVersion).c_str());
        return;
    }

    LogFnPrint("masternode", "outpoint='%s' | %s | last broadcast %" PRId64 " secs ago (v%hu) | last ping %s",
        GetDesc(), MasternodeStateToString(m_ActiveState), GetLastBroadcastAge(), GetVersion(),
        IsLastPingDefined() ? strprintf("%" PRId64 " secs ago", getLastPing().getAgeInSecs()) : "not received yet");

    string sReason;
    // keep old masternodes on start, give them a chance to receive updates...
    const bool fWaitingForPing = !masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && !IsPingedWithin(masterNodeCtrl.MasternodeMinMNPSeconds, -1, &sReason);

    if (fWaitingForPing && !fOurMasterNode)
    {   // if we are not running in masternode mode and we are waiting for ping packet for that masternode...
        // ...but if it was already expired before the initial check - return right away
        if (IsExpired() || IsWatchdogExpired() || IsNewStartRequired())
        {
            LogFnPrint("masternode", "Masternode '%s' is in %s state, waiting for ping (%s)", 
                GetDesc(), GetStateString(), sReason);
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own masternode
    if (!fWaitingForPing || fOurMasterNode)
    {
        // set status to NEW_START_REQUIRED if we didn't receive ping for more than MasternodeNewStartRequiredSeconds (180 mins)
        if (!IsPingedWithin(masterNodeCtrl.MasternodeNewStartRequiredSeconds, -1, &sReason))
        {
            SetState(MASTERNODE_STATE::NEW_START_REQUIRED, __METHOD_NAME__, sReason.c_str());
            return;
        }

        const bool fWatchdogActive = masterNodeCtrl.IsSynced() && masterNodeCtrl.masternodeManager.IsWatchdogActive();
        const bool fWatchdogExpired = fWatchdogActive && ((GetAdjustedTime() - nTimeLastWatchdogVote) > masterNodeCtrl.MasternodeWatchdogMaxSeconds);

        if (fWatchdogExpired)
        {
            LogFnPrint("masternode", "outpoint='%s' | %s | nTimeLastWatchdogVote=%d, fWatchdogExpired=%d",
                GetDesc(), MasternodeStateToString(m_ActiveState), nTimeLastWatchdogVote, fWatchdogExpired);
            SetState(MASTERNODE_STATE::WATCHDOG_EXPIRED, __METHOD_NAME__);
            return;
        }

        // do not set state to EXPIRED if we're active masternode but not yet registered MNID
        if (!IsPingedWithin(masterNodeCtrl.MasternodeExpirationSeconds, -1, &sReason) &&
            (!fOurMasterNode || !masterNodeCtrl.activeMasternode.NeedMnId()))
        {
            SetState(MASTERNODE_STATE::EXPIRED, __METHOD_NAME__, sReason.c_str());
            return;
        }
    }

    // if ping was received less than MasternodeMinMNPSeconds (10 mins) since last broadcast - can't enable MN
    // unless it's already in ENABLED state
    if (m_lastPing.IsPingedWithin(masterNodeCtrl.MasternodeMinMNPSeconds, sigTime))
    {
        sReason = strprintf("last ping received %" PRId64 " secs ago", m_lastPing.getAgeInSecs());
        SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__, sReason.c_str());
        return;
    }

    // check that this MN has registered Pastel ID (mnid) - sets m_sMNPastelID if successfull
    string error;
    if (!CheckAndUpdateMNID(error))
    {
        LogFnPrint("masternode", "%s", error);
        SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__, "no registered mnid");
        return;
    }
    if (fOurMasterNode)
    {
        // if we're running in MasterNode mode - check that MNID actually exists locally
        const auto mapIDs = CPastelID::GetStoredPastelIDs(true, m_sMNPastelID);
        if (mapIDs.empty())
        {
            error = strprintf("Masternode '%s' registered Pastel ID '%s' is not stored locally", GetDesc(), m_sMNPastelID);
            LogFnPrint("masternode", error);
            SetState(MASTERNODE_STATE::PRE_ENABLED, __METHOD_NAME__, "mnid is not stored locally");
            return;
        }
    }

    SetState(MASTERNODE_STATE::ENABLED, __METHOD_NAME__); // OK
}

bool CMasternode::IsInputAssociatedWithPubkey() const
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

void CMasternode::setLastPing(const CMasterNodePing& lastPing) noexcept
{
    m_lastPing = lastPing;
}

bool CMasternode::setLastPingAndCheck(const CMasterNodePing& lastPing, const bool bSkipEarlyPingCheck, int& nDos) noexcept
{
    nDos = 0;
    if (!lastPing.IsDefined())
		return false; // just ignore ping if it's not defined

    const auto hashPing = lastPing.GetHash();
    if (lastPing.IsExpired())
    {
		LogFnPrint("masternode", "Masternode '%s' ping '%s' is expired (%" PRId64 " secs old)", 
            GetDesc(), hashPing.ToString(), lastPing.getAgeInSecs());
        masterNodeCtrl.masternodeManager.EraseSeenMnp(hashPing); // make sure it is not in the seen mnp cache
		return false;
	}
    if (m_lastPing.IsDefined())
    {
        if (!lastPing.IsPingedAfter(m_lastPing.getSigTime()))
        {
            LogFnPrint("masternode", "Masternode '%s' ping '%s' is older than the last one (%" PRId64 " secs)",
                GetDesc(), hashPing.ToString(), lastPing.getAgeInSecs());
            return false;
        }
        const auto existingHashPing = m_lastPing.GetHash();
        if (hashPing == existingHashPing)
			return false;
    }
    const auto mnpCheckResult = lastPing.SimpleCheck(nDos);
    m_lastPing.HandleCheckResult(mnpCheckResult);
    if (mnpCheckResult != CMasterNodePing::MNP_CHECK_RESULT::OK)
        return false;

    string sLastPingInfo;
    if (IsLastPingDefined())
		sLastPingInfo = strprintf("last ping received %" PRId64 " secs ago", m_lastPing.getAgeInSecs());
    else
        sLastPingInfo = "no known last ping";
    LogFnPrint("masternode", "New ping: masternode '%s', blockHash=%s, sigTime=%" PRId64 ", %s",
        GetDesc(), lastPing.getBlockHashString(), lastPing.getSigTime(), sLastPingInfo);

    // for ENABLED masternode we can ignore this new ping if it came too early
    if (IsEnabled())
    {
        // update only if there is no known ping or
        // last ping was more then (masterNodeCtrl.MasternodeMinMNPSeconds - 60) (9 mins) ago compare to this one
        if (!bSkipEarlyPingCheck && IsPingedWithin(masterNodeCtrl.MasternodeMinMNPSeconds - 60, lastPing.getSigTime()))
        {
			LogFnPrint("masternode", "Masternode '%s' ping arrived too early", GetDesc());
			return false;
		}
    }

    if (!lastPing.CheckSignature(pubKeyMasternode, nDos))
        return false;

    // if we are still syncing and there was no known ping for this masternode for quite a while
    // (NOTE: assuming that masterNodeCtrl.MasternodeExpirationSeconds/2 (30+ mins) should be enough to finish mn list sync)
    if (!masterNodeCtrl.masternodeSync.IsMasternodeListSynced() && !IsPingedWithin(masterNodeCtrl.MasternodeExpirationSeconds/2))
    {
        // let's bump sync timeout
        LogFnPrint("masternode", "Masternode '%s' ping sync timeout", GetDesc());
        masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__);
    }

    const auto hash = GetHash();

    // ping looks good, store it as the last one
    LogFnPrint("masternode", "Masternode '%s' ping '%s' accepted", GetDesc(), hashPing.ToString());
    setLastPing(lastPing);

    masterNodeCtrl.masternodeManager.UpdateMnpAndMnb(hash, hashPing, m_lastPing);

    // force update, ignoring cache
    Check(true);
    // relay ping for nodes in ENABLED/EXPIRED/WATCHDOG_EXPIRED state only, skip everyone else
    if (IsEnabled() || IsExpired() || IsWatchdogExpired())
        m_lastPing.Relay();

    return true;
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
    LOCK(cs_mn);
    nTimeLastWatchdogVote = (nVoteTime == 0) ? GetAdjustedTime() : nVoteTime;
}

bool CMasternode::IsPingedWithin(const int nSeconds, int64_t nTimeToCheckAt, string *psReason) const noexcept
{
    if (!m_lastPing.IsDefined())
    {
        if (psReason)
            *psReason = "no ping received yet";
        return false;
    }
    if (psReason)
        psReason->clear();
    if (nTimeToCheckAt == -1)
        nTimeToCheckAt = GetAdjustedTime();
    const bool bIsPingedWithin = m_lastPing.IsPingedWithin(nSeconds, nTimeToCheckAt);
    if (!bIsPingedWithin && psReason)
    {
        string sTimeLog = strprintf("current adjusted time %" PRId64 ", sigtime %" PRId64 ", time offset %" PRId64,
            nTimeToCheckAt, m_lastPing.getSigTime(), GetTimeOffset());
        int64_t nLastPingAge = 0;
        if (nTimeToCheckAt >= m_lastPing.getSigTime())
        {
            const int64_t nLastPingAge = nTimeToCheckAt - m_lastPing.getSigTime();
            *psReason = strprintf("last ping was received %" PRId64 " seconds ago, %s", nLastPingAge, sTimeLog);
        } 
        else
        {
            const int64_t nLastPingAge = m_lastPing.getSigTime() - nTimeToCheckAt;
			*psReason = strprintf("last ping receive time (%d seconds) is in the future, %s", nLastPingAge, sTimeLog);
        }
    }
    return bIsPingedWithin;
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

CAmount CMasternode::GetMNFeeInPSL(const MN_FEE mnFeeType) const noexcept
{
    CAmount nFee = 0;
    switch (mnFeeType)
	{
        case MN_FEE::StorageFeePerMB:
            nFee = m_nMNFeePerMB == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFeeType) : m_nMNFeePerMB;
            break;

        case MN_FEE::TicketChainStorageFeePerKB:
            nFee = m_nTicketChainStorageFeePerKB == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFeeType) : m_nTicketChainStorageFeePerKB;
            break;

        case MN_FEE::SenseComputeFee:
            nFee = m_nSenseComputeFee == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFeeType) : m_nSenseComputeFee;
            break;

        case MN_FEE::SenseProcessingFeePerMB:
            nFee = m_nSenseProcessingFeePerMB == 0 ? masterNodeCtrl.GetDefaultMNFee(mnFeeType) : m_nSenseProcessingFeePerMB;
            break;

        default:
            break;
    }
    if (nFee < DEFAULT_MIN_MN_FEE_PSL)
		nFee = DEFAULT_MIN_MN_FEE_PSL;
    return nFee;
}

void CMasternode::SetMNFeeInPSL(const MN_FEE mnFeeType, const CAmount nNewFeeInPSL) noexcept
{
    switch (mnFeeType)
    {
        case MN_FEE::StorageFeePerMB:
            m_nMNFeePerMB = nNewFeeInPSL;
            break;

        case MN_FEE::TicketChainStorageFeePerKB:
            m_nTicketChainStorageFeePerKB = nNewFeeInPSL;
            break;

        case MN_FEE::SenseComputeFee:
            m_nSenseComputeFee = nNewFeeInPSL;
            break;

        case MN_FEE::SenseProcessingFeePerMB:
            m_nSenseProcessingFeePerMB = nNewFeeInPSL;
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

bool CMasternode::VerifyCollateral(CollateralStatus& collateralStatus, uint256 &collateralMinConfBlockHash) const
{
    AssertLockHeld(cs_main);
    
    int nHeight;
    collateralStatus = CheckCollateral(m_vin.prevout, nHeight);
    if (collateralStatus == CollateralStatus::UTXO_NOT_FOUND)
    {
        LogFnPrint("masternode", "Failed to find Masternode UTXO, masternode='%s'", GetDesc());
        return false;
    }
    
    if (collateralStatus == CollateralStatus::INVALID_AMOUNT)
    {
        LogFnPrint("masternode", "Masternode UTXO should have %d %s, masternode='%s'",
                 masterNodeCtrl.MasternodeCollateral, CURRENCY_UNIT, GetDesc());
        return false;
    }
    
    const int nConfirmations = chainActive.Height() - nHeight + 1;
    if (nConfirmations < 0 || static_cast<uint32_t>(nConfirmations) < masterNodeCtrl.nMasternodeMinimumConfirmations)
    {
        LogFnPrintf("Masternode UTXO must have at least %u confirmations, masternode='%s'",
                    masterNodeCtrl.nMasternodeMinimumConfirmations, GetDesc());
        // maybe we miss few blocks, let this mnb to be checked again later
        return false;
    }
    // remember the hash of the block where masternode collateral had minimum required confirmations
    collateralMinConfBlockHash = chainActive[nHeight + masterNodeCtrl.nMasternodeMinimumConfirmations-1]->GetBlockHash();
    LogFnPrintf("Masternode UTXO CollateralMinConfBlockHash is [%s], masternode='%s'",
            collateralMinConfBlockHash.ToString(), GetDesc());
    
    LogFnPrint("masternode", "Masternode UTXO verified");
    return true;
}

string GetListOfMasterNodes(const masternode_vector_t& mnList)
{
    string s;
    s.reserve(mnList.size() * 75);
    for (const auto& pmn : mnList)
    {
        if (pmn)
            str_append_field(s, pmn->GetDesc().c_str(), ", ");
    }
	return s;
}

#ifdef ENABLE_WALLET
/**
 * Initialize masternode broadcast (mnb) from configuration entry.
 *  
 * \param error - error message
 * \param mne - masternode configuration entry
 * \param bOffline - offline mode
 * \return true if success, false otherwise
 */
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
            error = strprintf("Could not allocate outpoint %s-%s for masternode '%s'. %s", mne.getTxHash(), mne.getOutputIndex(), strService, error);
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

        if (m_chainparams.IsMainNet())
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

        KeyIO keyIO(m_chainparams);
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
        else if (!m_sMNPastelID.empty())
            SetEligibleForMining(mne.isEligibleForMining());
        m_nVersion = CMasternode::MASTERNODE_VERSION;

        bRet = true;
    } while (false);
    if (!error.empty())
        LogFnPrintf("%s", error);
    return bRet;
}
#endif // ENABLE_WALLET

bool CMasternodeBroadcast::SimpleCheck(int& nDos, bool &bExpired) const
{
    nDos = 0;
    bExpired = false;

    // make sure addr is valid
    if (!IsValidNetAddr())
    {
        LogFnPrintf("Invalid addr, rejected: masternode='%s'  addr=%s", GetDesc(), m_addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60)
    {
        LogFnPrintf("Signature rejected, too far into the future: masternode='%s'", GetDesc());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if (!IsLastPingDefined() || !CheckLastPing(nDos))
        bExpired = true; // one of us is probably forked or smth, just mark it as expired and check the rest of the rules

    if (nProtocolVersion < masterNodeCtrl.GetSupportedProtocolVersion())
    {
        LogFnPrintf("ignoring outdated Masternode: masternode='%s'  nProtocolVersion=%d", 
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

    if (m_chainparams.IsMainNet())
    {
        if (m_addr.GetPort() != MAINNET_DEFAULT_PORT)
            return false;
    } else if (m_addr.GetPort() == MAINNET_DEFAULT_PORT)
        return false;

    return true;
}

CMasternodeBroadcast::MNB_UPDATE_RESULT CMasternodeBroadcast::Update(string& error, masternode_t &pmn, int& nDos) const
{
    nDos = 0;

    error.clear();
    if (!pmn)
    {
        error = "Masternode not found";
        return MNB_UPDATE_RESULT::NOT_FOUND;
    }
    const bool bVersionUpdate = pmn->GetVersion() < GetVersion();
    const auto& hashMNB = GetHash();
    const bool bHashUpdate = pmn->GetHash() != hashMNB;
    if (pmn->sigTime == sigTime && !fRecovery && !bVersionUpdate && !bHashUpdate)
    {
        // mapSeenMasternodeBroadcast in CMasternodeMan::CheckMnbAndUpdateMasternodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        error = "Duplicate Masternode broadcast";
        return MNB_UPDATE_RESULT::DUPLICATE_MNB;
    }

    // this broadcast is older than the one that we already have
    // this can happen only if we're getting broadcast with the newer version
    if (pmn->sigTime > sigTime)
    {
        LogFnPrintf("Bad sigTime %" PRId64 " (existing broadcast is at %" PRId64 ") for Masternode '%s' %s",
                      sigTime, pmn->sigTime, GetDesc(), m_addr.ToString());
        error = "Masternode broadcast is older than the one that we already have";
        return MNB_UPDATE_RESULT::OLDER;
    }

    pmn->Check(false, SKIP_LOCK);

    // masternode is banned by PoSe
    if (pmn->IsPoSeBanned())
    {
        LogFnPrintf("Banned by PoSe, masternode=%s", GetDesc());
        error = "Masternode is banned by PoSe score";
        return MNB_UPDATE_RESULT::POSE_BANNED;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if (pmn->pubKeyCollateralAddress != pubKeyCollateralAddress)
    {
        LogFnPrintf("Got mismatched pubKeyCollateralAddress");
        error = "Masternode broadcast has mismatched pubKeyCollateralAddress";
        nDos = 33;
        return MNB_UPDATE_RESULT::PUBKEY_MISMATCH;
    }

    if (!CheckSignature(nDos))
    {
        LogFnPrintf("CheckSignature() failed, masternode=%s", GetDesc());
        error = "Masternode broadcast signature is invalid";
        return MNB_UPDATE_RESULT::INVALID_SIGNATURE;
    }

    // if there was no masternode broadcast recently or if it matches our Masternode public key...
    if (!pmn->IsBroadcastedWithin(masterNodeCtrl.MasternodeMinMNBSeconds) || masterNodeCtrl.IsOurMasterNode(pubKeyMasternode))
    {
        // take the newest entry
        LogFnPrintf("Got UPDATED Masternode '%s' entry: addr=%s (v%hd, mnb '%s')", pmn->GetDesc(), 
            m_addr.ToString(), GetVersion(), hashMNB.ToString());
        if (pmn->UpdateFromNewBroadcast(*this))
            pmn->Check(true, SKIP_LOCK);
        masterNodeCtrl.masternodeSync.BumpAssetLastTime(__METHOD_NAME__);
    }

    return MNB_UPDATE_RESULT::SUCCESS;
}

/*
Check collateral tnx in the Anounce message is correct - 
    - it exists
    - it has correct amount
    - there were right number of confirmations (number of blocks after)
    - verify signature
*/
bool CMasternodeBroadcast::CheckOutpoint(int& nDos, uint256 &collateralMinConfBlockHash) const
{
    AssertLockHeld(cs_main);
    // we are a masternode with the same vin (i.e. already activated) and this mnb is ours (matches our Masternode privkey)
    // so nothing to do here for us
    if (masterNodeCtrl.IsOurMasterNode(pubKeyMasternode) && m_vin.prevout == masterNodeCtrl.activeMasternode.outpoint)
        return false;

    if (!CheckSignature(nDos))
    {
        LogFnPrintf("CheckSignature() failed, masternode=%s", GetDesc());
        return false;
    }
    
    CollateralStatus collateralStatus = CollateralStatus::OK;
    if (!VerifyCollateral(collateralStatus, collateralMinConfBlockHash))
    {
        // if error but collateral itself is OK, let this mnb to be checked again later
        if (collateralStatus == CollateralStatus::OK)
            masterNodeCtrl.masternodeManager.EraseSeenMnb(GetHash());
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
                LogFnPrintf("Bad sigTime %" PRId64 " (%u conf block is at %" PRId64 ") for Masternode '%s' %s",
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

bool CMasternodeBroadcast::CheckSignature(int& nDos) const
{
    nDos = 0;
    string strError;
    string strMessage = m_addr.ToString(false) + to_string(sigTime) +
                    pubKeyCollateralAddress.GetID().ToString() + pubKeyMasternode.GetID().ToString() +
                    to_string(nProtocolVersion);

    KeyIO keyIO(m_chainparams);
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
    const uint256 hash = GetHash();
    // Do not relay until fully synced
    if (!masterNodeCtrl.IsSynced())
    {
        LogFnPrint("masternode", "won't relay mnb '%s' until fully synced", hash.ToString());
        masterNodeCtrl.masternodeManager.ScheduleMnbForRelay(hash, getOutPoint());
        return;
    }

    LogFnPrint("masternode", "Relaying mnb '%s' for masternode '%s'", hash.ToString(), GetDesc());
    CInv inv(MSG_MASTERNODE_ANNOUNCE, GetHash());
    gl_NodeManager.RelayInv(inv);
}

// check if pinged after sigTime
bool CMasternodeBroadcast::IsPingedAfter(const int64_t& sigTime) const noexcept
{
    if (!IsLastPingDefined())
        return false;
    return m_lastPing.IsPingedAfter(sigTime);
}

bool CMasternodeBroadcast::IsSamePingTime(const int64_t& sigTime) const noexcept
{
	if (!IsLastPingDefined())
		return false;
	return m_lastPing.getSigTime() == sigTime;
}

void CMasternodeVerification::Relay() const
{
    CInv inv(MSG_MASTERNODE_VERIFY, GetHash());
    gl_NodeManager.RelayInv(inv);
}

