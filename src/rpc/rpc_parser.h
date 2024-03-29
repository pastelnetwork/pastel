#pragma once
// Copyright (c) 2021-2023 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <sstream>
#include <string>
#include <unordered_map>

#include <utils/tinyformat.h>
#include <utils/enum_util.h>
#include <utils/str_utils.h>
#include <rpc/protocol.h>
#include <core_io.h>

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
            throw JSONRPCError(RPC_MISC_ERROR, "Failed to parse the list of RPC commands. " + error);
        if (!ParseParams(error))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to parse RPC parameters. " + error);
    }

    // returns number of supported commands
    constexpr size_t size()
    {
        return to_integral_type<RPC_CMD_ENUM>(RPC_CMD_ENUM::rpc_command_count) - 1;
    }

    // returns rpc command (enumeration type)
    RPC_CMD_ENUM cmd() const noexcept { return m_Cmd; }
    // returns string representation of the rpc command 
    std::string getCmdStr() const noexcept { return m_sCmd; }
    // returns true if command is supported
    bool IsCmdSupported() const noexcept { return m_Cmd != RPC_CMD_ENUM::unknown; }
    // returns true if cmd was passed
    bool IsCmd(const RPC_CMD_ENUM& cmd) const noexcept { return m_Cmd == cmd; }

    // returns true if cmd equals any of the parameters (all params should have RPC_CMD_ENUM type)
    // function is enabled if all Ts... have the same type as RPC_CMD_ENUM
    template <typename T, typename... Ts, typename = AllParamsHaveSameType<T, Ts...>>
    bool IsCmdAnyOf(const T cmd1, Ts... xs) const noexcept
    {
        if (IsCmd(cmd1))
            return true;
        if constexpr (sizeof... (xs) > 0)
            return IsCmdAnyOf(xs...);
        return false;
    }

protected:
    // map of supported RPC commands <name> -> <enum>
    using map_cmdenum_t = std::unordered_map<std::string, RPC_CMD_ENUM>;

    bool ParseCmdList(std::string &error, const char* szCmdList)
    {
        error.clear();
        if (!szCmdList || !*szCmdList)
        {
            error = "RPC command list is empty";
            return false;
        }
        m_CmdMap.clear();
        std::string sCmdList(szCmdList), sToken;
        std::istringstream tokenStream(sCmdList);
        auto nCmdNo = to_integral_type<RPC_CMD_ENUM>(RPC_CMD_ENUM::unknown);
        bool bRet = true;
        while (std::getline(tokenStream, sToken, ','))
        {
            if (++nCmdNo > size())
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
        if (nCmdNo != size())
            bRet = false;
        if (!bRet)
            error = strprintf("RPC enum mismatch [%s]", szCmdList);
        return bRet;
    }

    bool ParseParams(std::string &error)
    {
        // check if we can retrieve command from param list by index
        if (m_Params.size() <= m_nCmdIndex)
            return true; // not an error - command not passed, so help message will be returned
        const auto &cmdParam = m_Params[m_nCmdIndex];
        if (!cmdParam.isStr())
        {
            error = strprintf("RPC command parameter #%zu is not a string", m_nCmdIndex + 1);
            return false;
        }
        m_sCmd = cmdParam.get_str();
        trim(m_sCmd);
        lowercase(m_sCmd);
        if (m_sCmd.empty())
            return true;
        const auto it = m_CmdMap.find(m_sCmd);
        if (it != m_CmdMap.cend())
            m_Cmd = it->second;
        return true;
    }

    map_cmdenum_t m_CmdMap;     // map <command -> cmd_enum>
    std::string m_sCmd;         // command
    const UniValue &m_Params;   // rpc parameters
    RPC_CMD_ENUM m_Cmd;         // cmd enumeration
    size_t m_nCmdIndex;
};

// parse first command in params (UniValue array)
// 
// examples:
//     RPC_CMD_PARSER(TICKETS, params, register, find, list, get)
//     TICKETS - instance of RPCCommandParser object that parses parameters
//     register, find, ... list of supported commands. 
//     enumeration with name RPC_CMD_TICKETS is created based on this list
//     individual commands are accessible like this: RPC_CMD_TICKETS::register
// 
// special syntax for commands that contain hyphen (-):
//      find-all -> find__all
// double underscore is replaced by single hyphen in the command name
#define RPC_CMD_PARSER(command, params, ...) \
    enum class RPC_CMD_##command : uint32_t \
    { unknown = 0, __VA_ARGS__, rpc_command_count }; \
    RPCCommandParser<RPC_CMD_##command> command(params, 0, #__VA_ARGS__);

// parse second command in params (UniValue array)
// examples:
//     RPC_CMD_PARSER2(LIST, params, id, art, act, sell, buy, trade, down)
#define RPC_CMD_PARSER2(command, params, ...) \
    enum class RPC_CMD_##command : uint32_t \
    { unknown = 0, __VA_ARGS__, rpc_command_count }; \
    RPCCommandParser<RPC_CMD_##command> command(params, 1, #__VA_ARGS__);

