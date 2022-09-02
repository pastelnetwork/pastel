// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for denial-of-service detection/prevention code
//

#include <stdint.h>
#include <gtest/gtest.h>

#include <consensus/upgrades.h>
#include <keystore.h>
#include <main.h>
#include <net.h>
#include <pow.h>
#include <script/sign.h>
#include <serialize.h>
#include <util.h>
#include <orphan-tx.h>

#include <pastel_gtest_main.h>

using namespace std;
using namespace testing;

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

class TestDoS : public Test
{
public:
    
    static void SetUpTestSuite()
    {
        gl_pPastelTestEnv->InitializeRegTest();
    }

    static void TearDownTestSuite()
    {
        gl_pPastelTestEnv->FinalizeRegTest();
    }
};

TEST_F(TestDoS, DoS_banning)
{
    CNode::ClearBanned();
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100); // Should get banned
    SendMessages(Params(), &dummyNode1, false);
    EXPECT_TRUE( CNode::IsBanned(addr1) );
    EXPECT_TRUE( !CNode::IsBanned(ip(0xa0b0c001|0x0000ff00)) ); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002));
    CNode dummyNode2(INVALID_SOCKET, addr2, "", true);
    dummyNode2.nVersion = 1;
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(Params(), & dummyNode2, false);
    EXPECT_TRUE( !CNode::IsBanned(addr2) ); // 2 not banned yet...
    EXPECT_TRUE( CNode::IsBanned(addr1) );  // ... but 1 still should be
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(Params(), &dummyNode2, false);
    EXPECT_TRUE( CNode::IsBanned(addr2) );
}

TEST_F(TestDoS, DoS_banscore)
{
    CNode::ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100);
    SendMessages(Params(), &dummyNode1, false);
    EXPECT_TRUE(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 10);
    SendMessages(Params(), &dummyNode1, false);
    EXPECT_TRUE(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 1);
    SendMessages(Params(), &dummyNode1, false);
    EXPECT_TRUE(CNode::IsBanned(addr1));
    mapArgs.erase("-banscore");
}

TEST_F(TestDoS, DoS_bantime)
{
    CNode::ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = 1;

    Misbehaving(dummyNode.GetId(), 100);
    SendMessages(Params(), &dummyNode, false);
    EXPECT_TRUE(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60);
    EXPECT_TRUE(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60*24+1);
    EXPECT_TRUE(!CNode::IsBanned(addr));
}

CTransaction RandomOrphan()
{
    return gl_pOrphanTxManager->getTxOrFirst(GetRandHash());
}

class PTestDoS : public TestWithParam<int>
{};

// Parameterized testing over consensus branch ids
TEST_P(PTestDoS, DoS_mapOrphans)
{
    const int sample = GetParam();
    EXPECT_LT(sample, static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

        gl_pOrphanTxManager->AddOrphanTx(tx, i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev.GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        SignSignature(keystore, txPrev, tx, 0, to_integral_type(SIGHASH::ALL), consensusBranchId);

        gl_pOrphanTxManager->AddOrphanTx(tx, i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 10; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev.GetHash();
        }
        SignSignature(keystore, txPrev, tx, 0, to_integral_type(SIGHASH::ALL), consensusBranchId);
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        EXPECT_TRUE(!gl_pOrphanTxManager->AddOrphanTx(tx, i));
    }

    // Test EraseOrphansFor:
    for (NodeId i = 0; i < 3; i++)
    {
        const size_t sizeBefore = gl_pOrphanTxManager->size();
        gl_pOrphanTxManager->EraseOrphansFor(i);
        EXPECT_LT(gl_pOrphanTxManager->size(), sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    gl_pOrphanTxManager->LimitOrphanTxSize(40);
    EXPECT_TRUE(gl_pOrphanTxManager->size() <= 40);

    gl_pOrphanTxManager->LimitOrphanTxSize(10);
    EXPECT_TRUE(gl_pOrphanTxManager->size() <= 10);

    gl_pOrphanTxManager->LimitOrphanTxSize(0);
    EXPECT_EQ(gl_pOrphanTxManager->size(), 0u);
    EXPECT_EQ(gl_pOrphanTxManager->sizePrev(), 0u);
}

INSTANTIATE_TEST_SUITE_P(DoS_mapOrphans, PTestDoS, Values(
    0,1,2,3
));