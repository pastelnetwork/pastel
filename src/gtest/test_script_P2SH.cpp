// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <vector>

#include <gtest/gtest.h>

#include <consensus/upgrades.h>
#include <core_io.h>
#include <key.h>
#include <keystore.h>
#include <main.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet_ismine.h>
#endif

using namespace std;
using namespace testing;

// Helpers:
static v_uint8 Serialize(const CScript& s)
{
    v_uint8 sSerialized(s.begin(), s.end());
    return sSerialized;
}

static bool Verify(const CScript& scriptSig, const CScript& scriptPubKey, bool fStrict, ScriptError& err, uint32_t consensusBranchId)
{
    // Create dummy to/from transactions:
    CMutableTransaction txFrom;
    txFrom.vout.resize(1);
    txFrom.vout[0].scriptPubKey = scriptPubKey;

    CMutableTransaction txTo;
    txTo.vin.resize(1);
    txTo.vout.resize(1);
    txTo.vin[0].prevout.n = 0;
    txTo.vin[0].prevout.hash = txFrom.GetHash();
    txTo.vin[0].scriptSig = scriptSig;
    txTo.vout[0].nValue = 1;

    return VerifyScript(scriptSig, scriptPubKey, fStrict ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE, MutableTransactionSignatureChecker(&txTo, 0, txFrom.vout[0].nValue), consensusBranchId, &err);
}

class PTest_ScriptP2SH : public TestWithParam<int>
{};

// Parameterized testing over consensus branch ids
TEST_P(PTest_ScriptP2SH, sign)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    LOCK(cs_main);
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;
    // Pay-to-script-hash looks like this:
    // scriptSig:    <sig> <sig...> <serialized_script>
    // scriptPubKey: HASH160 <hash> EQUAL

    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    // 8 Scripts: checking all combinations of
    // different keys, straight/P2SH, pubkey/pubkeyhash
    CScript standardScripts[4];
    standardScripts[0] << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    standardScripts[1] = GetScriptForDestination(key[1].GetPubKey().GetID());
    standardScripts[2] << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    standardScripts[3] = GetScriptForDestination(key[2].GetPubKey().GetID());
    CScript evalScripts[4];
    for (int i = 0; i < 4; i++)
    {
        keystore.AddCScript(standardScripts[i]);
        evalScripts[i] = GetScriptForDestination(CScriptID(standardScripts[i]));
    }

    CMutableTransaction txFrom;  // Funding transaction:
    string reason;
    txFrom.vout.resize(8);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = evalScripts[i];
        txFrom.vout[i].nValue = COIN;
        txFrom.vout[i+4].scriptPubKey = standardScripts[i];
        txFrom.vout[i+4].nValue = COIN;
    }
    const auto& chainparams = Params();
    EXPECT_TRUE(IsStandardTx(txFrom, reason, chainparams));

    CMutableTransaction txTo[8]; // Spending transactions
    for (int i = 0; i < 8; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
#ifdef ENABLE_WALLET
        EXPECT_TRUE(IsMine(keystore, txFrom.vout[i].scriptPubKey)) << strprintf("IsMine %d", i);
#endif
    }
    for (int i = 0; i < 8; i++)
    {
        EXPECT_TRUE(SignSignature(keystore, txFrom, txTo[i], 0, to_integral_type(SIGHASH::ALL), consensusBranchId)) << strprintf("SignSignature %d", i);
    }
    // All of the above should be OK, and the txTos have valid signatures
    // Check to make sure signature verification fails if we use the wrong ScriptSig:
    for (int i = 0; i < 8; i++) {
        PrecomputedTransactionData txdata(txTo[i]);
        for (int j = 0; j < 8; j++)
        {
            CScript sigSave = txTo[i].vin[0].scriptSig;
            txTo[i].vin[0].scriptSig = txTo[j].vin[0].scriptSig;
            bool sigOK = CScriptCheck(CCoins(txFrom, 0), txTo[i], 0, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, false, consensusBranchId, &txdata)();
            if (i == j)
                EXPECT_TRUE(sigOK) << strprintf("VerifySignature %d %d", i, j);
            else
                EXPECT_TRUE(!sigOK) << strprintf("VerifySignature %d %d", i, j);
            txTo[i].vin[0].scriptSig = sigSave;
        }
    }
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_ScriptP2SH, norecurse)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    ScriptError err;
    // Make sure only the outer pay-to-script-hash does the
    // extra-validation thing:
    CScript invalidAsScript;
    invalidAsScript << OP_INVALIDOPCODE << OP_INVALIDOPCODE;

    CScript p2sh = GetScriptForDestination(CScriptID(invalidAsScript));

    CScript scriptSig;
    scriptSig << Serialize(invalidAsScript);

    // Should not verify, because it will try to execute OP_INVALIDOPCODE
    EXPECT_TRUE(!Verify(scriptSig, p2sh, true, err, consensusBranchId));
    EXPECT_EQ(err , SCRIPT_ERR_BAD_OPCODE) << ScriptErrorString(err);

    // Try to recur, and verification should succeed because
    // the inner HASH160 <> EQUAL should only check the hash:
    CScript p2sh2 = GetScriptForDestination(CScriptID(p2sh));
    CScript scriptSig2;
    scriptSig2 << Serialize(invalidAsScript) << Serialize(p2sh);

    EXPECT_TRUE(Verify(scriptSig2, p2sh2, true, err, consensusBranchId));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_ScriptP2SH, set)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    LOCK(cs_main);
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;
    // Test the CScript::Set* methods
    CBasicKeyStore keystore;
    CKey key[4];
    vector<CPubKey> keys;
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
        keys.push_back(key[i].GetPubKey());
    }

    CScript inner[4];
    inner[0] = GetScriptForDestination(key[0].GetPubKey().GetID());
    inner[1] = GetScriptForMultisig(2, vector<CPubKey>(keys.begin(), keys.begin()+2));
    inner[2] = GetScriptForMultisig(1, vector<CPubKey>(keys.begin(), keys.begin()+2));
    inner[3] = GetScriptForMultisig(2, vector<CPubKey>(keys.begin(), keys.begin()+3));

    CScript outer[4];
    for (int i = 0; i < 4; i++)
    {
        outer[i] = GetScriptForDestination(CScriptID(inner[i]));
        keystore.AddCScript(inner[i]);
    }

    CMutableTransaction txFrom;  // Funding transaction:
    string reason;
    txFrom.vout.resize(4);
    for (int i = 0; i < 4; i++)
    {
        txFrom.vout[i].scriptPubKey = outer[i];
        txFrom.vout[i].nValue = CENT;
    }
    const auto& chainparams = Params();
    EXPECT_TRUE(IsStandardTx(txFrom, reason, chainparams));

    CMutableTransaction txTo[4]; // Spending transactions
    for (int i = 0; i < 4; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1*CENT;
        txTo[i].vout[0].scriptPubKey = inner[i];
#ifdef ENABLE_WALLET
        EXPECT_TRUE(IsMine(keystore, txFrom.vout[i].scriptPubKey)) << strprintf("IsMine %d", i);
#endif
    }
    for (int i = 0; i < 4; i++)
    {
        EXPECT_TRUE(SignSignature(keystore, txFrom, txTo[i], 0, to_integral_type(SIGHASH::ALL), consensusBranchId)) << strprintf("SignSignature %d", i);
        EXPECT_TRUE(IsStandardTx(txTo[i], reason, chainparams)) << strprintf("txTo[%d].IsStandard", i);
    }
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_ScriptP2SH, switchover)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Test switch over code
    CScript notValid;
    ScriptError err;
    notValid << OP_11 << OP_12 << OP_EQUALVERIFY;
    CScript scriptSig;
    scriptSig << Serialize(notValid);

    CScript fund = GetScriptForDestination(CScriptID(notValid));


    // Validation should succeed under old rules (hash is correct):
    EXPECT_TRUE(Verify(scriptSig, fund, false, err, consensusBranchId));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);
    // Fail under new:
    EXPECT_TRUE(!Verify(scriptSig, fund, true, err, consensusBranchId));
    EXPECT_EQ(err , SCRIPT_ERR_EQUALVERIFY) << ScriptErrorString(err);
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_ScriptP2SH, AreInputsStandard)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    LOCK(cs_main);
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    CBasicKeyStore keystore;
    CKey key[6];
    vector<CPubKey> keys;
    for (int i = 0; i < 6; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }
    for (int i = 0; i < 3; i++)
        keys.push_back(key[i].GetPubKey());

    CMutableTransaction txFrom;
    txFrom.vout.resize(7);

    // First three are standard:
    CScript pay1 = GetScriptForDestination(key[0].GetPubKey().GetID());
    keystore.AddCScript(pay1);
    CScript pay1of3 = GetScriptForMultisig(1, keys);

    txFrom.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(pay1)); // P2SH (OP_CHECKSIG)
    txFrom.vout[0].nValue = 1000;
    txFrom.vout[1].scriptPubKey = pay1; // ordinary OP_CHECKSIG
    txFrom.vout[1].nValue = 2000;
    txFrom.vout[2].scriptPubKey = pay1of3; // ordinary OP_CHECKMULTISIG
    txFrom.vout[2].nValue = 3000;

    // vout[3] is complicated 1-of-3 AND 2-of-3
    // ... that is OK if wrapped in P2SH:
    CScript oneAndTwo;
    oneAndTwo << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey());
    oneAndTwo << OP_3 << OP_CHECKMULTISIGVERIFY;
    oneAndTwo << OP_2 << ToByteVector(key[3].GetPubKey()) << ToByteVector(key[4].GetPubKey()) << ToByteVector(key[5].GetPubKey());
    oneAndTwo << OP_3 << OP_CHECKMULTISIG;
    keystore.AddCScript(oneAndTwo);
    txFrom.vout[3].scriptPubKey = GetScriptForDestination(CScriptID(oneAndTwo));
    txFrom.vout[3].nValue = 4000;

    // vout[4] is max sigops:
    CScript fifteenSigops; fifteenSigops << OP_1;
    for (unsigned i = 0; i < MAX_P2SH_SIGOPS; i++)
        fifteenSigops << ToByteVector(key[i%3].GetPubKey());
    fifteenSigops << OP_15 << OP_CHECKMULTISIG;
    keystore.AddCScript(fifteenSigops);
    txFrom.vout[4].scriptPubKey = GetScriptForDestination(CScriptID(fifteenSigops));
    txFrom.vout[4].nValue = 5000;

    // vout[5/6] are non-standard because they exceed MAX_P2SH_SIGOPS
    CScript sixteenSigops; sixteenSigops << OP_16 << OP_CHECKMULTISIG;
    keystore.AddCScript(sixteenSigops);
    txFrom.vout[5].scriptPubKey = GetScriptForDestination(CScriptID(fifteenSigops));
    txFrom.vout[5].nValue = 5000;
    CScript twentySigops; twentySigops << OP_CHECKMULTISIG;
    keystore.AddCScript(twentySigops);
    txFrom.vout[6].scriptPubKey = GetScriptForDestination(CScriptID(twentySigops));
    txFrom.vout[6].nValue = 6000;

    coins.ModifyCoins(txFrom.GetHash())->FromTx(txFrom, 0);

    CMutableTransaction txTo;
    txTo.vout.resize(1);
    txTo.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());

    txTo.vin.resize(5);
    for (int i = 0; i < 5; i++)
    {
        txTo.vin[i].prevout.n = i;
        txTo.vin[i].prevout.hash = txFrom.GetHash();
    }
    EXPECT_TRUE(SignSignature(keystore, txFrom, txTo, 0, to_integral_type(SIGHASH::ALL), consensusBranchId));
    EXPECT_TRUE(SignSignature(keystore, txFrom, txTo, 1, to_integral_type(SIGHASH::ALL), consensusBranchId));
    EXPECT_TRUE(SignSignature(keystore, txFrom, txTo, 2, to_integral_type(SIGHASH::ALL), consensusBranchId));
    // SignSignature doesn't know how to sign these. We're
    // not testing validating signatures, so just create
    // dummy signatures that DO include the correct P2SH scripts:
    txTo.vin[3].scriptSig << OP_11 << OP_11 << v_uint8(oneAndTwo.begin(), oneAndTwo.end());
    txTo.vin[4].scriptSig << v_uint8(fifteenSigops.begin(), fifteenSigops.end());

    EXPECT_TRUE(::AreInputsStandard(txTo, coins, consensusBranchId));
    // 22 P2SH sigops for all inputs (1 for vin[0], 6 for vin[3], 15 for vin[4]
    EXPECT_EQ(GetP2SHSigOpCount(txTo, coins), 22U);

    // Make sure adding crap to the scriptSigs makes them non-standard:
    for (int i = 0; i < 3; i++)
    {
        CScript t = txTo.vin[i].scriptSig;
        txTo.vin[i].scriptSig = (CScript() << 11) + t;
        EXPECT_TRUE(!::AreInputsStandard(txTo, coins, consensusBranchId));
        txTo.vin[i].scriptSig = t;
    }

    CMutableTransaction txToNonStd1;
    txToNonStd1.vout.resize(1);
    txToNonStd1.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());
    txToNonStd1.vout[0].nValue = 1000;
    txToNonStd1.vin.resize(1);
    txToNonStd1.vin[0].prevout.n = 5;
    txToNonStd1.vin[0].prevout.hash = txFrom.GetHash();
    txToNonStd1.vin[0].scriptSig << v_uint8(sixteenSigops.begin(), sixteenSigops.end());

    EXPECT_TRUE(!::AreInputsStandard(txToNonStd1, coins, consensusBranchId));
    EXPECT_EQ(GetP2SHSigOpCount(txToNonStd1, coins), 16U);

    CMutableTransaction txToNonStd2;
    txToNonStd2.vout.resize(1);
    txToNonStd2.vout[0].scriptPubKey = GetScriptForDestination(key[1].GetPubKey().GetID());
    txToNonStd2.vout[0].nValue = 1000;
    txToNonStd2.vin.resize(1);
    txToNonStd2.vin[0].prevout.n = 6;
    txToNonStd2.vin[0].prevout.hash = txFrom.GetHash();
    txToNonStd2.vin[0].scriptSig << v_uint8(twentySigops.begin(), twentySigops.end());

    EXPECT_TRUE(!::AreInputsStandard(txToNonStd2, coins, consensusBranchId));
    EXPECT_EQ(GetP2SHSigOpCount(txToNonStd2, coins), 20U);
}

INSTANTIATE_TEST_SUITE_P(script_P2SH, PTest_ScriptP2SH, Values(
    0,1,2,3
));

TEST(test_script_P2SH, is)
{
    // Test CScript::IsPayToScriptHash()
    uint160 dummy;
    CScript p2sh;
    p2sh << OP_HASH160 << ToByteVector(dummy) << OP_EQUAL;
    EXPECT_TRUE(p2sh.IsPayToScriptHash());

    // Not considered pay-to-script-hash if using one of the OP_PUSHDATA opcodes:
    static const unsigned char direct[] =    { OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    EXPECT_TRUE(CScript(direct, direct+sizeof(direct)).IsPayToScriptHash());
    static const unsigned char pushdata1[] = { OP_HASH160, OP_PUSHDATA1, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    EXPECT_TRUE(!CScript(pushdata1, pushdata1+sizeof(pushdata1)).IsPayToScriptHash());
    static const unsigned char pushdata2[] = { OP_HASH160, OP_PUSHDATA2, 20,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    EXPECT_TRUE(!CScript(pushdata2, pushdata2+sizeof(pushdata2)).IsPayToScriptHash());
    static const unsigned char pushdata4[] = { OP_HASH160, OP_PUSHDATA4, 20,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL };
    EXPECT_TRUE(!CScript(pushdata4, pushdata4+sizeof(pushdata4)).IsPayToScriptHash());

    CScript not_p2sh;
    EXPECT_TRUE(!not_p2sh.IsPayToScriptHash());

    not_p2sh.clear(); not_p2sh << OP_HASH160 << ToByteVector(dummy) << ToByteVector(dummy) << OP_EQUAL;
    EXPECT_TRUE(!not_p2sh.IsPayToScriptHash());

    not_p2sh.clear(); not_p2sh << OP_NOP << ToByteVector(dummy) << OP_EQUAL;
    EXPECT_TRUE(!not_p2sh.IsPayToScriptHash());

    not_p2sh.clear(); not_p2sh << OP_HASH160 << ToByteVector(dummy) << OP_CHECKSIG;
    EXPECT_TRUE(!not_p2sh.IsPayToScriptHash());
}
