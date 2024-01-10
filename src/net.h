#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <cstdint>
#include <deque>
#include <functional>

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <utilstrencodings.h>
#include <chainparams.h>
#include <svc_thread.h>
#include <netmsg/node.h>

class CAddrMan;
class CBlockIndex;
class CScheduler;

/** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
static constexpr int PING_INTERVAL = 2 * 60;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
static constexpr int DISCONNECT_TIMEOUT_INTERVAL_SECS = 20 * 60;
/** Maximum length of strSubVer in `version` message */
static constexpr unsigned int MAX_SUBVERSION_LENGTH = 256;
/** -listen default */
static constexpr bool DEFAULT_LISTEN = true;
// The period before a network upgrade activates, where connections to upgrading peers are preferred (in blocks).
constexpr uint32_t MAINNET_NETWORK_UPGRADE_PEER_PREFERENCE_BLOCK_PERIOD = 24 * 24 * 3;
constexpr uint32_t TESTNET_NETWORK_UPGRADE_PEER_PREFERENCE_BLOCK_PERIOD = 100;
constexpr uint32_t DEVNET_NETWORK_UPGRADE_PEER_PREFERENCE_BLOCK_PERIOD = 100;
constexpr uint32_t REGTEST_NETWORK_UPGRADE_PEER_PREFERENCE_BLOCK_PERIOD = 24;

void AddOneShot(const std::string& strDest);
void AddressCurrentlyConnected(const CService& addr);

bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound = nullptr, const char *strDest = nullptr, bool fOneShot = false);
bool BindListenPort(const CService &bindAddr, std::string& strError, bool fWhitelisted = false);

// returns true if we have at least one active network interface
bool hasActiveNetworkInterface();
// returns true if we have internet connectivity
bool hasInternetConnectivity(std::function<bool()> shouldStop);

/** Maximum number of connections to simultaneously allow (aka connection slots) */
extern size_t nMaxConnections;

extern std::map<CInv, CDataStream> mapRelay;
extern std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
extern CCriticalSection cs_mapRelay;

extern std::vector<std::string> vAddedNodes;
extern CCriticalSection cs_vAddedNodes;

class CSocketHandlerThread : public CStoppableServiceThread
{
public:
    CSocketHandlerThread() : 
        CStoppableServiceThread("net")
    {}

    void execute() override;
};


class CTransaction;
void RelayTransaction(const CTransaction& tx);
void RelayTransaction(const CTransaction& tx, const CDataStream& ss);

void StartNode(CServiceThreadGroup& threadGroup, CScheduler &scheduler);
bool StopNode();

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
    fs::path pathAddr;

public:
    CAddrDB();
    bool Write(const CAddrMan& addr);
    bool Read(CAddrMan& addr);
};
