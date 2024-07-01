// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <atomic>

#include <utils/sync.h>
#include <utils/utilstrencodings.h>
#include <utils/random.h>
#include <protocol.h>
#include <hash.h>
#include <chainparams.h>
#include <timedata.h>
#include <netmsg/netconsts.h>
#include <netmsg/netmessage.h>
#include <netmsg/node.h>
#include <netmsg/nodemanager.h>
using namespace std;

uint64_t nLocalHostNonce = 0;
uint64_t nLocalServices = NODE_NETWORK;
uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;
map<CSubNet, int64_t> CNode::setBanned;
CCriticalSection CNode::cs_setBanned;
vector<CSubNet> CNode::vWhitelistedRange;
CCriticalSection CNode::cs_vWhitelistedRange;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);
std::string strSubVersion;

// Signals for message handling
static CNodeSignals g_node_signals;
CNodeSignals& GetNodeSignals() { return g_node_signals; }

atomic<NodeId> CNode::nLastNodeId(0);

size_t ReceiveFloodSize() { return 1000*GetArg("-maxreceivebuffer", 5*1000); }
size_t SendBufferSize() { return 1000*GetArg("-maxsendbuffer", 1*1000); }

// requires LOCK(cs_vSendMsg)
void SocketSendData(CNode &node)
{
    auto it = node.vSendMsg.begin();
    while (it != node.vSendMsg.end())
    {
        const CSerializeData &data = *it;
        assert(data.size() > node.nSendOffset);
        const int nBytes = send(node.hSocket, &data[node.nSendOffset], static_cast<int>(data.size() - node.nSendOffset), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (nBytes > 0)
        {
            node.nLastSend = GetTime();
            node.nSendBytes += nBytes;
            node.nSendOffset += nBytes;
            node.RecordBytesSent(nBytes);
            if (node.nSendOffset == data.size())
            {
                node.nSendOffset = 0;
                node.nSendSize -= data.size();
                it++;
            } else // could not send full message; stop sending more
                break;
        }
        else
        {
            if (nBytes < 0)
            {
                // error
                int nErr = WSAGetLastError();
                if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
                {
                    LogFnPrintf("socket send error %s", GetErrorString(nErr));
                    node.CloseSocketDisconnect();
                }
            }
            // couldn't send anything at all
            break;
        }
    }

    if (it == node.vSendMsg.end())
    {
        assert(node.nSendOffset == 0);
        assert(node.nSendSize == 0);
    }
    node.vSendMsg.erase(node.vSendMsg.begin(), it);
}

CNode::CNode(SOCKET hSocketIn, const CAddress& addrIn, const string& addrNameIn, bool fInboundIn, bool fNetworkNodeIn) :
    ssSend(SER_NETWORK, INIT_PROTO_VERSION),
    addrKnown(5000, 0.001),
    setInventoryKnown(SendBufferSize() / 1000)
{
    nServices = 0;
    hSocket = hSocketIn;
    nRecvVersion = INIT_PROTO_VERSION;
    nLastSend = 0;
    nLastRecv = 0;
    nSendBytes = 0;
    nRecvBytes = 0;
    nTimeConnected = GetTime();
    nTimeOffset = 0;
    addr = addrIn;
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
    nVersion = 0;
    strSubVer = "";
    fWhitelisted = false;
    fOneShot = false;
    fClient = false; // set by version message
    fInbound = fInboundIn;
    fNetworkNode = fNetworkNodeIn;
    fSuccessfullyConnected = false;
    fDisconnect = false;
    nSendSize = 0;
    nSendOffset = 0;
    hashContinue = uint256();
    nStartingHeight = -1;
    fGetAddr = false;
    fRelayTxes = false;
    fSentAddr = false;
    pfilter = new CBloomFilter();
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    fMasternode = false;

    nMinPingUsecTime = numeric_limits<int64_t>::max();

    id = CNode::nLastNodeId.fetch_add(1);

    if (fLogIPs)
        LogPrint("net", "Added connection to %s peer=%d\n", addrName, id);
    else
        LogPrint("net", "Added connection peer=%d\n", id);

    // Be shy and don't send version until we hear
    if (hSocket != INVALID_SOCKET && !fInbound)
        PushVersion();

    GetNodeSignals().InitializeNode(GetId(), *this);
}

CNode::~CNode()
{
    CloseSocket(hSocket);

    if (pfilter)
        delete pfilter;

    GetNodeSignals().FinalizeNode(GetId());
}

void CNode::AskFor(const CInv& inv)
{
    if (mapAskFor.size() > MAPASKFOR_MAX_SZ || setAskFor.size() > SETASKFOR_MAX_SZ)
        return;
    // a peer may not have multiple non-responded queue positions for a single inv item
    if (!setAskFor.insert(inv.hash).second)
        return;

    // We're using mapAskFor as a priority queue,
    // the key is the earliest time the request can be sent
    int64_t nRequestTime;
    auto it = mapAlreadyAskedFor.find(inv);
    if (it != mapAlreadyAskedFor.end())
        nRequestTime = it->second;
    else
        nRequestTime = 0;
    LogPrint("net", "askfor %s  %d (%s) peer=%d\n", inv.ToString(), nRequestTime, DateTimeStrFormat("%H:%M:%S", nRequestTime/1000000), id);

    // Make sure not to reuse time indexes to keep things in the same order
    int64_t nNow = GetTimeMicros() - 1000000;
    static int64_t nLastTime;
    ++nLastTime;
    nNow = max(nNow, nLastTime);
    nLastTime = nNow;

    // Each retry is 2 minutes after the last
    nRequestTime = max(nRequestTime + 2 * 60 * 1000000, nNow);
    if (it != mapAlreadyAskedFor.end())
        mapAlreadyAskedFor.update(it, nRequestTime);
    else
        mapAlreadyAskedFor.insert(make_pair(inv, nRequestTime));
    mapAskFor.insert(make_pair(nRequestTime, inv));
}

void CNode::BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSendMsg)
{
    ENTER_CRITICAL_SECTION(cs_vSendMsg);
    assert(ssSend.size() == 0);
    ssSend << CMessageHeader(Params().MessageStart(), pszCommand, 0);
    LogPrint("net", "sending: [%s]\n", SanitizeString(pszCommand));
}

void CNode::AbortMessage() UNLOCK_FUNCTION(cs_vSendMsg)
{
    ssSend.clear();

    LEAVE_CRITICAL_SECTION(cs_vSendMsg);

    LogPrint("net", "(aborted)\n");
}

void CNode::EndMessage() UNLOCK_FUNCTION(cs_vSendMsg)
{
    // The -*messagestest options are intentionally not documented in the help message,
    // since they are only used during development to debug the networking code and are
    // not intended for end-users.
    if (mapArgs.count("-dropmessagestest") && GetRand(GetArg("-dropmessagestest", 2)) == 0)
    {
        LogPrint("net", "dropmessages DROPPING SEND MESSAGE\n");
        AbortMessage();
        return;
    }
    if (mapArgs.count("-fuzzmessagestest"))
        Fuzz(static_cast<int>(GetArg("-fuzzmessagestest", 10)));

    if (ssSend.empty())
    {
        LEAVE_CRITICAL_SECTION(cs_vSendMsg);
        return;
    }
    // Set the size
    const uint32_t nSize = static_cast<uint32_t>(ssSend.size() - CMessageHeader::HEADER_SIZE);
    WriteLE32((uint8_t*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], nSize);

    // Set the checksum
    const uint256 hash = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    assert(ssSend.size () >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
    memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

    LogPrint("net", "sent: (%u bytes) peer=%d\n", nSize, id);

    auto &msgData = vSendMsg.emplace_back();
    ssSend.GetAndClear(msgData);
    nSendSize += msgData.size();

    // If write queue empty, attempt "optimistic write"
    if (msgData == vSendMsg.front())
        SocketSendData(*this);

    LEAVE_CRITICAL_SECTION(cs_vSendMsg);
}

void CNode::PushAddress(const CAddress& addr)
{
    // Known checking here is only to save space from duplicates.
    // SendMessages will filter it again for knowns that were added
    // after addresses were pushed.
    if (addr.IsValid() && !addrKnown.contains(addr.GetKey()))
    {
        if (vAddrToSend.size() >= MAX_ADDR_SZ)
            vAddrToSend[insecure_rand() % vAddrToSend.size()] = addr;
        else
            vAddrToSend.push_back(addr);
    }
}

void CNode::CloseSocketDisconnect()
{
    fDisconnect = true;
    if (hSocket != INVALID_SOCKET)
    {
        LogPrint("net", "disconnecting peer=%d\n", id);
        CloseSocket(hSocket);
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
        vRecvMsg.clear();
}

void CNode::PushVersion()
{
    int nBestHeight = GetNodeSignals().GetHeight().get_value_or(0);

    const int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0",0)));
    CAddress addrMe = GetLocalAddress(&addr);
    GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
    if (fLogIPs)
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), addrYou.ToString(), id);
    else
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), id);
    PushMessage("version", PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe,
                nLocalHostNonce, strSubVersion, nBestHeight, true);
}

void CNode::ClearBanned()
{
    LOCK(cs_setBanned);
    setBanned.clear();
}

bool CNode::IsBanned(CNetAddr ip)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        for (const auto& [subNet, t] : setBanned)
        {
            if(subNet.Match(ip) && GetTime() < t)
                fResult = true;
        }
    }
    return fResult;
}

bool CNode::IsBanned(CSubNet subnet)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        const auto it = setBanned.find(subnet);
        if (it != setBanned.cend())
        {
            int64_t t = it->second;
            if (GetTime() < t)
                fResult = true;
        }
    }
    return fResult;
}

void CNode::Ban(const CNetAddr& addr, int64_t bantimeoffset, bool sinceUnixEpoch)
{
    CSubNet subNet(addr.ToString()+(addr.IsIPv4() ? "/32" : "/128"));
    Ban(subNet, bantimeoffset, sinceUnixEpoch);
}

void CNode::Ban(const CSubNet& subNet, int64_t bantimeoffset, bool sinceUnixEpoch)
{
    int64_t banTime = GetTime()+GetArg("-bantime", 60*60*24);  // Default 24-hour ban
    if (bantimeoffset > 0)
        banTime = (sinceUnixEpoch ? 0 : GetTime() )+bantimeoffset;

    LOCK(cs_setBanned);
    if (setBanned[subNet] < banTime)
        setBanned[subNet] = banTime;
}

bool CNode::Unban(const CNetAddr &addr)
{
    CSubNet subNet(addr.ToString()+(addr.IsIPv4() ? "/32" : "/128"));
    return Unban(subNet);
}

bool CNode::Unban(const CSubNet &subNet)
{
    LOCK(cs_setBanned);
    if (setBanned.erase(subNet))
        return true;
    return false;
}

void CNode::GetBanned(map<CSubNet, int64_t> &banMap)
{
    LOCK(cs_setBanned);
    banMap = setBanned; //create a thread safe copy
}

bool CNode::IsWhitelistedRange(const CNetAddr &addr)
{
    LOCK(cs_vWhitelistedRange);
    for (const auto& subnet : vWhitelistedRange)
    {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

void CNode::AddWhitelistedRange(const CSubNet &subnet)
{
    LOCK(cs_vWhitelistedRange);
    vWhitelistedRange.push_back(subnet);
}

void CNode::copyStats(CNodeStats &stats)
{
    stats.nodeid = this->GetId();
    stats.nServices = nServices;
    stats.nLastSend = nLastSend;
    stats.nLastRecv = nLastRecv;
    stats.nTimeConnected = nTimeConnected;
    stats.nTimeOffset = nTimeOffset;
    stats.addrName = addrName;
    stats.nVersion = nVersion;
    stats.cleanSubVer = cleanSubVer;
    stats.fInbound = fInbound;
    stats.nStartingHeight = nStartingHeight;
    stats.nSendBytes = nSendBytes;
    stats.nRecvBytes = nRecvBytes;
    stats.fWhitelisted = fWhitelisted;

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != nPingNonceSent) && (0 != nPingUsecStart))
        nPingUsecWait = GetTimeMicros() - nPingUsecStart;

    // Raw ping time is in microseconds, but show it to user as whole seconds (Bitcoin users should be well used to small numbers with many decimal places by now :)
    stats.dPingTime = (((double)nPingUsecTime) / 1e6);
    stats.dPingWait = (((double)nPingUsecWait) / 1e6);

    // Leave string empty if addrLocal invalid (not filled in yet)
    stats.addrLocal = addrLocal.IsValid() ? addrLocal.ToString() : "";
}

// requires LOCK(cs_vRecvMsg)
bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes)
{
    while (nBytes > 0)
    {

        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() ||
            vRecvMsg.back().complete())
            vRecvMsg.push_back(CNetMessage(Params().MessageStart(), SER_NETWORK, nRecvVersion));

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
                return false;

        if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH)
        {
            LogPrint("net", "Oversized message from peer=%i, disconnecting\n", GetId());
            return false;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete())
        {
            msg.nTime = GetTimeMicros();
            gl_NodeManager.MessageHandlerNotifyOne();
        }
    }

    return true;
}

bool CNode::IsNotUsed()
{
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (!lockRecv)
		return false;
    TRY_LOCK(cs_vSendMsg, lockSend);
    if (!lockSend)
        return false;
	return vRecvMsg.empty() && nSendSize == 0 && ssSend.empty();
}

void CNode::RecordBytesRecv(uint64_t bytes)
{
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes)
{
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv()
{
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent()
{
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}

void CNode::Fuzz(int nChance)
{
    // Don't fuzz initial handshake
    if (!fSuccessfullyConnected)
        return;
    // Fuzz 1 of every nChance messages
    if (GetRand(nChance) != 0)
        return;

    switch (GetRand(3))
    {
    case 0:
        // xor a random byte with a random value:
        if (!ssSend.empty())
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend[pos] ^= (unsigned char)(GetRand(256));
        }
        break;

    case 1:
        // delete a random byte:
        if (!ssSend.empty())
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend.erase(ssSend.begin()+pos);
        }
        break;
    case 2:
        // insert a random byte at a random position
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            char ch = (char)GetRand(256);
            ssSend.insert(ssSend.begin()+pos, ch);
        }
        break;
    }
    // Chance of more than one change half the time:
    // (more changes exponentially less likely):
    Fuzz(2);
}

