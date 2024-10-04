// Copyright (c) 2023-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <extlibs/scope_guard.hpp>
#include <mnode/mnode-db.h>
#include <mnode/mnode-consts.h>
#include <mnode/mnode-payments.h>
#include <mnode/mnode-manager.h>

#include <pastel_gtest_main.h>
#include <pastel_gtest_utils.h>

using namespace std;
using namespace testing;

class TestMNodeCache : public Test
{
public:
    TestMNodeCache() = default;

    static void SetUpTestCase()
    {
        SelectParams(ChainNetwork::REGTEST);
        gl_pPastelTestEnv->GenerateTempDataDir();
    }

    static void TearDownTestCase()
    {
        gl_pPastelTestEnv->ClearTempDataDir();
    }
};

TEST_F(TestMNodeCache, payments)
{
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

TEST_F(TestMNodeCache, mnode_manager)
{
    CMasternodeMan mnMgr;

    // generate random masternodes
    vector<masternode_info_t> vTestMN;
    time_t nNow = time(nullptr);
    size_t nEnabledCount = 0;
    for (int i = 0; i < 100; ++i)
    {
        v_uint8 v1, v2;
        generateRandomData(v1, CPubKey::PUBLIC_KEY_SIZE);
        generateRandomData(v2, CPubKey::PUBLIC_KEY_SIZE);
        CPubKey pkCollAddr(v1);
        CPubKey pkMN(v2);

        MASTERNODE_STATE state = static_cast<MASTERNODE_STATE>(i % to_integral_type(MASTERNODE_STATE::COUNT));
        if (state == MASTERNODE_STATE::ENABLED)
            ++nEnabledCount;
        masternode_info_t mnInfo(
            state, PROTOCOL_VERSION,
            nNow - i * 60,
            COutPoint(generateRandomUint256(), i),
            CService("127.0.0.1", i * 60 + 1),
            pkCollAddr, pkMN,
            "extAddress" + to_string(i), "extP2P" + to_string(i), "extCfg" + to_string(i),
            nNow + i * 60, i % 2 == 0);

        vTestMN.push_back(mnInfo);
        masternode_t pmn = make_shared<CMasternode>(mnInfo);
        EXPECT_TRUE(mnMgr.Add(pmn));
    }

    CFlatDB<CMasternodeMan> flatDB(MNCACHE_FILENAME, MNCACHE_CACHE_MAGIC_STR);
    EXPECT_TRUE(flatDB.Dump(mnMgr, false));

    CMasternodeMan mnMgrLoaded;
    EXPECT_TRUE(flatDB.Load(mnMgrLoaded));
    EXPECT_EQ(mnMgrLoaded.CountByProtocol(PROTOCOL_VERSION), 100);
    EXPECT_EQ(mnMgrLoaded.CountEnabled(), nEnabledCount);

    for (const auto& mnInfo : vTestMN)
    {
        masternode_t pmn = mnMgrLoaded.Get(false, mnInfo.getOutPoint());
        EXPECT_TRUE(pmn);
        EXPECT_EQ(pmn->GetActiveState(), mnInfo.GetActiveState());
        EXPECT_EQ(pmn->nProtocolVersion, mnInfo.nProtocolVersion);
        EXPECT_EQ(pmn->sigTime, mnInfo.sigTime);
        EXPECT_EQ(pmn->pubKeyCollateralAddress, mnInfo.pubKeyCollateralAddress);
        EXPECT_EQ(pmn->pubKeyMasternode, mnInfo.pubKeyMasternode);
        EXPECT_EQ(pmn->strExtraLayerAddress, mnInfo.strExtraLayerAddress);
        EXPECT_EQ(pmn->strExtraLayerCfg, mnInfo.strExtraLayerCfg);
        EXPECT_EQ(pmn->strExtraLayerP2P, mnInfo.strExtraLayerP2P);
        EXPECT_EQ(pmn->nTimeLastWatchdogVote, mnInfo.nTimeLastWatchdogVote);
        EXPECT_EQ(pmn->IsEligibleForMining(), mnInfo.IsEligibleForMining());
    }
}