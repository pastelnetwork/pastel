// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>
#include <univalue.h>

#include <rpc/rpc_parser.h>
#include <rpc/rpc_consts.h>

using namespace std;
using namespace testing;

void ExpectRPCError(const UniValue& v, const int nCode, const string& sMsgSubStr)
{
    EXPECT_EQ(v.type(), UniValue::VOBJ);
    EXPECT_GE(v.size(), 2u);
    EXPECT_TRUE(v.exists(RPC_KEY_CODE));
    EXPECT_TRUE(v.exists(RPC_KEY_MESSAGE));
    if (v.size() >= 2)
    {
        EXPECT_EQ(v[0].get_int(), nCode) << "RPC error code mismatch [" << v[0].get_int() << " != " << nCode << "]";
        const string& sMsg = v[1].get_str();
        EXPECT_TRUE(str_ifind(sMsg, sMsgSubStr)) << "[" << sMsgSubStr << "] was not found in RPC error message [" << sMsg << "]";
    }
}

TEST(rpc_cmd_parser, invalid)
{
    enum class TestEnum : uint32_t
    {
        unknown = 0,
        cmd1,
        cmd2,
        rpc_command_count
    };

    UniValue params(UniValue::VARR);
    UniValue objErr;
    // test empty command list
    try
    {
        RPCCommandParser<TestEnum> tst(params, 0, "");
    } catch (const UniValue& objError) {
        objErr = objError;
    }
    ExpectRPCError(objErr, RPC_MISC_ERROR, "empty");

    // test the case when number of parameters greater than enum count
    objErr.clear();
    try
    {
        RPCCommandParser<TestEnum> tst(params, 0, "cmd1, cmd2, cmd3");
    } catch (const UniValue& objError) {
        objErr = objError;
    }
    ExpectRPCError(objErr, RPC_MISC_ERROR, "enum mismatch");
}

TEST(rpc_cmd_parser, test_known_parameter)
{
    UniValue params(UniValue::VARR);
    params.push_back("cmd2");
    RPC_CMD_PARSER(TST, params, cmd1, cmd2, cmd3, cmd__4);

    EXPECT_EQ(TST.size(), 4u);
    EXPECT_EQ(TST.cmd(), RPC_CMD_TST::cmd2);
    EXPECT_TRUE(TST.IsCmdSupported());

    EXPECT_TRUE(TST.IsCmd(RPC_CMD_TST::cmd2));
    EXPECT_FALSE(TST.IsCmd(RPC_CMD_TST::cmd3));

    EXPECT_TRUE(TST.IsCmdAnyOf(RPC_CMD_TST::cmd1, RPC_CMD_TST::cmd2, RPC_CMD_TST::cmd3));
    EXPECT_FALSE(TST.IsCmdAnyOf(RPC_CMD_TST::cmd3, RPC_CMD_TST::cmd__4));
}

TEST(rpc_cmd_parser, test_parameter_with_dash)
{
    UniValue params(UniValue::VARR);
    params.push_back("cmd-4");
    RPC_CMD_PARSER(TST, params, cmd1, cmd2, cmd3, cmd__4);

    EXPECT_EQ(TST.size(), 4u);
    EXPECT_EQ(TST.cmd(), RPC_CMD_TST::cmd__4);
    EXPECT_TRUE(TST.IsCmdSupported());

    EXPECT_TRUE(TST.IsCmd(RPC_CMD_TST::cmd__4));
    EXPECT_FALSE(TST.IsCmd(RPC_CMD_TST::cmd3));

    EXPECT_TRUE(TST.IsCmdAnyOf(RPC_CMD_TST::cmd2, RPC_CMD_TST::cmd3, RPC_CMD_TST::cmd__4));
    EXPECT_FALSE(TST.IsCmdAnyOf(RPC_CMD_TST::cmd1, RPC_CMD_TST::cmd2, RPC_CMD_TST::cmd3));
}

TEST(rpc_cmd_parser, test_unnknown_parameter)
{
    UniValue params(UniValue::VARR);
    params.push_back("cmd5");
    RPC_CMD_PARSER(TST, params, cmd1, cmd2, cmd3, cmd__4);

    EXPECT_EQ(TST.size(), 4u);
    EXPECT_EQ(TST.cmd(), RPC_CMD_TST::unknown);
    EXPECT_FALSE(TST.IsCmdSupported());

    EXPECT_TRUE(TST.IsCmd(RPC_CMD_TST::unknown));
    EXPECT_FALSE(TST.IsCmd(RPC_CMD_TST::cmd3));

    EXPECT_FALSE(TST.IsCmdAnyOf(RPC_CMD_TST::cmd1, RPC_CMD_TST::cmd2, RPC_CMD_TST::cmd3, RPC_CMD_TST::cmd__4));
}
