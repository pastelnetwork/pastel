// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "main.h"

#include "test/test_bitcoin.h"

#include <boost/signals2/signal.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

static void TestBlockSubsidyHalvings(const Consensus::Params& consensusParams)
{
    int maxHalvings = 64;
    CAmount nInitialSubsidy = REWARD * COIN;
    
    CAmount total = 0;
    CAmount nPreviousSubsidy = nInitialSubsidy * 2; // for height == 0
    BOOST_CHECK_EQUAL(nPreviousSubsidy, nInitialSubsidy * 2);
    for (int nHalvings = 0; nHalvings < maxHalvings; nHalvings++) {
        //1002 is the first block with real subsidy
        int nHeight = 1002 + nHalvings * consensusParams.nSubsidyHalvingInterval;
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        BOOST_CHECK(nSubsidy <= nInitialSubsidy);
        BOOST_CHECK_EQUAL(nSubsidy, nPreviousSubsidy / 2);
        nPreviousSubsidy = nSubsidy;
    }
    BOOST_CHECK_EQUAL(GetBlockSubsidy(maxHalvings * consensusParams.nSubsidyHalvingInterval, consensusParams), 0);
}

static void TestBlockSubsidyHalvings(int nSubsidyHalvingInterval)
{
    Consensus::Params consensusParams;
    consensusParams.nSubsidyHalvingInterval = nSubsidyHalvingInterval;
    TestBlockSubsidyHalvings(consensusParams);
}

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    TestBlockSubsidyHalvings(Params(CBaseChainParams::Network::MAIN).GetConsensus()); // As in main
    TestBlockSubsidyHalvings(500000);
    TestBlockSubsidyHalvings(100000);
}

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    const Consensus::Params& consensusParams = Params(CBaseChainParams::Network::MAIN).GetConsensus();
    CAmount nSum = 0;
    //1002 is the first block with real subsidy
    for (int nHeight = 1002, i=1; nHeight < 56000000; nHeight += consensusParams.nSubsidyHalvingInterval, i++) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, consensusParams);
        if (nSubsidy == 0) break;
        BOOST_CHECK(nSubsidy <= REWARD * COIN);
        nSum += nSubsidy * consensusParams.nSubsidyHalvingInterval;
        // printf("%d. Height = %d; Subsidy = %lu; Sum = %lu\n", i, nHeight, nSubsidy, nSum);
        BOOST_CHECK(MoneyRange(nSum));
    }
    // Changing the block interval from 10 to 2.5 minutes causes truncation
    // effects to occur earlier (from the 9th halving interval instead of the
    // 11th), decreasing the total monetary supply by 0.0693 PSL. If the
    // transaction output field is widened, this discrepancy will become smaller
    // or disappear entirely.
    //BOOST_CHECK_EQUAL(nSum, 2099999997690000ULL);
    //BOOST_CHECK_EQUAL(nSum, 2099999990760000ULL);

    /*Pastel: 
    1. Height = 0; Subsidy = 625000000; Sum = 525000000000000
    2. Height = 840000; Subsidy = 312500000; Sum = 787500000000000
    3. Height = 1680000; Subsidy = 156250000; Sum = 918750000000000
    4. Height = 2520000; Subsidy = 78125000; Sum = 984375000000000
    5. Height = 3360000; Subsidy = 39062500; Sum = 1017187500000000
    6. Height = 4200000; Subsidy = 19531250; Sum = 1033593750000000
    7. Height = 5040000; Subsidy = 9765625; Sum = 1041796875000000
    8. Height = 5880000; Subsidy = 4882812; Sum = 1045898437080000
    9. Height = 6720000; Subsidy = 2441406; Sum = 1047949218120000
    10. Height = 7560000; Subsidy = 1220703; Sum = 1048974608640000
    11. Height = 8400000; Subsidy = 610351; Sum = 1049487303480000
    12. Height = 9240000; Subsidy = 305175; Sum = 1049743650480000
    13. Height = 10080000; Subsidy = 152587; Sum = 1049871823560000
    14. Height = 10920000; Subsidy = 76293; Sum = 1049935909680000
    15. Height = 11760000; Subsidy = 38146; Sum = 1049967952320000
    16. Height = 12600000; Subsidy = 19073; Sum = 1049983973640000
    17. Height = 13440000; Subsidy = 9536; Sum = 1049991983880000
    18. Height = 14280000; Subsidy = 4768; Sum = 1049995989000000
    19. Height = 15120000; Subsidy = 2384; Sum = 1049997991560000
    20. Height = 15960000; Subsidy = 1192; Sum = 1049998992840000
    21. Height = 16800000; Subsidy = 596; Sum = 1049999493480000
    22. Height = 17640000; Subsidy = 298; Sum = 1049999743800000
    23. Height = 18480000; Subsidy = 149; Sum = 1049999868960000
    24. Height = 19320000; Subsidy = 74; Sum = 1049999931120000
    25. Height = 20160000; Subsidy = 37; Sum = 1049999962200000
    26. Height = 21000000; Subsidy = 18; Sum = 1049999977320000
    27. Height = 21840000; Subsidy = 9; Sum = 1049999984880000
    28. Height = 22680000; Subsidy = 4; Sum = 1049999988240000
    29. Height = 23520000; Subsidy = 2; Sum = 1049999989920000
    30. Height = 24360000; Subsidy = 1; Sum = 1049999990760000
    */
    BOOST_CHECK_EQUAL(nSum, 1049999990760000ULL);
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}

BOOST_AUTO_TEST_SUITE_END()
