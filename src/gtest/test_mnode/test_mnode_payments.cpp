// Copyright (c) 2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <extlibs/scope_guard.hpp>
#include <mnode/mnode-db.h>
#include <mnode/mnode-payments.h>

#include <pastel_gtest_main.h>
#include <pastel_gtest_utils.h>

using namespace std;

TEST(mnpayments, test_cache_file)
{
    SelectParams(ChainNetwork::REGTEST);
    string sTempPath = gl_pPastelTestEnv->GenerateTempDataDir();
    auto guard = sg::make_scope_guard([&]() noexcept 
    {
        gl_pPastelTestEnv->ClearTempDataDir();
    });

    CMasternodePayments mnPayments;
    
    CMasternodePaymentVote vote1(COutPoint(generateRandomUint256(), 1), 10, CScript());
    CMasternodePaymentVote vote2(COutPoint(generateRandomUint256(), 2), 20, CScript());
    mnPayments.mapMasternodePaymentVotes[vote1.GetHash()] = vote1;
    mnPayments.mapMasternodePaymentVotes[vote2.GetHash()] = vote2;

    CMasternodePayee payee1(CScript(), generateRandomUint256());
    CMasternodePayee payee2(CScript(), generateRandomUint256());
    CMasternodeBlockPayees mnBlockPayees(10);
    mnBlockPayees.vecPayees.push_back(payee1);
    mnBlockPayees.vecPayees.push_back(payee2);
    mnPayments.mapMasternodeBlockPayees[10] = mnBlockPayees;

    CFlatDB<CMasternodePayments> flatDB(MNPAYMENTS_CACHE_FILENAME, MNPAYMENTS_CACHE_MAGIC_STR);
    EXPECT_TRUE(flatDB.Dump(mnPayments, false));

    CMasternodePayments mnPaymentsLoaded;
    EXPECT_TRUE(flatDB.Load(mnPaymentsLoaded));
    EXPECT_EQ(mnPaymentsLoaded.mapMasternodePaymentVotes.size(), 2);
    EXPECT_EQ(mnPaymentsLoaded.mapMasternodeBlockPayees.size(), 1);
    EXPECT_EQ(mnPaymentsLoaded.mapMasternodeBlockPayees[10].vecPayees.size(), 2);
    EXPECT_EQ(mnPaymentsLoaded.mapMasternodeBlockPayees[10].vecPayees[0].GetVoteCount(), 1);
    EXPECT_EQ(mnPaymentsLoaded.mapMasternodeBlockPayees[10].vecPayees[1].GetVoteCount(), 1);
}
