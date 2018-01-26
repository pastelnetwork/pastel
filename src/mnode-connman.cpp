// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mnode-connman.h"
#include "util.h"

#include "mnode-plugin.h"
#include "mnode-db.h"

static CSemaphore *semMasternodeOutbound = NULL;

void CConnman::ThreadMnbRequestConnections()
{
    // Connecting to specific addresses, no masternode connections available
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
        return;

    while (true)
    {
        MilliSleep(500);

        CSemaphoreGrant grant(*semMasternodeOutbound);

        std::pair<CService, std::set<uint256> > p = masterNodePlugin.masternodeManager.PopScheduledMnbRequestConnection();
        if(p.first == CService() || p.second.empty()) continue;

        ConnectNode(CAddress(p.first, NODE_NETWORK), NULL, true);

        LOCK(cs_vNodes);

        CNode *pnode = FindNode(p.first);
        if(!pnode || pnode->fDisconnect) continue;

        grant.MoveTo(pnode->grantMasternodeOutbound);

        // compile request vector
        std::vector<CInv> vToFetch;
        std::set<uint256>::iterator it = p.second.begin();
        while(it != p.second.end()) {
            if(*it != uint256()) {
                vToFetch.push_back(CInv(MSG_MASTERNODE_ANNOUNCE, *it));
                LogPrint("masternode", "ThreadMnbRequestConnections -- asking for mnb %s from addr=%s\n", it->ToString(), p.first.ToString());
            }
            ++it;
        }

        // ask for data
        pnode->PushMessage("getdata", vToFetch);
    }
}
