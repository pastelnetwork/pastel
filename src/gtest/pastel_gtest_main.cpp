// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gmock/gmock.h>
#include <univalue.h>
#include <libsnark/common/default_types/r1cs_ppzksnark_pp.hpp>
#include <libsnark/zk_proof_systems/ppzksnark/r1cs_ppzksnark/r1cs_ppzksnark.hpp>
#include "librustzcash.h"

#include <rpc/server.h>
#include <rpc/register.h>
#include <crypto/common.h>
#include <key.h>
#include <pubkey.h>
#include <util.h>
#include <metrics.h>
#include <orphan-tx.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

#include <pastel_gtest_main.h>
#include <pastel_gtest_utils.h>

CPastelTest_Environment *gl_pPastelTestEnv = nullptr;

#ifdef ENABLE_WALLET
extern CWallet* pwalletMain;
#endif // ENABLE_WALLET

#ifdef ENABLE_MINING
extern UniValue generate(const UniValue& params, bool fHelp);
#endif

using namespace std;

// Once this function has returned false, it must remain false.
static atomic_bool latchToFalse{false};

// resettable version of IsInitialBlockDownload
bool TestIsInitialBlockDownload(const Consensus::Params& consensusParams)
{
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
        return true;
    if (!chainActive.Tip())
        return true;
    if (chainActive.Tip()->nChainWork < UintToArith256(consensusParams.nMinimumChainWork))
        return true;
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
        return true;
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, memory_order_relaxed);
    return false;
}

void init_zksnark_params()
{
    static bool gl_bZkSnarkParamsInitialized = false;
    if (gl_bZkSnarkParamsInitialized)
        return;
    gl_bZkSnarkParamsInitialized = true;

    libsnark::default_r1cs_ppzksnark_pp::init_public_params();
    libsnark::inhibit_profiling_info = true;
    libsnark::inhibit_profiling_counters = true;

    fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    ASSERT_EQ(sizeof(fs::path::value_type), sizeof(codeunit)) << "librustzcash not configured correctly";

    auto sapling_spend_str = sapling_spend.native();
    auto sapling_output_str = sapling_output.native();
    auto sprout_groth16_str = sprout_groth16.native();

    librustzcash_init_zksnark_params(
        reinterpret_cast<const codeunit*>(sapling_spend_str.c_str()),
        sapling_spend_str.length(),
        "8270785a1a0d0bc77196f000ee6d221c9c9894f55307bd9357c3f0105d31ca63991ab91324160d8f53e2bbd3c2633a6eb8bdf5205d822e7f3f73edac51b2b70c",
        reinterpret_cast<const codeunit*>(sapling_output_str.c_str()),
        sapling_output_str.length(),
        "657e3d38dbb5cb5e7dd2970e8b03d69b4787dd907285b5a7f0790dcc8072f60bf593b32cc2d1c030e00ff5ae64bf84c5c3beb84ddc841d48264b4a171744d028",
        reinterpret_cast<const codeunit*>(sprout_groth16_str.c_str()),
        sprout_groth16_str.length(),
        "e9b238411bd6c0ec4791e9d04245ec350c9c5744f5610dfcce4365d5ca49dfefd5054e371842b3f88fa1b9d7e8e075249b3ebabd167fa8b0f3161292d36c180a");

    if (!gl_pOrphanTxManager)
        gl_pOrphanTxManager = make_unique<COrphanTxManager>();
}

void CPastelTest_Environment::SetUp()
{
    ASSERT_EQ(init_and_check_sodium(), 0);

    fnIsInitialBlockDownload = TestIsInitialBlockDownload;

    SetupEnvironment();
    SetupNetworking();

    RegisterAllCoreRPCCommands(tableRPC);
}

void CPastelTest_Environment::TearDown()
{
}

void CPastelTest_Environment::generate_coins(const size_t N)
{
#ifdef ENABLE_MINING
    UniValue params(UniValue::VARR);
    params.push_back(static_cast<uint64_t>(N));
    generate(params, false);
#else
    return;
#endif
}

/**
 * Generate unique temp directory and set it as a datadir.
 * \return temp datadir
 */
string CPastelTest_Environment::GenerateTempDataDir()
{
    // cleanup existing data directory
    ClearTempDataDir();

    // generate temporary unique datadir
    const auto sTempDataDir = generateTempFileName(nullptr);
    m_TempDataDir = fs::path(sTempDataDir);
    if (!fs::exists(m_TempDataDir))
        fs::create_directories(m_TempDataDir);

    SetTempDataDir(sTempDataDir);
    return sTempDataDir;
}

void CPastelTest_Environment::SetTempDataDir(const string& sDataDir)
{
    // clear cached data dirs
    ClearDatadirCache();
    mapArgs["-datadir"] = sDataDir;
}

/**
 * Cleanup temporary datadir.
 */
void CPastelTest_Environment::ClearTempDataDir()
{
    if (!fs::exists(m_TempDataDir))
        return;
    fs::remove_all(m_TempDataDir);
}

/**
 * Initialize given network for unittests.
 * 
 * \param network - main, regtest or testnet
 * \return if true - regtest initialized successfully
 */
void CPastelTest_Environment::InitializeChainTest(const ChainNetwork network)
{
    if (m_TestNetwork.has_value())
    {
        if (m_TestNetwork.value() == network)
            return; // this test network is already initialized
        FinalizeChainTest();
    }
    init_zksnark_params();

    SelectParams(network);
    const auto sTempDataDir = GenerateTempDataDir();
    ASSERT_TRUE(fs::exists(m_TempDataDir)) << "Failed to initialize temporary datadir [" << sTempDataDir << "] for regtest network unittests";

    ASSERT_EQ(pblocktree, nullptr);
    pblocktree = new CBlockTreeDB(1 << 20, true);
    pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    ASSERT_TRUE(InitBlockIndex(Params())) << "Failed to initialize block index for regtest network unittests";

#ifdef ENABLE_WALLET
    bitdb.MakeMock();

    ASSERT_EQ(pwalletMain, nullptr);
    pwalletMain = new CWallet("test_wallet.dat");
    bool bFirstRun = true;
    pwalletMain->LoadWallet(bFirstRun);
    static bool bWalletRPCInitialized = false;
    if (!bWalletRPCInitialized)
    {
        RegisterWalletRPCCommands(tableRPC);
        bWalletRPCInitialized = true;
    }
    RegisterValidationInterface(pwalletMain);
#endif // ENABLE_WALLET
    gl_ScriptCheckManager.SetThreadCount(3);
    gl_ScriptCheckManager.create_workers(threadGroup);
    RegisterNodeSignals(GetNodeSignals());

    m_TestNetwork = network;
}

void CPastelTest_Environment::InitializeRegTest()
{
    InitializeChainTest(ChainNetwork::REGTEST);
}

void CPastelTest_Environment::FinalizeChainTest()
{
    UnregisterNodeSignals(GetNodeSignals());
    threadGroup.stop_all();
    threadGroup.join_all();
#ifdef ENABLE_WALLET
    if (pwalletMain)
    {
        UnregisterValidationInterface(pwalletMain);
        delete pwalletMain;
        pwalletMain = nullptr;
    }
#endif // ENABLE_WALLET
    UnloadBlockIndex();
    if (pcoinsTip)
    {
        delete pcoinsTip;
        pcoinsTip = nullptr;
    }
    if (pcoinsdbview)
    {
        delete pcoinsdbview;
        pcoinsdbview = nullptr;
    }
    if (pblocktree)
    {
        delete pblocktree;
        pblocktree = nullptr;
    }
    // reset TestIsInitialBlockDownload
    latchToFalse.store(false, memory_order_relaxed);
#ifdef ENABLE_WALLET
    bitdb.Flush(true);
    bitdb.Reset();
#endif
    ClearMetrics();
    ClearTempDataDir();
    m_TestNetwork.reset();
}

int main(int argc, char **argv)
{
    // supported command-line options
    /*
    * Google Test supports the following command-line parameters:
        --gtest_list_tests
        --gtest_filter=<test string> - use google test filter, where
           <test string> is a serier of wildcard patterns separated by colons (:).
           examples:
             --gtest_filter=*
             --gtest_filter=test1*:PTest
             --gtest_filter=-test2*:test3*
        --gtest_repeat=N - repeat test N times
        --gtest_break_on_failure
        --gtest_throw_on_failure
        --gtest_catch_exceptions=0
        --gtest_output="(xml|json):<filename>"
        --gtest_color=(yes|no|auto)

    */
    testing::InitGoogleMock(&argc, argv);
  
    gl_pPastelTestEnv = new CPastelTest_Environment();
    if (!gl_pPastelTestEnv)
    {
        cerr << "Failed to create Pastel test environment";
        return 2;
    }
    ::testing::AddGlobalTestEnvironment(gl_pPastelTestEnv);
    init_zksnark_params();

    auto ret = RUN_ALL_TESTS();
    return ret;
}

