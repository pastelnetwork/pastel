// Copyright (c) 2018-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <tuple>
#include <gtest/gtest.h>
#include <scope_guard.hpp>

#include <pastelid/pastel_key.h>
#include <fs.h>
#include <util.h>
#include <chainparams.h>
#include <pastel_gtest_utils.h>
#include <pastel_gtest_main.h>

using namespace std;
using namespace testing;

constexpr auto TEST_PASS1 = "passphrase1";
constexpr auto TEST_PASS2 = "passphrase2";

class PTest_PastelID_Alg : public TestWithParam<tuple<string, CPastelID::SIGN_ALGORITHM>>
{};

TEST_P(PTest_PastelID_Alg, GetAlgorithmByName)
{
    auto &Param = GetParam();
    const auto alg = CPastelID::GetAlgorithmByName(get<0>(Param));
    EXPECT_EQ(alg, get<1>(Param));
}

INSTANTIATE_TEST_SUITE_P(PastelID, PTest_PastelID_Alg,
    Values(
        make_tuple("", CPastelID::SIGN_ALGORITHM::ed448),
        make_tuple(SIGN_ALG_ED448, CPastelID::SIGN_ALGORITHM::ed448),
        make_tuple(SIGN_ALG_LEGROAST, CPastelID::SIGN_ALGORITHM::legroast),
        make_tuple("myalg", CPastelID::SIGN_ALGORITHM::not_defined)
    ));

TEST(PastelID, GetStoredPastelIDs)
{
    SelectParams(ChainNetwork::REGTEST);

    string sTempPath = gl_pPastelTestEnv->GenerateTempDataDir();
    auto guard = sg::make_scope_guard([&]() noexcept 
    {
        gl_pPastelTestEnv->ClearTempDataDir();
    });

    auto mapIDs = CPastelID::GetStoredPastelIDs(true);
    EXPECT_TRUE(mapIDs.empty()) << "Found some Pastel IDs in [" << sTempPath << "]";

    const auto mapIDs_1 = CPastelID::CreateNewPastelKeys(TEST_PASS1);
    EXPECT_TRUE(!mapIDs_1.empty());
    const auto it = mapIDs_1.cbegin();
    EXPECT_NE(it, mapIDs_1.cend());

    if (it != mapIDs_1.cend())
    {
        mapIDs = CPastelID::GetStoredPastelIDs(false);
        const auto it1 = mapIDs.find(it->first);
        EXPECT_NE(it1, mapIDs.cend());
        EXPECT_EQ(it1->second, it->second);
    }
    const auto mapIDs_2 = CPastelID::CreateNewPastelKeys(TEST_PASS2);
    EXPECT_TRUE(!mapIDs_2.empty());
    const auto it2 = mapIDs_2.cbegin();
    EXPECT_NE(it2, mapIDs_2.cend());
    if (it2 != mapIDs_2.cend())
    {
        string sPastelID = it2->first;
        // get full list of pastelids - should be 2
        mapIDs = CPastelID::GetStoredPastelIDs(false);
        EXPECT_EQ(mapIDs.size(), 2u);

        // get filtered pastelid
        mapIDs = CPastelID::GetStoredPastelIDs(false, sPastelID);
        EXPECT_EQ(mapIDs.size(), 1u);
        const auto it3 = mapIDs.find(sPastelID);
        EXPECT_NE(it3, mapIDs.cend());

        // legroast pubkeys match
        if (it3 != mapIDs.cend())
            EXPECT_EQ(it3->second, it2->second);
    }
}
