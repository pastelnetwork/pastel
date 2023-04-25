// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"
#include "main.h"

#include "mnode/mnode-controller.h"
#include "mnode/mnode-requesttracker.h"

void CMasternodeRequestTracker::AddFulfilledRequest(CAddress addr, std::string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    mapFulfilledRequests[addr][strRequest] = GetTime() + masterNodeCtrl.nFulfilledRequestExpireTime;
}

bool CMasternodeRequestTracker::HasFulfilledRequest(CAddress addr, std::string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.find(addr);

    return  it != mapFulfilledRequests.end() &&
            it->second.find(strRequest) != it->second.end() &&
            it->second[strRequest] > GetTime();
}

int64_t CMasternodeRequestTracker::GetFulfilledRequestTime(CAddress addr, std::string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.find(addr);

    if it != mapFulfilledRequests.end() &&
       it->second.find(strRequest) != it->second.end()
        return it->second[strRequest];
    return -1;
}

void CMasternodeRequestTracker::RemoveFulfilledRequest(CAddress addr, std::string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.find(addr);

    if (it != mapFulfilledRequests.end()) {
        it->second.erase(strRequest);
    }
}

void CMasternodeRequestTracker::CheckAndRemove()
{
    LOCK(cs_mapFulfilledRequests);

    int64_t now = GetTime();
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.begin();

    while(it != mapFulfilledRequests.end()) {
        fulfilledreqmapentry_t::iterator it_entry = it->second.begin();
        while(it_entry != it->second.end()) {
            if(now > it_entry->second) {
                it->second.erase(it_entry++);
            } else {
                ++it_entry;
            }
        }
        if(it->second.size() == 0) {
            mapFulfilledRequests.erase(it++);
        } else {
            ++it;
        }
    }
}

void CMasternodeRequestTracker::Clear()
{
    LOCK(cs_mapFulfilledRequests);
    mapFulfilledRequests.clear();
}

std::string CMasternodeRequestTracker::ToString() const
{
    std::ostringstream info;
    info << "Nodes with fulfilled requests: " << (int)mapFulfilledRequests.size();
    return info.str();
}
