// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/upgrades.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/interpreter.h"
#include "script/sign.h"
#include "uint256.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet_ismine.h"
#endif

#include <gtest/gtest.h>

using namespace std;
using namespace testing;


CScript
sign_multisig(CScript scriptPubKey, vector<CKey> keys, CTransaction transaction, int whichIn, uint32_t consensusBranchId)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, whichIn, to_integral_type(SIGHASH::ALL), 0, consensusBranchId);

    CScript result;
    result << OP_0; // CHECKMULTISIG bug workaround
    for (const auto &key : keys)
    {
        v_uint8 vchSig;
        EXPECT_TRUE(key.Sign(hash, vchSig));
        vchSig.push_back(to_integral_type(SIGHASH::ALL));
        result << vchSig;
    }
    return result;
}

class PTest_Multisig : public TestWithParam<int>
{};

// Parameterized testing over consensus branch ids
TEST_P(PTest_Multisig, multisig_verify)
{
    const int sample = GetParam();

    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

    ScriptError err;
    CKey key[4];
    CAmount amount = 0;
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;  // Funding transaction
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    CMutableTransaction txTo[3]; // Spending transaction
    for (int i = 0; i < 3; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    vector<CKey> keys;
    CScript s;

    // Test a AND b:
    keys.assign(1,key[0]);
    keys.push_back(key[1]);
    s = sign_multisig(a_and_b, keys, txTo[0], 0, consensusBranchId);
    EXPECT_TRUE(VerifyScript(s, a_and_b, flags, MutableTransactionSignatureChecker(&txTo[0], 0, amount), consensusBranchId, &err));
    EXPECT_EQ(err, SCRIPT_ERR_OK) << ScriptErrorString(err);

    for (int i = 0; i < 4; i++)
    {
        keys.assign(1,key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0, consensusBranchId);
        EXPECT_TRUE(!VerifyScript(s, a_and_b, flags, MutableTransactionSignatureChecker(&txTo[0], 0, amount), consensusBranchId, &err)) << strprintf("a&b 1: %d", i);
        EXPECT_EQ(err, SCRIPT_ERR_INVALID_STACK_OPERATION) << ScriptErrorString(err);

        keys.assign(1,key[1]);
        keys.push_back(key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0, consensusBranchId);
        EXPECT_TRUE(!VerifyScript(s, a_and_b, flags, MutableTransactionSignatureChecker(&txTo[0], 0, amount), consensusBranchId, &err))<<strprintf("a&b 2: %d", i);
        EXPECT_EQ(err, SCRIPT_ERR_EVAL_FALSE)<< ScriptErrorString(err);
    }

    // Test a OR b:
    for (int i = 0; i < 4; i++)
    {
        keys.assign(1,key[i]);
        s = sign_multisig(a_or_b, keys, txTo[1], 0, consensusBranchId);
        if (i == 0 || i == 1)
        {
            EXPECT_TRUE(VerifyScript(s, a_or_b, flags, MutableTransactionSignatureChecker(&txTo[1], 0, amount), consensusBranchId, &err))<<strprintf("a|b: %d", i);
            EXPECT_EQ(err , SCRIPT_ERR_OK)<<ScriptErrorString(err);
        }
        else
        {
            EXPECT_TRUE(!VerifyScript(s, a_or_b, flags, MutableTransactionSignatureChecker(&txTo[1], 0, amount), consensusBranchId, &err))<<strprintf("a|b: %d", i);
            EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE)<<ScriptErrorString(err);
        }
    }
    s.clear();
    s << OP_0 << OP_1;
    EXPECT_TRUE(!VerifyScript(s, a_or_b, flags, MutableTransactionSignatureChecker(&txTo[1], 0, amount), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_SIG_DER)<<ScriptErrorString(err);


    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            keys.assign(1,key[i]);
            keys.push_back(key[j]);
            s = sign_multisig(escrow, keys, txTo[2], 0, consensusBranchId);
            if (i < j && i < 3 && j < 3)
            {
                EXPECT_TRUE(VerifyScript(s, escrow, flags, MutableTransactionSignatureChecker(&txTo[2], 0, amount), consensusBranchId, &err))<<strprintf("escrow 1: %d %d", i, j);
                EXPECT_EQ(err , SCRIPT_ERR_OK)<<ScriptErrorString(err);
            }
            else
            {
                EXPECT_TRUE(!VerifyScript(s, escrow, flags, MutableTransactionSignatureChecker(&txTo[2], 0, amount), consensusBranchId, &err))<<strprintf("escrow 2: %d %d", i, j);
                EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE)<<ScriptErrorString(err);
            }
        }
}

TEST_P(PTest_Multisig, multisig_Sign)
{
    const int sample = GetParam();

    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;  // Funding transaction
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[2].scriptPubKey = escrow;

    CMutableTransaction txTo[3]; // Spending transaction
    for (int i = 0; i < 3; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    for (int i = 0; i < 3; i++)
    {
        EXPECT_TRUE(SignSignature(keystore, txFrom, txTo[i], 0, to_integral_type(SIGHASH::ALL), consensusBranchId))<< strprintf("SignSignature %d", i);
    }
}

INSTANTIATE_TEST_SUITE_P(multisig, PTest_Multisig, Values(
    0,1,2,3
));

TEST(PTest_Multisig, multisig_IsStandard)
{
    CKey key[4];
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    txnouttype whichType;

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(a_and_b, whichType));

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(a_or_b, whichType));

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(escrow, whichType));

    CScript one_of_four;
    one_of_four << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << ToByteVector(key[3].GetPubKey()) << OP_4 << OP_CHECKMULTISIG;
    EXPECT_TRUE(!::IsStandard(one_of_four, whichType));

    CScript malformed[6];
    malformed[0] << OP_3 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[1] << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    malformed[2] << OP_0 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[3] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_0 << OP_CHECKMULTISIG;
    malformed[4] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_CHECKMULTISIG;
    malformed[5] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey());

    for (int i = 0; i < 6; i++)
        EXPECT_TRUE(!::IsStandard(malformed[i], whichType));
}

TEST(PTest_Multisig, multisig_Solver1)
{
    // Tests Solver() that returns lists of keys that are
    // required to satisfy a ScriptPubKey
    //
    // Also tests IsMine() and ExtractDestination()
    //
    // Note: ExtractDestination for the multisignature transactions
    // always returns false for this release, even if you have
    // one key that would satisfy an (a|b) or 2-of-3 keys needed
    // to spend an escrow transaction.
    //
    CBasicKeyStore keystore, emptykeystore, partialkeystore;
    CKey key[3];
    CTxDestination keyaddr[3];
    for (int i = 0; i < 3; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
        keyaddr[i] = key[i].GetPubKey().GetID();
    }
    partialkeystore.AddKey(key[0]);

    {
        vector<v_uint8> solutions;
        txnouttype whichType;
        CScript s;
        s << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
        EXPECT_TRUE(Solver(s, whichType, solutions));
        EXPECT_EQ(solutions.size() , 1U);
        CTxDestination addr;
        EXPECT_TRUE(ExtractDestination(s, addr));
        EXPECT_EQ(addr , keyaddr[0]);
#ifdef ENABLE_WALLET
        EXPECT_TRUE(IsMine(keystore, s));
        EXPECT_TRUE(!IsMine(emptykeystore, s));
#endif
    }
    {
        vector<v_uint8> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_DUP << OP_HASH160 << ToByteVector(key[0].GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        EXPECT_TRUE(Solver(s, whichType, solutions));
        EXPECT_EQ(solutions.size() , 1U);
        CTxDestination addr;
        EXPECT_TRUE(ExtractDestination(s, addr));
        EXPECT_EQ(addr , keyaddr[0]);
#ifdef ENABLE_WALLET
        EXPECT_TRUE(IsMine(keystore, s));
        EXPECT_TRUE(!IsMine(emptykeystore, s));
#endif
    }
    {
        vector<v_uint8> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
        EXPECT_TRUE(Solver(s, whichType, solutions));
        EXPECT_EQ(solutions.size(), 4U);
        CTxDestination addr;
        EXPECT_TRUE(!ExtractDestination(s, addr));
#ifdef ENABLE_WALLET
        EXPECT_TRUE(IsMine(keystore, s));
        EXPECT_TRUE(!IsMine(emptykeystore, s));
        EXPECT_TRUE(!IsMine(partialkeystore, s));
#endif
    }
    {
        vector<v_uint8> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
        EXPECT_TRUE(Solver(s, whichType, solutions));
        EXPECT_EQ(solutions.size(), 4U);
        vector<CTxDestination> addrs;
        int nRequired;
        EXPECT_TRUE(ExtractDestinations(s, whichType, addrs, nRequired));
        EXPECT_EQ(addrs[0] , keyaddr[0]);
        EXPECT_EQ(addrs[1] , keyaddr[1]);
        EXPECT_EQ(nRequired , 1U);
#ifdef ENABLE_WALLET
        EXPECT_TRUE(IsMine(keystore, s));
        EXPECT_TRUE(!IsMine(emptykeystore, s));
        EXPECT_TRUE(!IsMine(partialkeystore, s));
#endif
    }
    {
        vector<v_uint8> solutions;
        txnouttype whichType;
        CScript s;
        s << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
        EXPECT_TRUE(Solver(s, whichType, solutions));
        EXPECT_EQ(solutions.size() , 5U);
    }
}
