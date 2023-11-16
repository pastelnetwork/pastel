#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2019-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <map>

#include <utils/sync.h>
#include <chainparams.h>
#include <net.h>
#include <primitives/transaction.h>
#include <key.h>

// Responsible for activating the Masternode and pinging the network
class CActiveMasternode
{
public:
    enum class MasternodeType
    {
        Unknown = 0,
        Remote  = 1
    };

    enum class ActiveMasternodeState : uint8_t
    {
        Initial        = 0, // initial state
        SyncInProcess,
        InputTooNew,
        NotCapable,
        NeedMnId,
        Started,

        COUNT
    };

    typedef struct _ActiveMNStateInfo
    {
        const ActiveMasternodeState state{};
        const char* szState{};
    } ActiveMNStateInfo;

protected:
    static constexpr std::array<ActiveMNStateInfo, to_integral_type<ActiveMasternodeState>(ActiveMasternodeState::COUNT)> ACTIVE_MN_STATE =
        {{
               { ActiveMasternodeState::Initial,         "INITIAL" },
               { ActiveMasternodeState::SyncInProcess,   "SYNC_IN_PROCESS" },
               { ActiveMasternodeState::InputTooNew,     "INPUT_TOO_NEW" },
               { ActiveMasternodeState::NotCapable,      "NOT_CAPABLE" },
               { ActiveMasternodeState::NeedMnId,        "NEED_MNID" },
               { ActiveMasternodeState::Started,         "STARTED" }
        }};
    
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    MasternodeType mnType;

    bool fPingerEnabled;

    /// Ping Masternode
    bool SendMasternodePing();

public:
    // Keys for the active Masternode
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    // Initialized while registering Masternode
    COutPoint outpoint;
    CService service;

    ActiveMasternodeState nState;
    std::string strNotCapableReason;

    CActiveMasternode() :
        mnType(MasternodeType::Unknown),
        fPingerEnabled(false),
        nState(ActiveMasternodeState::Initial)
    {}

    /// Manage state of active Masternode
    void ManageState();

    /**
     * Get Active MasterNode current state string.
     * 
     * \return Active MN state string (not nullptr)
     */
    auto GetStateString() const noexcept
    {
        return ACTIVE_MN_STATE[to_integral_type<ActiveMasternodeState>(nState)].szState;
    }
    std::string GetStatus() const noexcept;
    std::string GetTypeString() const noexcept;
    bool IsStarted() const noexcept { return nState == ActiveMasternodeState::Started; }
    bool NeedMnId() const noexcept { return nState == ActiveMasternodeState::NeedMnId; }

private:
    void ManageStateInitial();
    void ManageStateRemote();
    // check that mnid is registered
    bool CheckMnId(const COutPoint &outPoint);
};
