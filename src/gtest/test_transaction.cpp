// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utils/detect_cpp_standard.h>
#include <array>
#include <map>
#include <string>
#include <regex>
#include <algorithm>
#ifdef PARALLEL_STL
#include <execution>
#endif

#include <univalue.h>
#include <sodium.h>
#include <gtest/gtest.h>

#include <data/tx_invalid.json.h>
#include <data/tx_valid.json.h>
#include <init.h>
#include <clientversion.h>
#include <checkqueue.h>
#include <consensus/upgrades.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key.h>
#include <keystore.h>
#include <accept_to_mempool.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script_check.h>
#include <main.h>
#include <chain_options.h>
#include <primitives/transaction.h>
#include <json_test_vectors.h>

#include <zcash/Note.hpp>
#include <zcash/Address.hpp>
#include <zcash/Proof.hpp>

using namespace std;
using namespace testing;

static map<string, unsigned int> mapFlagNames = 
{
    {"NONE", (unsigned int)SCRIPT_VERIFY_NONE},
    {"P2SH", (unsigned int)SCRIPT_VERIFY_P2SH},
    {"STRICTENC", (unsigned int)SCRIPT_VERIFY_STRICTENC},
    {"LOW_S", (unsigned int)SCRIPT_VERIFY_LOW_S},
    {"SIGPUSHONLY", (unsigned int)SCRIPT_VERIFY_SIGPUSHONLY},
    {"MINIMALDATA", (unsigned int)SCRIPT_VERIFY_MINIMALDATA},
    {"NULLDUMMY", (unsigned int)SCRIPT_VERIFY_NULLDUMMY},
    {"DISCOURAGE_UPGRADABLE_NOPS", (unsigned int)SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS},
    {"CLEANSTACK", (unsigned int)SCRIPT_VERIFY_CLEANSTACK},
    {"CHECKLOCKTIMEVERIFY", (unsigned int)SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY}
};

unsigned int ParseScriptFlags1(string strFlags)
{
    if (strFlags.empty()) {
        return 0;
    }
    unsigned int flags = 0;
    vector<string> words;
    regex pattern(",");
    words = vector<string>(
                    sregex_token_iterator(strFlags.begin(), strFlags.end(), pattern, -1),
                    sregex_token_iterator()
                    );

    for (const auto &word : words)
    {
        if (!mapFlagNames.count(word))
            EXPECT_TRUE(false) << "Bad test: unknown verification flag '" << word << "'";
        flags |= mapFlagNames[word];
    }

    return flags;
}

TEST(test_transaction, tx_valid)
{
    uint32_t consensusBranchId = SPROUT_BRANCH_ID;

    // Read tests from test/data/tx_valid.json
    // Format is an array of arrays
    // Inner arrays are either [ "comment" ]
    // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
    // ... where all scripts are stringified scripts.
    //
    // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
    UniValue tests = read_json(string(json_tests::tx_valid, json_tests::tx_valid + sizeof(json_tests::tx_valid)));
    string comment("");

    auto verifier = libzcash::ProofVerifier::Strict();
    ScriptError err;
    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
            {
                EXPECT_TRUE(false) << "Bad test: " << strTest << comment;
                continue;
            }

            map<COutPoint, CScript> mapprevOutScriptPubKeys;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
            for (size_t inpIdx = 0; inpIdx < inputs.size(); inpIdx++) {
	        const UniValue& input = inputs[inpIdx];
                if (!input.isArray())
                {
                    fValid = false;
                    break;
                }
                UniValue vinput = input.get_array();
                if (vinput.size() != 3)
                {
                    fValid = false;
                    break;
                }

                mapprevOutScriptPubKeys[COutPoint(uint256S(vinput[0].get_str()), vinput[1].get_int())] = ParseScript(vinput[2].get_str());
            }
            if (!fValid)
            {
                EXPECT_TRUE(false) << "Bad test: " << strTest << comment;
                continue;
            }

            string transaction = test[1].get_str();
            CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
            CTransaction tx;
            stream >> tx;

            CValidationState state(TxOrigin::MSG_TX);
            EXPECT_TRUE(CheckTransaction(tx, state, verifier)) <<  strTest + comment;
            EXPECT_TRUE(state.IsValid()) << comment;

            PrecomputedTransactionData txdata(tx);
            for (unsigned int i = 0; i < tx.vin.size(); i++)
            {
                if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                {
                    EXPECT_TRUE(false) << "Bad test: " << strTest << comment;
                    break;
                }

                CAmount amount = 0;
                unsigned int verify_flags = ParseScriptFlags1(test[2].get_str());
                EXPECT_TRUE(VerifyScript(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout],
                                                 verify_flags, TransactionSignatureChecker(&tx, i, amount, txdata), consensusBranchId, &err))
                                    << strTest + comment;
                EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err) + comment;
            }

            comment = "";
        }
        else if (test.size() == 1)
        {
            comment += "\n# ";
            comment += test[0].write();
        }
    }
}

TEST(test_transaction, tx_invalid)
{
    uint32_t consensusBranchId = SPROUT_BRANCH_ID;

    // Read tests from test/data/tx_invalid.json
    // Format is an array of arrays
    // Inner arrays are either [ "comment" ]
    // or [[[prevout hash, prevout index, prevout scriptPubKey], [input 2], ...],"], serializedTransaction, verifyFlags
    // ... where all scripts are stringified scripts.
    //
    // verifyFlags is a comma separated list of script verification flags to apply, or "NONE"
    UniValue tests = read_json(string(json_tests::tx_invalid, json_tests::tx_invalid + sizeof(json_tests::tx_invalid)));
    string comment("");

    auto verifier = libzcash::ProofVerifier::Strict();
    ScriptError err;
    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        string strTest = test.write();
        if (test[0].isArray())
        {
            if (test.size() != 3 || !test[1].isStr() || !test[2].isStr())
            {
                EXPECT_TRUE(false) << "Bad test: " << strTest << comment;
                continue;
            }

            map<COutPoint, CScript> mapprevOutScriptPubKeys;
            UniValue inputs = test[0].get_array();
            bool fValid = true;
	    for (size_t inpIdx = 0; inpIdx < inputs.size(); inpIdx++) {
	        const UniValue& input = inputs[inpIdx];
                if (!input.isArray())
                {
                    fValid = false;
                    break;
                }
                UniValue vinput = input.get_array();
                if (vinput.size() != 3)
                {
                    fValid = false;
                    break;
                }

                mapprevOutScriptPubKeys[COutPoint(uint256S(vinput[0].get_str()), vinput[1].get_int())] = ParseScript(vinput[2].get_str());
            }
            if (!fValid)
            {
                EXPECT_TRUE(false) << "Bad test: " << strTest << comment;
                continue;
            }

            string transaction = test[1].get_str();
            CDataStream stream(ParseHex(transaction), SER_NETWORK, PROTOCOL_VERSION);
            CTransaction tx;
            stream >> tx;

            CValidationState state(TxOrigin::MSG_TX);
            fValid = CheckTransaction(tx, state, verifier) && state.IsValid();

            PrecomputedTransactionData txdata(tx);
            for (unsigned int i = 0; i < tx.vin.size() && fValid; i++)
            {
                if (!mapprevOutScriptPubKeys.count(tx.vin[i].prevout))
                {
                    EXPECT_TRUE(false) << "Bad test: " << strTest << comment;
                    break;
                }

                unsigned int verify_flags = ParseScriptFlags1(test[2].get_str());
                CAmount amount = 0;
                fValid = VerifyScript(tx.vin[i].scriptSig, mapprevOutScriptPubKeys[tx.vin[i].prevout],
                                      verify_flags, TransactionSignatureChecker(&tx, i, amount, txdata), consensusBranchId, &err);
            }
            EXPECT_TRUE(!fValid) << strTest + comment;
            EXPECT_NE(err , SCRIPT_ERR_OK) << ScriptErrorString(err) + comment;

            comment = "";
        }
        else if (test.size() == 1)
        {
            comment += "\n# ";
            comment += test[0].write();
        }
    }
}

TEST(test_transaction, basic_transaction_tests)
{
    // Random real transaction (e2769b09e784f32f62ef849763d4f45b98e07ba658647343b915ff832b110436)
    unsigned char ch[] = {0x01, 0x00, 0x00, 0x00, 0x01, 0x6b, 0xff, 0x7f, 0xcd, 0x4f, 0x85, 0x65, 0xef, 0x40, 0x6d, 0xd5, 0xd6, 0x3d, 0x4f, 0xf9, 0x4f, 0x31, 0x8f, 0xe8, 0x20, 0x27, 0xfd, 0x4d, 0xc4, 0x51, 0xb0, 0x44, 0x74, 0x01, 0x9f, 0x74, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x49, 0x30, 0x46, 0x02, 0x21, 0x00, 0xda, 0x0d, 0xc6, 0xae, 0xce, 0xfe, 0x1e, 0x06, 0xef, 0xdf, 0x05, 0x77, 0x37, 0x57, 0xde, 0xb1, 0x68, 0x82, 0x09, 0x30, 0xe3, 0xb0, 0xd0, 0x3f, 0x46, 0xf5, 0xfc, 0xf1, 0x50, 0xbf, 0x99, 0x0c, 0x02, 0x21, 0x00, 0xd2, 0x5b, 0x5c, 0x87, 0x04, 0x00, 0x76, 0xe4, 0xf2, 0x53, 0xf8, 0x26, 0x2e, 0x76, 0x3e, 0x2d, 0xd5, 0x1e, 0x7f, 0xf0, 0xbe, 0x15, 0x77, 0x27, 0xc4, 0xbc, 0x42, 0x80, 0x7f, 0x17, 0xbd, 0x39, 0x01, 0x41, 0x04, 0xe6, 0xc2, 0x6e, 0xf6, 0x7d, 0xc6, 0x10, 0xd2, 0xcd, 0x19, 0x24, 0x84, 0x78, 0x9a, 0x6c, 0xf9, 0xae, 0xa9, 0x93, 0x0b, 0x94, 0x4b, 0x7e, 0x2d, 0xb5, 0x34, 0x2b, 0x9d, 0x9e, 0x5b, 0x9f, 0xf7, 0x9a, 0xff, 0x9a, 0x2e, 0xe1, 0x97, 0x8d, 0xd7, 0xfd, 0x01, 0xdf, 0xc5, 0x22, 0xee, 0x02, 0x28, 0x3d, 0x3b, 0x06, 0xa9, 0xd0, 0x3a, 0xcf, 0x80, 0x96, 0x96, 0x8d, 0x7d, 0xbb, 0x0f, 0x91, 0x78, 0xff, 0xff, 0xff, 0xff, 0x02, 0x8b, 0xa7, 0x94, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xba, 0xde, 0xec, 0xfd, 0xef, 0x05, 0x07, 0x24, 0x7f, 0xc8, 0xf7, 0x42, 0x41, 0xd7, 0x3b, 0xc0, 0x39, 0x97, 0x2d, 0x7b, 0x88, 0xac, 0x40, 0x94, 0xa8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x19, 0x76, 0xa9, 0x14, 0xc1, 0x09, 0x32, 0x48, 0x3f, 0xec, 0x93, 0xed, 0x51, 0xf5, 0xfe, 0x95, 0xe7, 0x25, 0x59, 0xf2, 0xcc, 0x70, 0x43, 0xf9, 0x88, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00};
    vector<unsigned char> vch(ch, ch + sizeof(ch) -1);
    CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
    CMutableTransaction tx;
    stream >> tx;
    CValidationState state(TxOrigin::MSG_TX);
    auto verifier = libzcash::ProofVerifier::Strict();
    EXPECT_TRUE(CheckTransaction(tx, state, verifier) && state.IsValid()) << "Simple deserialized transaction should be valid.";

    // Check that duplicate txins fail
    tx.vin.push_back(tx.vin[0]);
    EXPECT_TRUE(!CheckTransaction(tx, state, verifier) || !state.IsValid()) << "Transaction with duplicate txins should be invalid.";
}

//
// Helper: create two dummy transactions, each with
// two outputs.  The first has 11 and 50 CENT outputs
// paid to a TX_PUBKEY, the second 21 and 22 CENT outputs
// paid to a TX_PUBKEYHASH.
//
static vector<CMutableTransaction>
SetupDummyInputs(CBasicKeyStore& keystoreRet, CCoinsViewCache& coinsRet)
{
    vector<CMutableTransaction> dummyTransactions;
    dummyTransactions.resize(2);

    // Add some keys to the keystore:
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(i % 2);
        keystoreRet.AddKey(key[i]);
    }

    // Create some dummy input transactions
    dummyTransactions[0].vout.resize(2);
    dummyTransactions[0].vout[0].nValue = 11*CENT;
    dummyTransactions[0].vout[0].scriptPubKey << ToByteVector(key[0].GetPubKey()) << OP_CHECKSIG;
    dummyTransactions[0].vout[1].nValue = 50*CENT;
    dummyTransactions[0].vout[1].scriptPubKey << ToByteVector(key[1].GetPubKey()) << OP_CHECKSIG;
    coinsRet.ModifyCoins(dummyTransactions[0].GetHash())->FromTx(dummyTransactions[0], 0);

    dummyTransactions[1].vout.resize(2);
    dummyTransactions[1].vout[0].nValue = 21*CENT;
    dummyTransactions[1].vout[0].scriptPubKey = GetScriptForDestination(key[2].GetPubKey().GetID());
    dummyTransactions[1].vout[1].nValue = 22*CENT;
    dummyTransactions[1].vout[1].scriptPubKey = GetScriptForDestination(key[3].GetPubKey().GetID());
    coinsRet.ModifyCoins(dummyTransactions[1].GetHash())->FromTx(dummyTransactions[1], 0);

    return dummyTransactions;
}

void test_simple_sapling_invalidity(uint32_t consensusBranchId, CMutableTransaction tx)
{
    {
        CMutableTransaction newTx(tx);
        CValidationState state(TxOrigin::MSG_TX);

        EXPECT_TRUE(!CheckTransactionWithoutProofVerification(newTx, state));
        EXPECT_EQ(state.GetRejectReason() , "bad-txns-vin-empty");
    }
    {
        CMutableTransaction newTx(tx);
        CValidationState state(TxOrigin::MSG_TX);

        newTx.vShieldedSpend.push_back(SpendDescription());
        newTx.vShieldedSpend[0].nullifier = GetRandHash();

        EXPECT_TRUE(!CheckTransactionWithoutProofVerification(newTx, state));
        EXPECT_EQ(state.GetRejectReason() , "bad-txns-vout-empty");
    }
    {
        // Ensure that nullifiers are never duplicated within a transaction.
        CMutableTransaction newTx(tx);
        CValidationState state(TxOrigin::MSG_TX);

        newTx.vShieldedSpend.push_back(SpendDescription());
        newTx.vShieldedSpend[0].nullifier = GetRandHash();

        newTx.vShieldedOutput.push_back(OutputDescription());

        newTx.vShieldedSpend.push_back(SpendDescription());
        newTx.vShieldedSpend[1].nullifier = newTx.vShieldedSpend[0].nullifier;

        EXPECT_TRUE(!CheckTransactionWithoutProofVerification(newTx, state));
        EXPECT_EQ(state.GetRejectReason() , "bad-spend-description-nullifiers-duplicate");

        newTx.vShieldedSpend[1].nullifier = GetRandHash();

        EXPECT_TRUE(CheckTransactionWithoutProofVerification(newTx, state));
    }
    {
        CMutableTransaction newTx(tx);
        CValidationState state(TxOrigin::MSG_TX);

        // Create a coinbase transaction
        CTxIn vin;
        vin.prevout = COutPoint();
        newTx.vin.push_back(vin);
        CTxOut vout;
        vout.nValue = 1;
        newTx.vout.push_back(vout);

        newTx.vShieldedOutput.push_back(OutputDescription());

        EXPECT_TRUE(!CheckTransactionWithoutProofVerification(newTx, state));
        EXPECT_EQ(state.GetRejectReason() , "bad-cb-has-output-description");

        newTx.vShieldedSpend.push_back(SpendDescription());

        EXPECT_TRUE(!CheckTransactionWithoutProofVerification(newTx, state));
        EXPECT_EQ(state.GetRejectReason() , "bad-cb-has-spend-description");
    }
}

class PTest_Transaction : public TestWithParam<int>
{};

// Parameterized testing over consensus branch ids
TEST_P(PTest_Transaction, test_Get)
{
    const int sample = GetParam();
    EXPECT_LT(sample, static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    CBasicKeyStore keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

    CMutableTransaction t1;
    t1.vin.resize(3);
    t1.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t1.vin[0].prevout.n = 1;
    t1.vin[0].scriptSig << v_uint8(65, 0);
    t1.vin[1].prevout.hash = dummyTransactions[1].GetHash();
    t1.vin[1].prevout.n = 0;
    t1.vin[1].scriptSig << v_uint8(65, 0) << v_uint8(33, 4);
    t1.vin[2].prevout.hash = dummyTransactions[1].GetHash();
    t1.vin[2].prevout.n = 1;
    t1.vin[2].scriptSig << v_uint8(65, 0) << v_uint8(33, 4);
    t1.vout.resize(2);
    t1.vout[0].nValue = 90*CENT;
    t1.vout[0].scriptPubKey << OP_1;

    EXPECT_TRUE(AreInputsStandard(t1, coins, consensusBranchId));
    EXPECT_EQ(coins.GetValueIn(t1), (50+21+22)*CENT);

    // Adding extra junk to the scriptSig should make it non-standard:
    t1.vin[0].scriptSig << OP_11;
    EXPECT_TRUE(!AreInputsStandard(t1, coins, consensusBranchId));

    // ... as should not having enough:
    t1.vin[0].scriptSig = CScript();
    EXPECT_TRUE(!AreInputsStandard(t1, coins, consensusBranchId));
}

INSTANTIATE_TEST_SUITE_P(test_Get, PTest_Transaction, Values(
    0,1,2,3
));

TEST(test_transaction, test_big_overwinter_transaction)
{
    const uint32_t consensusBranchId = GetUpgradeBranchId(Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;

    CKey key;
    key.MakeNewKey(false);
    CBasicKeyStore keystore;
    keystore.AddKeyPubKey(key, key.GetPubKey());
    CKeyID hash = key.GetPubKey().GetID();
    CScript scriptPubKey = GetScriptForDestination(hash);

    v_uint8 sigHashes;
    sigHashes.push_back(enum_or(SIGHASH::NONE, SIGHASH::ANYONECANPAY));
    sigHashes.push_back(enum_or(SIGHASH::SINGLE, SIGHASH::ANYONECANPAY));
    sigHashes.push_back(enum_or(SIGHASH::ALL, SIGHASH::ANYONECANPAY));
    sigHashes.push_back(to_integral_type(SIGHASH::NONE));
    sigHashes.push_back(to_integral_type(SIGHASH::SINGLE));
    sigHashes.push_back(to_integral_type(SIGHASH::ALL));

    // create a big transaction of 4500 inputs signed by the same key
    const uint256 prevId = uint256S("0000000000000000000000000000000000000000000000000000000000000100");
    // number of tx inputs
    constexpr uint32_t TEST_TX_INPUT_SIZE = 4500;
    mtx.vin.reserve(TEST_TX_INPUT_SIZE);
    mtx.vout.reserve(TEST_TX_INPUT_SIZE);
    for(uint32_t i = 0; i < TEST_TX_INPUT_SIZE; ++i)
    {
        COutPoint outpoint(prevId, i);
        mtx.vin.emplace_back(outpoint);
        mtx.vout.emplace_back(1000, CScript() << OP_1);
    }

#ifndef PARALLEL_STL
    // clang does not support parallel STL yet
    for (size_t i = 0; i < TEST_TX_INPUT_SIZE; ++i)
    {
        const bool bHashSigned = SignSignature(keystore, scriptPubKey, mtx, i, 1000, sigHashes.at(i % sigHashes.size()), consensusBranchId);
        ASSERT_TRUE(bHashSigned);
    }
#else
    // sign all inputs concurrently
    for_each(execution::par_unseq, mtx.vin.begin(), mtx.vin.end(), [&](const CTxIn& txIn)
        {
            const uint32_t i = txIn.prevout.n;
            const bool bHashSigned = SignSignature(keystore, scriptPubKey, mtx, i, 1000, sigHashes.at(i % sigHashes.size()), consensusBranchId);
            ASSERT_TRUE(bHashSigned);
        });
#endif

    CTransaction tx;
    CDataStream ssout(SER_NETWORK, PROTOCOL_VERSION);
    ssout << mtx;
    ssout >> tx;

    // check all inputs concurrently, with the cache
    PrecomputedTransactionData txdata(tx);
    CServiceThreadGroup threadGroup;
    CScriptCheckManager scriptCheckMgr;
    scriptCheckMgr.SetThreadCount(MAX_SCRIPTCHECK_THREADS + 10);
    // (for MAX_SCRIPTCHECK_THREADS=16) only 15 workers should be created
    scriptCheckMgr.create_workers(threadGroup);

    auto scriptCheckControl = scriptCheckMgr.create_master(true);

    CCoins coins;
    coins.nVersion = 1;
    coins.fCoinBase = false;
    for (auto &txIn : mtx.vin)
        coins.vout.emplace_back(1000, scriptPubKey);

    vector<CScriptCheck> vChecks;
    for(uint32_t i = 0; i < mtx.vin.size(); i++)
    {
        vChecks.clear();
        vChecks.emplace_back(coins, tx, i, SCRIPT_VERIFY_P2SH, false, consensusBranchId, &txdata);
        scriptCheckControl->Add(vChecks);
    }

    const bool bControlCheck = scriptCheckControl->Wait();
    ASSERT_TRUE(bControlCheck);

    threadGroup.stop_all();
    threadGroup.join_all();
}

TEST(test_transaction, test_IsStandard)
{
    LOCK(cs_main);
    CBasicKeyStore keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << v_uint8(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90*CENT;
    CKey key;
    key.MakeNewKey(true);
    t.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    const auto& chainparams = Params();
    string reason;
    EXPECT_TRUE(IsStandardTx(t, reason, chainparams));

    t.vout[0].nValue = (DEFAULT_MIN_RELAY_TX_FEE / 3) - 1; // dust
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));

    t.vout[0].nValue = 2730; // not dust
    EXPECT_TRUE(IsStandardTx(t, reason, chainparams));

    t.vout[0].scriptPubKey = CScript() << OP_1;
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));

    // 80-byte TX_NULL_DATA (standard)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    EXPECT_TRUE(IsStandardTx(t, reason, chainparams));

    // 81-byte TX_NULL_DATA (non-standard)
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3804678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef3800");
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));

    // TX_NULL_DATA w/o PUSHDATA
    t.vout.resize(1);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    EXPECT_TRUE(IsStandardTx(t, reason, chainparams));

    // Only one TX_NULL_DATA permitted in all cases
    t.vout.resize(2);
    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));

    t.vout[0].scriptPubKey = CScript() << OP_RETURN << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38");
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));

    t.vout[0].scriptPubKey = CScript() << OP_RETURN;
    t.vout[1].scriptPubKey = CScript() << OP_RETURN;
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));
}

TEST(test_transaction, test_IsStandardV2)
{
    LOCK(cs_main);
    CBasicKeyStore keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    vector<CMutableTransaction> dummyTransactions = SetupDummyInputs(keystore, coins);

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << v_uint8(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90*CENT;
    CKey key;
    key.MakeNewKey(true);
    t.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    const auto& chainparams = Params();
    string reason;
    // A v2 transaction with no JoinSplits is still standard.
    t.nVersion = 2;
    EXPECT_TRUE(IsStandardTx(t, reason, chainparams));

    // v2 transactions can still be non-standard for the same reasons as v1.
    t.vout[0].nValue = (DEFAULT_MIN_RELAY_TX_FEE / 3) - 1; // dust
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));

    // v3 is not standard.
    t.nVersion = 3;
    t.vout[0].nValue = 90*CENT;
    EXPECT_TRUE(!IsStandardTx(t, reason, chainparams));
}

