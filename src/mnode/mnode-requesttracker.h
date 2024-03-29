#pragma once
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <protocol.h>

constexpr auto MN_REQUEST_TRACKER_FILENAME = "netfulfilled.dat";
constexpr auto MN_REQUEST_TRACKER_MAGIC_CACHE_STR = "magicFulfilledCache";

// Fulfilled requests are used to prevent nodes from asking for the same data on sync
// and from being banned for doing so too often.
class CMasternodeRequestTracker
{
private:
    typedef std::map<std::string, int64_t> fulfilledreqmapentry_t;
    typedef std::map<CNetAddr, fulfilledreqmapentry_t> fulfilledreqmap_t;

    //keep track of what node has/was asked for and when
    fulfilledreqmap_t mapFulfilledRequests;
    CCriticalSection cs_mapFulfilledRequests;

public:
    CMasternodeRequestTracker() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        LOCK(cs_mapFulfilledRequests);
        READWRITE(mapFulfilledRequests);
    }

    void AddFulfilledRequest(CAddress addr, std::string strRequest); // expire after 1 hour by default
    bool HasFulfilledRequest(CAddress addr, std::string strRequest);
    void RemoveFulfilledRequest(CAddress addr, std::string strRequest);
    int64_t GetFulfilledRequestTime(CAddress addr, std::string strRequest);

    void CheckAndRemove();
    void Clear();

    std::string ToString() const;
};
