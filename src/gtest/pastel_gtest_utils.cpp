// Copyright (c) 2021-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <regex>
#include <random>

#include <gtest/gtest.h>

#include <utils/fs.h>
#include <rpc/client.h>
#include <rpc/server.h>
#include <chainparams.h>

#include <pastel_gtest_utils.h>

using namespace std;

int GenZero(int n)
{
    return 0;
}

int GenMax(int n)
{
    return n-1;
}

/**
 * generate random string.
 * 
 * \param nLength - string length
 * \return 
 */
string generateRandomId(const size_t nLength)
{
    static constexpr auto charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789_";

    static random_device rd;
    static mt19937 mt(rd());
    static uniform_int_distribution<int> dist(0, static_cast<int>(strlen(charset) - 1));

    string s;
    s.resize(nLength);
    for (size_t i = 0; i < nLength; ++i)
        s[i] = charset[dist(mt)];
    return s;
}

void generateRandomData(v_uint8 &v, const size_t nLength)
{
    static random_device rd;
    static mt19937_64 gen(rd());
    static uniform_int_distribution<uint64_t> dist;

    if (v.capacity() < nLength)
        v.reserve(nLength);

    v.clear();
    for (size_t i = 0; i < nLength; i += sizeof(uint64_t))
    {
        const uint64_t rand_value = dist(gen);
        const size_t nToCopy = std::min(sizeof(uint64_t), nLength - i);
        v.insert(v.end(), 
            reinterpret_cast<const uint8_t*>(&rand_value),
            reinterpret_cast<const uint8_t*>(&rand_value) + nToCopy);
    }
}

string generateRandomTxId()
{
    static random_device rd;
    static mt19937_64 gen(rd());
    static uniform_int_distribution<uint64_t> dist;

    string sTxId;
    sTxId.reserve(64);

    for (int i = 0; i < 4; ++i)
    {
        const uint64_t rand_value = dist(gen);
        for (int j = 60; j >= 0; j -= 4)
            sTxId.push_back("0123456789abcdef"[(rand_value >> j) & 0xF]);
    }
    return sTxId;
}

// generate random uint256
uint256 generateRandomUint256()
{
    return uint256S(generateRandomTxId());
}

/**
 * Generate temporary file name with extension.
 * 
 * \param szFileExt - optional extension
 * \return generated filename
 */
string generateTempFileName(const char* szFileExt)
{
    string s = generateRandomId(50);
    if (szFileExt)
        s += szFileExt;
    auto file = fs::temp_directory_path() / fs::path(s);
    return file.string();
}

// Sapling
const Consensus::Params& RegtestActivateSapling()
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    return Params().GetConsensus();
}

void RegtestDeactivateSapling()
{
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

UniValue TestCallRPC(const string& args)
{
    v_strings vArgs;

    regex pattern("\\ |\t");
    vArgs = v_strings(
                    sregex_token_iterator(args.begin(), args.end(), pattern, -1),
                    sregex_token_iterator()
                    );

    string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    // Handle empty strings the same way as CLI
    for (auto i = 0; i < vArgs.size(); i++)
    {
        if (vArgs[i] == "\"\"")
            vArgs[i] = "";
    }
    UniValue params = RPCConvertValues(strMethod, vArgs);
    EXPECT_TRUE(tableRPC[strMethod] != nullptr);
    rpcfn_type method = tableRPC[strMethod]->actor;
    try {
        UniValue result = (*method)(params, false);
        return result;
    }
    catch (const UniValue& objError) {
        throw runtime_error(find_value(objError, "message").get_str());
    }
}

UniValue TestCallRPC_Params(const std::string& sRpcMethod, const std::string& sRpcParams)
{
    return TestCallRPC(sRpcMethod + " " + sRpcParams);
}

void CheckRPCThrows(const string &sRpcMethod, const string& sRpcParams, const string &sExpectedErrorMessage)
{
    try {
        TestCallRPC(sRpcMethod + " " + sRpcParams);
        // Note: CallRPC catches (const UniValue& objError) and rethrows a runtime_error
        // "Should have caused an error";
    } catch (const runtime_error& e) {
        EXPECT_EQ(sExpectedErrorMessage, e.what());
    } catch([[maybe_unused]] const exception& e) {
        // string("Unexpected exception: ") + typeid(e).name() + ", message=\"" + e.what() + "\"";
    }
}
