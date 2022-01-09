// Copyright (c) 2021 The Pastel developers
#include "gmock/gmock.h"
#include "univalue.h"
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>

#include "rpc/server.h"
#include "rpc/register.h"
#include "crypto/common.h"
#include "key.h"
#include "pubkey.h"
#include "util.h"
#include "metrics.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include <libsnark/common/default_types/r1cs_ppzksnark_pp.hpp>
#include <libsnark/zk_proof_systems/ppzksnark/r1cs_ppzksnark/r1cs_ppzksnark.hpp>
#include "librustzcash.h"
#include "pastel_gtest_main.h"

struct ECCryptoClosure
{
    ECCVerifyHandle handle;
};

ECCryptoClosure instance_of_eccryptoclosure;
CPastelTest_Environment *gl_pPastelTestEnv = nullptr;

#ifdef ENABLE_WALLET
extern CWallet* pwalletMain;
#endif // ENABLE_WALLET

#ifdef ENABLE_MINING
extern UniValue generate(const UniValue& params, bool fHelp);
#endif

// Once this function has returned false, it must remain false.
static std::atomic<bool> latchToFalse{false};

// resettable version of IsInitialBlockDownload
bool TestIsInitialBlockDownload(const Consensus::Params& consensusParams)
{
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(std::memory_order_relaxed))
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
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

void CPastelTest_Environment::SetUp()
{
    ASSERT_EQ(init_and_check_sodium(), 0);
    ECC_Start();

    libsnark::default_r1cs_ppzksnark_pp::init_public_params();
    libsnark::inhibit_profiling_info = true;
    libsnark::inhibit_profiling_counters = true;

    fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    ASSERT_EQ(sizeof(fs::path::value_type), sizeof(codeunit))  << "librustzcash not configured correctly";

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

    fnIsInitialBlockDownload = TestIsInitialBlockDownload;

    SetupNetworking();
}

void CPastelTest_Environment::TearDown()
{
    ECC_Stop();
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

void CPastelTest_Environment::SetupTesting()
{
    fPrintToDebugLog = false; // don't want to write to debug.log file
    fCheckBlockIndex = true;
    SelectParams(CBaseChainParams::Network::MAIN);

    libsnark::default_r1cs_ppzksnark_pp::init_public_params();
    libsnark::inhibit_profiling_info = true;
    libsnark::inhibit_profiling_counters = true;

    fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    ASSERT_EQ(sizeof(fs::path::value_type), sizeof(codeunit))  << "librustzcash not configured correctly";

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
    
    fnIsInitialBlockDownload = TestIsInitialBlockDownload;

    RegisterAllCoreRPCCommands(tableRPC);
    
#ifdef ENABLE_WALLET
        RegisterWalletRPCCommands(tableRPC);
#endif
    
    pblocktree = new CBlockTreeDB(1 << 20, true);
    pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    InitBlockIndex(Params());
#ifdef ENABLE_WALLET
    ASSERT_EQ(pwalletMain, nullptr);
    pwalletMain = new CWallet("test_wallet.dat");
    bool bFirstRun = true;
    pwalletMain->LoadWallet(bFirstRun);
    RegisterValidationInterface(pwalletMain);
#endif // ENABLE_WALLET
    nScriptCheckThreads = 3;
    for (int i = 0; i < nScriptCheckThreads - 1; i++)
        threadGroup.create_thread(&ThreadScriptCheck);
    RegisterNodeSignals(GetNodeSignals());
}

void CPastelTest_Environment::FinalizeSetupTesting()
{
    UnregisterNodeSignals(GetNodeSignals());
    threadGroup.interrupt_all();
    threadGroup.join_all();
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
    latchToFalse.store(false, std::memory_order_relaxed);
    ClearMetrics();
}

void CPastelTest_Environment::InitializeRegTest()
{
    SelectParams(CBaseChainParams::Network::REGTEST);

    ASSERT_EQ(pblocktree, nullptr);
    pblocktree = new CBlockTreeDB(1 << 20, true);
    pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    InitBlockIndex(Params());

#ifdef ENABLE_WALLET
    ASSERT_EQ(pwalletMain, nullptr);
    pwalletMain = new CWallet("test_wallet.dat");
    bool bFirstRun = true;
    pwalletMain->LoadWallet(bFirstRun);
    RegisterValidationInterface(pwalletMain);
#endif // ENABLE_WALLET
    nScriptCheckThreads = 3;
    for (int i = 0; i < nScriptCheckThreads - 1; i++)
        threadGroup.create_thread(&ThreadScriptCheck);
    RegisterNodeSignals(GetNodeSignals());
}

void CPastelTest_Environment::FinalizeRegTest()
{
    UnregisterNodeSignals(GetNodeSignals());
    threadGroup.interrupt_all();
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
    latchToFalse.store(false, std::memory_order_relaxed);
    ClearMetrics();
}

int main(int argc, char **argv)
{
    // supported command-line options
    constexpr auto CMDLINE_PARAM_HELP = "help";
    constexpr auto CMDLINE_PARAM_FILTER = "filter";

    // parse command-line options:
    //   --filter=MyTest* - this can be used to debug specific tests by name
    //   --filter=*include_tests*:-exclude_tests
    namespace po = boost::program_options;
    po::options_description cmdline_options_desc("Pastel Core Google Test command-line options");
    cmdline_options_desc.add_options()(CMDLINE_PARAM_HELP, "show help message")(CMDLINE_PARAM_FILTER, po::value<std::string>(), "use filter to execute specific Google tests only, supports *, exclusions with -");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, cmdline_options_desc), vm);
    po::notify(vm);

    if (vm.count(CMDLINE_PARAM_HELP)) {
        std::cout << cmdline_options_desc << std::endl;
        return 1;
    }
    std::string sGoogleTestFilter;
    if (vm.count(CMDLINE_PARAM_FILTER))
        sGoogleTestFilter = vm[CMDLINE_PARAM_FILTER].as<std::string>();

    if (!sGoogleTestFilter.empty())
        ::testing::GTEST_FLAG(filter) = sGoogleTestFilter.c_str();
    testing::InitGoogleMock(&argc, argv);
  
    gl_pPastelTestEnv = new CPastelTest_Environment();
    if (!gl_pPastelTestEnv)
    {
        std::cerr << "Failed to create Pastel test environment";
        return 2;
    }
    ::testing::AddGlobalTestEnvironment(gl_pPastelTestEnv);

    auto ret = RUN_ALL_TESTS();
    return ret;
}

