// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <algorithm>
#include <map>
#include <cstdint>

#include <utils/util.h>
#include <alert.h>
#include <clientversion.h>
#include <net.h>
#include <pubkey.h>
#include <timedata.h>
#include <ui_interface.h>

using namespace std;

unordered_map<uint256, CAlert> mapAlerts;
CCriticalSection cs_mapAlerts;

void CUnsignedAlert::SetNull()
{
    nVersion = 1;
    nRelayUntil = 0;
    nExpiration = 0;
    nID = 0;
    nCancel = 0;
    setCancel.clear();
    nMinVer = 0;
    nMaxVer = 0;
    setSubVer.clear();
    nPriority = 0;

    strComment.clear();
    strStatusBar.clear();
    strRPCError.clear();
}

string CUnsignedAlert::ToString() const
{
    string strSetCancel;
    strSetCancel.reserve(setCancel.size() * 3);
    for (const auto & n : setCancel)
        strSetCancel += strprintf("%d ", n);
    string strSetSubVer;
    for(const auto& str : setSubVer)
        strSetSubVer += "\"" + str + "\" ";
    return strprintf(
        "CAlert(\n"
        "    nVersion     = %d\n"
        "    nRelayUntil  = %d\n"
        "    nExpiration  = %d\n"
        "    nID          = %d\n"
        "    nCancel      = %d\n"
        "    setCancel    = %s\n"
        "    nMinVer      = %d\n"
        "    nMaxVer      = %d\n"
        "    setSubVer    = %s\n"
        "    nPriority    = %d\n"
        "    strComment   = \"%s\"\n"
        "    strStatusBar = \"%s\"\n"
        "    strRPCError  = \"%s\"\n"
        ")\n",
        nVersion,
        nRelayUntil,
        nExpiration,
        nID,
        nCancel,
        strSetCancel,
        nMinVer,
        nMaxVer,
        strSetSubVer,
        nPriority,
        strComment,
        strStatusBar,
        strRPCError);
}

void CAlert::SetNull()
{
    CUnsignedAlert::SetNull();
    vchMsg.clear();
    vchSig.clear();
}

bool CAlert::IsNull() const noexcept
{
    return (nExpiration == 0);
}

uint256 CAlert::GetHash() const noexcept
{
    return Hash(this->vchMsg.begin(), this->vchMsg.end());
}

bool CAlert::IsInEffect() const noexcept
{
    return (GetAdjustedTime() < nExpiration);
}

bool CAlert::Cancels(const CAlert& alert) const noexcept
{
    if (!IsInEffect())
        return false; // this was a no-op before 31403
    return (alert.nID <= nCancel || setCancel.count(alert.nID));
}

bool CAlert::AppliesTo(int nVersion, const string& strSubVerIn) const noexcept
{
    // TODO: rework for client-version-embedded-in-strSubVer ?
    return (IsInEffect() &&
            nMinVer <= nVersion && nVersion <= nMaxVer &&
            (setSubVer.empty() || setSubVer.count(strSubVerIn)));
}

bool CAlert::AppliesToMe() const noexcept
{
    return AppliesTo(PROTOCOL_VERSION, FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, v_strings()));
}

bool CAlert::RelayTo(node_t &pnode) const
{
    if (!IsInEffect())
        return false;
    // don't relay to nodes which haven't sent their version message
    if (pnode->nVersion == 0)
        return false;
    // returns true if wasn't already contained in the set
    if (pnode->setKnown.insert(GetHash()).second)
    {
        if (AppliesTo(pnode->nVersion, pnode->strSubVer) ||
            AppliesToMe() ||
            GetAdjustedTime() < nRelayUntil)
        {
            pnode->PushMessage("alert", *this);
            return true;
        }
    }
    return false;
}

bool CAlert::CheckSignature(const v_uint8 & alertKey) const
{
    CPubKey key(alertKey);
    if (!key.Verify(Hash(vchMsg.begin(), vchMsg.end()), vchSig))
        return error("CAlert::CheckSignature(): verify signature failed");

    // Now unserialize the data
    CDataStream sMsg(vchMsg, SER_NETWORK, PROTOCOL_VERSION);
    sMsg >> *(CUnsignedAlert*)this;
    return true;
}

CAlert CAlert::getAlertByHash(const uint256 &hash)
{
    CAlert retval;
    {
        LOCK(cs_mapAlerts);
        auto mi = mapAlerts.find(hash);
        if(mi != mapAlerts.end())
            retval = mi->second;
    }
    return retval;
}

bool CAlert::ProcessAlert(const v_uint8 & alertKey, bool fThread)
{
    if (!CheckSignature(alertKey))
        return false;
    if (!IsInEffect())
        return false;

    // alert.nID=max is reserved for if the alert key is
    // compromised. It must have a pre-defined message,
    // must never expire, must apply to all versions,
    // and must cancel all previous
    // alerts or it will be ignored (so an attacker can't
    // send an "everything is OK, don't panic" version that
    // cannot be overridden):
    constexpr int maxInt = numeric_limits<int>::max();
    if (nID == maxInt)
    {
        if (!(
                nExpiration == maxInt &&
                nCancel == (maxInt-1) &&
                nMinVer == 0 &&
                nMaxVer == maxInt &&
                setSubVer.empty() &&
                nPriority == maxInt &&
                strStatusBar == "URGENT: Alert key compromised, upgrade required"
                ))
            return false;
    }

    {
        LOCK(cs_mapAlerts);
        // Cancel previous alerts
        v_uint256 vAlertsToDelete;
        for (const auto& [hash, alert] : mapAlerts)
        {
            if (Cancels(alert))
            {
                LogPrint("alert", "cancelling alert %d\n", alert.nID);
                uiInterface.NotifyAlertChanged(hash, CT_DELETED);
                vAlertsToDelete.emplace_back(hash);
            }
            else if (!alert.IsInEffect())
            {
                LogPrint("alert", "expiring alert %d\n", alert.nID);
                uiInterface.NotifyAlertChanged(hash, CT_DELETED);
                vAlertsToDelete.emplace_back(hash);
            }
        }
        // erase marked alerts
        for (const auto& hash : vAlertsToDelete)
            mapAlerts.erase(hash);

        // Check if this alert has been cancelled
        for (const auto &[hash, alert] : mapAlerts)
        {
            if (alert.Cancels(*this))
            {
                LogPrint("alert", "alert already cancelled by %d\n", alert.nID);
                return false;
            }
        }

        // Add to mapAlerts
        mapAlerts.emplace(GetHash(), *this);
        // Notify UI and -alertnotify if it applies to me
        if(AppliesToMe())
        {
            uiInterface.NotifyAlertChanged(GetHash(), CT_NEW);
            Notify(strStatusBar, fThread);
        }
    }

    LogPrint("alert", "accepted alert %d, AppliesToMe()=%d\n", nID, AppliesToMe());
    return true;
}

void CAlert::Notify(const string& strMessage, bool fThread)
{
    string strCmd = GetArg("-alertnotify", "");
    if (strCmd.empty())
        return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    string singleQuote("'");
    string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    replaceAll(strCmd, "%s", safeStatus);

    if (fThread)
    {
        thread t(runCommand, strCmd); // thread runs free
        if (t.joinable())
            t.join();
    }
    else
        runCommand(strCmd);
}
