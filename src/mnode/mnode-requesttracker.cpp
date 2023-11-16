// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utils/util.h>
#include <main.h>

#include <mnode/mnode-controller.h>
#include <mnode/mnode-requesttracker.h>

using namespace std;

void CMasternodeRequestTracker::AddFulfilledRequest(CAddress addr, string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    mapFulfilledRequests[addr][strRequest] = GetTime() + masterNodeCtrl.nFulfilledRequestExpireTime;
}

bool CMasternodeRequestTracker::HasFulfilledRequest(CAddress addr, string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto it = mapFulfilledRequests.find(addr);

    return  it != mapFulfilledRequests.cend() &&
            it->second.find(strRequest) != it->second.cend() &&
            it->second[strRequest] > GetTime();
}

int64_t CMasternodeRequestTracker::GetFulfilledRequestTime(CAddress addr, string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto it = mapFulfilledRequests.find(addr);

    if (it != mapFulfilledRequests.cend() &&
       it->second.find(strRequest) != it->second.cend())
        return it->second[strRequest];
    return -1;
}

void CMasternodeRequestTracker::RemoveFulfilledRequest(CAddress addr, string strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    auto it = mapFulfilledRequests.find(addr);

    if (it != mapFulfilledRequests.end())
        it->second.erase(strRequest);
}

void CMasternodeRequestTracker::CheckAndRemove()
{
    LOCK(cs_mapFulfilledRequests);

    const int64_t now = GetTime();
    auto it = mapFulfilledRequests.begin();

    while (it != mapFulfilledRequests.end())
    {
        auto it_entry = it->second.begin();
        while(it_entry != it->second.end())
        {
            if (now > it_entry->second)
                it_entry = it->second.erase(it_entry);
            else
                ++it_entry;
        }
        if (it->second.empty())
            it = mapFulfilledRequests.erase(it);
        else
            ++it;
    }
}

void CMasternodeRequestTracker::Clear()
{
    LOCK(cs_mapFulfilledRequests);
    mapFulfilledRequests.clear();
}

string CMasternodeRequestTracker::ToString() const
{
    ostringstream info;
    info << "Nodes with fulfilled requests: " << (int)mapFulfilledRequests.size();
    return info.str();
}
