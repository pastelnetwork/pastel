// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef WIN32
# include <arpa/inet.h>
#endif

#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <protocol.h>

using namespace std;

static constexpr array<const char *, 13> NET_MSG_TYPE =
{
    "ERROR",
    "tx",
    "block",
    "filtered block",

    //MasterNode
    NetMsgType::GOVERNANCE,
    NetMsgType::GOVERNANCEVOTE,
    NetMsgType::MASTERNODEPAYMENTVOTE,
    NetMsgType::MASTERNODEPAYMENTBLOCK,
    NetMsgType::MNANNOUNCE,
    NetMsgType::MNPING,
    NetMsgType::DSTX,
    NetMsgType::MNVERIFY,
    NetMsgType::MASTERNODEMESSAGE
};

CMessageHeader::CMessageHeader(const MessageStartChars& pchMessageStartIn)
{
    memcpy(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    nMessageSize = -1;
    nChecksum = 0;
}

CMessageHeader::CMessageHeader(const MessageStartChars& pchMessageStartIn, const char* pszCommand, unsigned int nMessageSizeIn)
{
    memcpy(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    strncpy(pchCommand, pszCommand, COMMAND_SIZE);
    nMessageSize = nMessageSizeIn;
    nChecksum = 0;
}

string CMessageHeader::GetCommand() const noexcept
{
    return string(pchCommand, pchCommand + strnlen(pchCommand, COMMAND_SIZE));
}

bool CMessageHeader::IsValid(string &error, const MessageStartChars& pchExpectedMessageStart) const
{
    // Check start string
    error.clear();
    bool bRet = false;
    do
    {
        if (memcmp(pchMessageStart, pchExpectedMessageStart, MESSAGE_START_SIZE) != 0)
        {
            error = "Invalid message prefix";
            break;
        }

        bool bCmdValid = true;
        // Check the command string for errors
        for (const char* p1 = pchCommand; p1 < pchCommand + COMMAND_SIZE; p1++)
        {
            if (*p1 == 0)
            {
                // Must be all zeros after the first zero
                for (; p1 < pchCommand + COMMAND_SIZE; p1++)
                {
                    if (*p1 != 0)
                    {
                        bCmdValid = false;
                        error = strprintf("Character '0x%X' found in the message command after first zero at pos %u", *p1, p1 - pchCommand);
                        break;
                    }
                }
            }
            else if (*p1 < ' ' || *p1 > 0x7E)
            {
                bCmdValid = false;
                error = strprintf("Invalid character '0x%X' found in the message command at pos %u", *p1, p1 - pchCommand);
                break;
            }
        }
        if (!bCmdValid)
            break;

        // Message size
        if (nMessageSize > MAX_DATA_SIZE)
        {
            error = strprintf("Message size (%u) exceeds max size %u bytes", nMessageSize, MAX_DATA_SIZE);
            break;
        }

        bRet = true;
    } while (false);
    return bRet;
}

CAddress::CAddress() : CService()
{
    Init();
}

CAddress::CAddress(CService ipIn, uint64_t nServicesIn) : CService(ipIn)
{
    Init();
    nServices = nServicesIn;
}

void CAddress::Init()
{
    nServices = NODE_NETWORK;
    nTime = 100000000;
}

CInv::CInv() noexcept
{
    type = 0;
    hash.SetNull();
}

CInv::CInv(const int typeIn, const uint256& hashIn) noexcept
{
    type = typeIn;
    hash = hashIn;
}

CInv::CInv(const string& strType, const uint256& hashIn)
{
    size_t i = 0;
    for (i = 1; i < NET_MSG_TYPE.size(); ++i)
    {
        if (strType == NET_MSG_TYPE[i])
        {
            type = static_cast<int>(i);
            break;
        }
    }
    if (i == NET_MSG_TYPE.size())
        throw out_of_range(strprintf("CInv::CInv(string, uint256): unknown type '%s'", strType));
    hash = hashIn;
}

bool operator<(const CInv& a, const CInv& b)
{
    return (a.type < b.type || (a.type == b.type && a.hash < b.hash));
}

bool CInv::IsKnownType() const noexcept
{
    return (type >= 1 && type < NET_MSG_TYPE.size());
}

const char* CInv::GetCommand() const
{
    if (!IsKnownType())
        throw out_of_range(strprintf("CInv::GetCommand(): type=%d unknown type", type));
    return NET_MSG_TYPE[type];
}

string CInv::ToString() const
{
    return strprintf("%s %s", GetCommand(), hash.ToString());
}
