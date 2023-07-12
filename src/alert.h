#pragma once
// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <map>
#include <set>
#include <cstdint>
#include <string>
#include <uint256.h>

#include <serialize.h>
#include <sync.h>
#include <vector_types.h>

class CAlert;
class CNode;

extern std::unordered_map<uint256, CAlert> mapAlerts;
extern CCriticalSection cs_mapAlerts;

/** Alerts are for notifying old versions if they become too obsolete and
 * need to upgrade.  The message is displayed in the status bar.
 * Alert messages are broadcast as a vector of signed data.  Unserializing may
 * not read the entire buffer if the alert is for a newer version, but older
 * versions can still relay the original data.
 */
class CUnsignedAlert
{
public:
    int nVersion;
    int64_t nRelayUntil;      // when newer nodes stop relaying to newer nodes
    int64_t nExpiration;
    int nID;
    int nCancel;
    std::set<int> setCancel;
    int nMinVer;            // lowest version inclusive
    int nMaxVer;            // highest version inclusive
    std::set<std::string> setSubVer;  // empty matches all
    int nPriority;

    // Actions
    std::string strComment;
    std::string strStatusBar;
    std::string strRPCError;

    CUnsignedAlert()
    {
		SetNull();
	}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(this->nVersion);
        READWRITE(nRelayUntil);
        READWRITE(nExpiration);
        READWRITE(nID);
        READWRITE(nCancel);
        READWRITE(setCancel);
        READWRITE(nMinVer);
        READWRITE(nMaxVer);
        READWRITE(setSubVer);
        READWRITE(nPriority);

        READWRITE(LIMITED_STRING(strComment, 65536));
        READWRITE(LIMITED_STRING(strStatusBar, 256));
        READWRITE(LIMITED_STRING(strRPCError, 256));
    }

    void SetNull();

    std::string ToString() const;
};

/** An alert is a combination of a serialized CUnsignedAlert and a signature. */
class CAlert : public CUnsignedAlert
{
public:
    v_uint8 vchMsg;
    v_uint8 vchSig;

    CAlert()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(vchMsg);
        READWRITE(vchSig);
    }

    void SetNull();
    bool IsNull() const noexcept;
    uint256 GetHash() const noexcept;
    bool IsInEffect() const noexcept;
    bool Cancels(const CAlert& alert) const noexcept;
    bool AppliesTo(int nVersion, const std::string& strSubVerIn) const noexcept;
    bool AppliesToMe() const noexcept;
    bool RelayTo(CNode* pnode) const;
    bool CheckSignature(const v_uint8 & alertKey) const;
    bool ProcessAlert(const v_uint8 & alertKey, bool fThread = true); // fThread means run -alertnotify in a free-running thread
    static void Notify(const std::string& strMessage, bool fThread);

    /*
     * Get copy of (active) alert object by hash. Returns a null alert if it is not found.
     */
    static CAlert getAlertByHash(const uint256 &hash);
};

