// Copyright (c) 2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "data/sighash.json.h"
#include "main.h"
#include "random.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "serialize.h"
#include "test/test_bitcoin.h"
#include "util.h"
#include "version.h"
#include "sodium.h"
#include "json_test_vectors.h"

#include <iostream>
#include <random>

#include <gtest/gtest.h>

// #include <boost/test/unit_test.hpp>

#include <univalue.h>

using namespace std;
using namespace testing;

// extern UniValue read_json(const std::string& jsondata);
UniValue
read_json1(const std::string& jsondata)
{
    UniValue v;

    if (!v.read(jsondata) || !v.isArray())
    {
        EXPECT_TRUE(false) << "Parse error.";
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}


// Old script.cpp SignatureHash function
uint256 static SignatureHashOld(CScript scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
    static const uint256 one(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));
    if (nIn >= txTo.vin.size())
    {
        printf("ERROR: SignatureHash(): nIn=%d out of range\n", nIn);
        return one;
    }
    CMutableTransaction txTmp(txTo);

    // Blank out other inputs' signatures
    for (unsigned int i = 0; i < txTmp.vin.size(); i++)
        txTmp.vin[i].scriptSig = CScript();
    txTmp.vin[nIn].scriptSig = scriptCode;

    // Blank out some of the outputs
    if ((nHashType & 0x1f) == SIGHASH_NONE)
    {
        // Wildcard payee
        txTmp.vout.clear();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn)
                txTmp.vin[i].nSequence = 0;
    }
    else if ((nHashType & 0x1f) == SIGHASH_SINGLE)
    {
        // Only lock-in the txout payee at same index as txin
        unsigned int nOut = nIn;
        if (nOut >= txTmp.vout.size())
        {
            printf("ERROR: SignatureHash(): nOut=%d out of range\n", nOut);
            return one;
        }
        txTmp.vout.resize(nOut+1);
        for (unsigned int i = 0; i < nOut; i++)
            txTmp.vout[i].SetNull();

        // Let the others update at will
        for (unsigned int i = 0; i < txTmp.vin.size(); i++)
            if (i != nIn)
                txTmp.vin[i].nSequence = 0;
    }

    // Blank out other inputs completely, not recommended for open transactions
    if (nHashType & SIGHASH_ANYONECANPAY)
    {
        txTmp.vin[0] = txTmp.vin[nIn];
        txTmp.vin.resize(1);
    }

    // Serialize and hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << txTmp << nHashType;
    return ss.GetHash();
}

void static RandomScript(CScript &script) {
    static const opcodetype oplist[] = {OP_FALSE, OP_1, OP_2, OP_3, OP_CHECKSIG, OP_IF, OP_VERIF, OP_RETURN};
    script = CScript();
    int ops = (insecure_rand() % 10);
    for (int i=0; i<ops; i++)
        script << oplist[insecure_rand() % (sizeof(oplist)/sizeof(oplist[0]))];
}

// Overwinter tx version numbers are selected randomly from current version range.
// http://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
// https://stackoverflow.com/a/19728404
std::random_device rd;
std::mt19937 rng(rd());
std::uniform_int_distribution<int> overwinter_version_dist(
    CTransaction::OVERWINTER_MIN_CURRENT_VERSION,
    CTransaction::OVERWINTER_MAX_CURRENT_VERSION);
std::uniform_int_distribution<int> sapling_version_dist(
    CTransaction::SAPLING_MIN_CURRENT_VERSION,
    CTransaction::SAPLING_MAX_CURRENT_VERSION);

void static RandomTransaction(CMutableTransaction &tx, bool fSingle, uint32_t consensusBranchId) {
    tx.fOverwintered = insecure_rand() % 2;
    if (tx.fOverwintered) {
        if (insecure_rand() % 2) {
            tx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
            tx.nVersion = sapling_version_dist(rng);
        } else {
            tx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
            tx.nVersion = overwinter_version_dist(rng);
        }
        tx.nExpiryHeight = (insecure_rand() % 2) ? insecure_rand() : 0;
    } else {
        tx.nVersion = insecure_rand() & 0x7FFFFFFF;
    }
    tx.vin.clear();
    tx.vout.clear();
    tx.vShieldedSpend.clear();
    tx.vShieldedOutput.clear();
    tx.nLockTime = (insecure_rand() % 2) ? insecure_rand() : 0;
    int ins = (insecure_rand() % 4) + 1;
    int outs = fSingle ? ins : (insecure_rand() % 4) + 1;
    int shielded_spends = (insecure_rand() % 4) + 1;
    int shielded_outs = (insecure_rand() % 4) + 1;
    int joinsplits = (insecure_rand() % 4);
    for (int in = 0; in < ins; in++) {
        tx.vin.push_back(CTxIn());
        CTxIn &txin = tx.vin.back();
        txin.prevout.hash = GetRandHash();
        txin.prevout.n = insecure_rand() % 4;
        RandomScript(txin.scriptSig);
        txin.nSequence = (insecure_rand() % 2) ? insecure_rand() : (unsigned int)-1;
    }
    for (int out = 0; out < outs; out++) {
        tx.vout.push_back(CTxOut());
        CTxOut &txout = tx.vout.back();
        txout.nValue = insecure_rand() % 100000000;
        RandomScript(txout.scriptPubKey);
    }
    if (tx.nVersionGroupId == SAPLING_VERSION_GROUP_ID) {
        tx.valueBalance = insecure_rand() % 100000000;
        for (int spend = 0; spend < shielded_spends; spend++) {
            SpendDescription sdesc;
            sdesc.cv = GetRandHash();
            sdesc.anchor = GetRandHash();
            sdesc.nullifier = GetRandHash();
            sdesc.rk = GetRandHash();
            randombytes_buf(sdesc.zkproof.data(), sdesc.zkproof.size());
            tx.vShieldedSpend.push_back(sdesc);
        }
        for (int out = 0; out < shielded_outs; out++) {
            OutputDescription odesc;
            odesc.cv = GetRandHash();
            odesc.cm = GetRandHash();
            odesc.ephemeralKey = GetRandHash();
            randombytes_buf(odesc.encCiphertext.data(), odesc.encCiphertext.size());
            randombytes_buf(odesc.outCiphertext.data(), odesc.outCiphertext.size());
            randombytes_buf(odesc.zkproof.data(), odesc.zkproof.size());
            tx.vShieldedOutput.push_back(odesc);
        }
    }
}

TEST(test_sighash, sighash_from_data)
{
    UniValue tests = read_json1(std::string(json_tests::sighash, json_tests::sighash + sizeof(json_tests::sighash)));

    for (const auto& test : tests.getValues()) {
        // UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            EXPECT_TRUE(false) << "Bad test: " << strTest;
            continue;
        }
        if (test.size() == 1) continue; // comment

        std::string raw_tx, raw_script, sigHashHex;
        int nIn, nHashType;
        uint256 sh;
        CTransaction tx;
        CScript scriptCode = CScript();

        // try {
          // deserialize test data
          raw_tx = test[0].get_str();
          raw_script = test[1].get_str();
          nIn = test[2].get_int();
          nHashType = test[3].get_int();
          sigHashHex = test[4].get_str();

          CDataStream stream(ParseHex(raw_tx), SER_NETWORK, PROTOCOL_VERSION);
          stream >> tx;

        //   CValidationState state;
        //   auto verifier = libzcash::ProofVerifier::Strict();
        //   EXPECT_TRUE(CheckTransaction(tx, state, verifier)) << strTest;
        //   EXPECT_TRUE(state.IsValid());

        //   std::vector<unsigned char> raw = ParseHex(raw_script);
        //   scriptCode.insert(scriptCode.end(), raw.begin(), raw.end());
        // } catch (...) {
        //   EXPECT_TRUE(false) << "Bad test, couldn't deserialize data: " << strTest;
        //   continue;
        // }

        sh = SignatureHash(scriptCode, tx, nIn, nHashType, 0, 0);
        EXPECT_EQ(sh.GetHex() , sigHashHex) << strTest;
    }
}