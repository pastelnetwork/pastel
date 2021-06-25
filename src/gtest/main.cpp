#include "gmock/gmock.h"
#include "crypto/common.h"
#include "key.h"
#include "pubkey.h"
#include "zcash/JoinSplit.hpp"
#include "util.h"

#include <libsnark/common/default_types/r1cs_ppzksnark_pp.hpp>
#include <libsnark/zk_proof_systems/ppzksnark/r1cs_ppzksnark/r1cs_ppzksnark.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include "librustzcash.h"

struct ECCryptoClosure
{
    ECCVerifyHandle handle;
};

ECCryptoClosure instance_of_eccryptoclosure;

ZCJoinSplit* params;

int main(int argc, char **argv)
{
  assert(init_and_check_sodium() != -1);
  ECC_Start();
  
  libsnark::default_r1cs_ppzksnark_pp::init_public_params();
  libsnark::inhibit_profiling_info = true;
  libsnark::inhibit_profiling_counters = true;
  fs::path pk_path = ZC_GetParamsDir() / "sprout-proving.key";
  fs::path vk_path = ZC_GetParamsDir() / "sprout-verifying.key";
  params = ZCJoinSplit::Prepared(vk_path.string(), pk_path.string());

  fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
  fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
  fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    static_assert(
        sizeof(fs::path::value_type) == sizeof(codeunit),
        "librustzcash not configured correctly");
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
        "e9b238411bd6c0ec4791e9d04245ec350c9c5744f5610dfcce4365d5ca49dfefd5054e371842b3f88fa1b9d7e8e075249b3ebabd167fa8b0f3161292d36c180a"
    );

    // supported command-line options
    constexpr auto CMDLINE_PARAM_HELP = "help";
    constexpr auto CMDLINE_PARAM_FILTER = "filter";

    // parse command-line options:
    //   --filter=MyTest* - this can be used to debug specific tests by name
    //   --filter=*include_tests*:-exclude_tests
    namespace po = boost::program_options;
    po::options_description cmdline_options_desc("Pastel Core Google Test command-line options");
    cmdline_options_desc.add_options()
        (CMDLINE_PARAM_HELP, "show help message")
        (CMDLINE_PARAM_FILTER, po::value<std::string>(), "use filter to execute specific Google tests only, supports *, exclusions with -")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, cmdline_options_desc), vm);
    po::notify(vm);

    if (vm.count(CMDLINE_PARAM_HELP))
    {
        std::cout << cmdline_options_desc << std::endl;
        return 1;
    }
    std::string sGoogleTestFilter;
    if (vm.count(CMDLINE_PARAM_FILTER))
        sGoogleTestFilter = vm[CMDLINE_PARAM_FILTER].as<std::string>();

    if (!sGoogleTestFilter.empty())
        ::testing::GTEST_FLAG(filter) = sGoogleTestFilter.c_str();
    testing::InitGoogleMock(&argc, argv);
  
    auto ret = RUN_ALL_TESTS();

    ECC_Stop();
    return ret;
}
