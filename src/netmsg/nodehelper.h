// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CONNMAN_H
#define CONNMAN_H

#include <string>
#include <vector>

#include "chainparams.h"
#include "protocol.h"
#include "net.h"

class CNodeHelper 
{
public:
/***** Push message helpers *****/
    static void RelayInv(CInv &inv, const int minProtoVersion = MIN_PEER_PROTO_VERSION)
    {
        LOCK(cs_vNodes);
        for (auto pnode : vNodes)
        {
            if (pnode->nVersion >= minProtoVersion)
                pnode->PushInventory(inv);
        }
    }

/*****  *****/
    static bool NodeFullyConnected(const CNode* pnode)
    {
        return pnode && pnode->fSuccessfullyConnected && !pnode->fDisconnect;
    }

    struct CFullyConnectedOnly {
        bool operator() (const CNode* pnode) const {
            return NodeFullyConnected(pnode);
        }
    };
    constexpr static const CFullyConnectedOnly FullyConnectedOnly{};

    struct CAllNodes {
        bool operator() (const CNode*) const {return true;}
    };

    constexpr static const CAllNodes AllNodes{};
};

#endif
