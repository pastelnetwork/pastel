#pragma once
// Copyright (c) 2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "enum_util.h"
#include "tinyformat.h"
#include "rpc/protocol.h"
#include "core_io.h"
#include "str_utils.h"

#include <string>
#include <unordered_map>
#include <sstream>

template <typename RPC_CMD_ENUM>
class RPCCommandParser
{
public:
    RPCCommandParser(const UniValue& params, const size_t nCmdIndex, const char* szCmdList) :
        m_Params(params),
        m_Cmd(RPC_CMD_ENUM::unknown),
        m_nCmdIndex(nCmdIndex)
    {
        std::string error;
        if (!ParseCmdList(error, szCmdList))
            throw JSONRPCError(RPC_MISC_ERROR, "Failed to parse rpc command list. " + error);
        ParseParams();
    }

    RPC_CMD_ENUM cmd() const noexcept { return m_Cmd; }
    bool IsCmdSupported() const noexcept { return m_Cmd != RPC_CMD_ENUM::unknown; }
    bool IsCmd(const RPC_CMD_ENUM& cmd) const noexcept { return m_Cmd == cmd; }

protected:
    using map_cmdenum_t = std::unordered_map<std::string, RPC_CMD_ENUM>;

    bool ParseCmdList(std::string &error, const char* szCmdList)
    {
        error.clear();
        if (!szCmdList)
        {
            error = "rpc command list is empty";
            return false;
        }
        m_CmdMap.clear();
        std::string sCmdList(szCmdList), sToken;
        std::istringstream tokenStream(sCmdList);
        auto nCmdNo = to_integral_type<RPC_CMD_ENUM>(RPC_CMD_ENUM::unknown);
        const auto nMaxCmdNo = to_integral_type<RPC_CMD_ENUM>(RPC_CMD_ENUM::max_command_count);
        bool bRet = true;
        while (std::getline(tokenStream, sToken, ','))
        {
            if (++nCmdNo >= nMaxCmdNo)
            {
                bRet = false;
                break;
            }
            trim(sToken);
            lowercase(sToken);
            // replace double underscore with single hyphen
            replaceAll(sToken, "__", "-");
            m_CmdMap.emplace(sToken, static_cast<RPC_CMD_ENUM>(nCmdNo));
        }
        if (nCmdNo + 1 != nMaxCmdNo)
            bRet = false;
        if (!bRet)
            error = tfm::format("Failed to parser the list of rpc commands [%s] - mismatch", szCmdList);
        return bRet;
    }

    void ParseParams() noexcept
    {
        if (m_Params.size() <= m_nCmdIndex)
            return;
        m_sCmd = m_Params[m_nCmdIndex].get_str();
        trim(m_sCmd);
        lowercase(m_sCmd);
        if (m_sCmd.empty())
            return;
        const auto it = m_CmdMap.find(m_sCmd);
        if (it != m_CmdMap.cend())
            m_Cmd = it->second;
    }

    map_cmdenum_t m_CmdMap;     // map <command -> cmd_enum>
    std::string m_sCmd;         // command
    const UniValue &m_Params;   // rpc parameters
    RPC_CMD_ENUM m_Cmd;         // cmd enumeration
    size_t m_nCmdIndex;
};

// parse first command in params
// examples:
//     RPC_CMD_PARSER(TICKETS, params, register, find, list, get)
// special syntax for commands that containe hyphen (-):
//      find-all -> find__all
// double underscore is replaced by single hyphen in the command name
#define RPC_CMD_PARSER(command,params,...) \
    enum class RPC_CMD_##command : uint32_t{unknown = 0, __VA_ARGS__, max_command_count}; \
    RPCCommandParser<RPC_CMD_##command> command(params, 0, #__VA_ARGS__);

// parse second command in params
// examples:
//     RPC_CMD_PARSER2(LIST, params, id, art, act, sell, buy, trade, down)
#define RPC_CMD_PARSER2(command,params,...) \
    enum class RPC_CMD_##command : uint32_t \
    { unknown = 0, __VA_ARGS__, max_command_count }; \
    RPCCommandParser<RPC_CMD_##command> command(params, 1, #__VA_ARGS__);

