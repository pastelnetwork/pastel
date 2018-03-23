// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMASTERNODE_H
#define ACTIVEMASTERNODE_H

#include <string>
#include <map>

#include "chainparams.h"
#include "net.h"
#include "sync.h"
#include "primitives/transaction.h"
#include "key.h"

// Responsible for activating the Masternode and pinging the network
class CActiveMasternode
{
public:
    enum class MasternodeType {
        Unknown = 0,
        Remote  = 1
    };

    enum class ActiveMasternodeState {
        Initial        = 0, // initial state
        SyncInProcess  = 1,
        InputTooNew    = 2,
        NotCapable     = 3,
        Started        = 4
    };

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


    CActiveMasternode()
        : mnType(MasternodeType::Unknown),
          fPingerEnabled(false),
          pubKeyMasternode(),
          keyMasternode(),
          outpoint(),
          service(),
          nState(ActiveMasternodeState::Initial)
    {}

    /// Manage state of active Masternode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
};

#endif
