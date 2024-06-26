// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2021-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fstream>
#include <cstdint>
#include <string>
#include <vector>
#include <regex>

#include <gtest/gtest.h>

#include <utils/util.h>
#include <data/script_invalid.json.h>
#include <data/script_valid.json.h>
#include <consensus/upgrades.h>
#include <core_io.h>
#include <key.h>
#include <keystore.h>
#include <main.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <json_test_vectors.h>

#if defined(HAVE_CONSENSUS_LIB)
#include <script/zcashconsensus.h>
#endif
#include <univalue.h>

using namespace std;
using namespace testing;

// Uncomment if you want to output updated JSON tests.
// #define UPDATE_JSON_TESTS

constexpr unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

static map<string, unsigned int> mapFlagNames = {
    {string("NONE"), (unsigned int)SCRIPT_VERIFY_NONE},
    {string("P2SH"), (unsigned int)SCRIPT_VERIFY_P2SH},
    {string("STRICTENC"), (unsigned int)SCRIPT_VERIFY_STRICTENC},
    {string("LOW_S"), (unsigned int)SCRIPT_VERIFY_LOW_S},
    {string("SIGPUSHONLY"), (unsigned int)SCRIPT_VERIFY_SIGPUSHONLY},
    {string("MINIMALDATA"), (unsigned int)SCRIPT_VERIFY_MINIMALDATA},
    {string("NULLDUMMY"), (unsigned int)SCRIPT_VERIFY_NULLDUMMY},
    {string("DISCOURAGE_UPGRADABLE_NOPS"), (unsigned int)SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS},
    {string("CLEANSTACK"), (unsigned int)SCRIPT_VERIFY_CLEANSTACK},
    {string("CHECKLOCKTIMEVERIFY"), (unsigned int)SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY}
};

unsigned int ParseScriptFlags(string strFlags)
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

string FormatScriptFlags(unsigned int flags)
{
    if (flags == 0) {
        return "";
    }
    string ret;
    map<string, unsigned int>::const_iterator it = mapFlagNames.begin();
    while (it != mapFlagNames.end()) {
        if (flags & it->second) {
            ret += it->first + ",";
        }
        it++;
    }
    return ret.substr(0, ret.size() - 1);
}

UniValue
read_json_script(const string& jsondata)
{
    UniValue v;

    if (!v.read(jsondata) || !v.isArray())
    {
        EXPECT_TRUE(false) << "Parse error.";
        return UniValue(UniValue::VARR);
    }
    return v.get_array();
}

CMutableTransaction BuildCreditingTransaction(const CScript& scriptPubKey)
{
    CMutableTransaction txCredit;
    txCredit.nVersion = 1;
    txCredit.nLockTime = 0;
    txCredit.vin.resize(1);
    txCredit.vout.resize(1);
    txCredit.vin[0].prevout.SetNull();
    txCredit.vin[0].scriptSig = CScript() << CScriptNum(0) << CScriptNum(0);
    txCredit.vin[0].nSequence = numeric_limits<unsigned int>::max();
    txCredit.vout[0].scriptPubKey = scriptPubKey;
    txCredit.vout[0].nValue = 0;

    return txCredit;
}

CMutableTransaction BuildSpendingTransaction(const CScript& scriptSig, const CMutableTransaction& txCredit)
{
    CMutableTransaction txSpend;
    txSpend.nVersion = 1;
    txSpend.nLockTime = 0;
    txSpend.vin.resize(1);
    txSpend.vout.resize(1);
    txSpend.vin[0].prevout.hash = txCredit.GetHash();
    txSpend.vin[0].prevout.n = 0;
    txSpend.vin[0].scriptSig = scriptSig;
    txSpend.vin[0].nSequence = numeric_limits<unsigned int>::max();
    txSpend.vout[0].scriptPubKey = CScript();
    txSpend.vout[0].nValue = 0;

    return txSpend;
}

void DoTest(const CScript& scriptPubKey, const CScript& scriptSig, int flags, uint32_t consensusBranchId, bool expect, const string& message)
{
    ScriptError err;
    CMutableTransaction txCredit = BuildCreditingTransaction(scriptPubKey);
    CMutableTransaction tx = BuildSpendingTransaction(scriptSig, txCredit);
    CMutableTransaction tx2 = tx;
    EXPECT_EQ(VerifyScript(scriptSig, scriptPubKey, flags, MutableTransactionSignatureChecker(&tx, 0, txCredit.vout[0].nValue), consensusBranchId, &err) , expect) << message;
    EXPECT_EQ(expect , (err == SCRIPT_ERR_OK)) << string(ScriptErrorString(err)) + ": " + message;
#if defined(HAVE_CONSENSUS_LIB)
    int expectedSuccessCode = expect ? 1 : 0;
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx2;
    EXPECT_EQ(zcashconsensus_verify_script(begin_ptr(scriptPubKey), scriptPubKey.size(), (const unsigned char*)&stream[0], stream.size(), 0, flags, nullptr) , expectedSuccessCode) << message);
#endif
}

void static NegateSignatureS(v_uint8& vchSig) {
    // Parse the signature.
    v_uint8 r, s;
    r = v_uint8(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
    s = v_uint8(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);

    // Really ugly to implement mod-n negation here, but it would be feature creep to expose such functionality from libsecp256k1.
    constexpr unsigned char order[33] = {
        0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
    };
    while (s.size() < 33) {
        s.insert(s.begin(), 0x00);
    }
    int carry = 0;
    for (int p = 32; p >= 1; p--) {
        int n = (int)order[p] - s[p] - carry;
        s[p] = (n + 256) & 0xFF;
        carry = (n < 0);
    }
    assert(carry == 0);
    if (s.size() > 1 && s[0] == 0 && s[1] < 0x80) { //-V560
        s.erase(s.begin());
    }

    // Reconstruct the signature.
    vchSig.clear();
    vchSig.push_back(0x30);
    vchSig.push_back(static_cast<uint8_t>(4 + r.size() + s.size()));
    vchSig.push_back(0x02);
    vchSig.push_back(static_cast<uint8_t>(r.size()));
    vchSig.insert(vchSig.end(), r.begin(), r.end());
    vchSig.push_back(0x02);
    vchSig.push_back(static_cast<uint8_t>(s.size()));
    vchSig.insert(vchSig.end(), s.begin(), s.end());
}

namespace
{
const unsigned char vchKey0[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
const unsigned char vchKey1[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0};
const unsigned char vchKey2[32] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0};

struct KeyData
{
    CKey key0, key0C, key1, key1C, key2, key2C;
    CPubKey pubkey0, pubkey0C, pubkey0H;
    CPubKey pubkey1, pubkey1C;
    CPubKey pubkey2, pubkey2C;

    KeyData()
    {

        key0.Set(vchKey0, vchKey0 + 32, false);
        key0C.Set(vchKey0, vchKey0 + 32, true);
        pubkey0 = key0.GetPubKey();
        pubkey0H = key0.GetPubKey();
        pubkey0C = key0C.GetPubKey();
        *const_cast<unsigned char*>(&pubkey0H[0]) = 0x06 | (pubkey0H[64] & 1);

        key1.Set(vchKey1, vchKey1 + 32, false);
        key1C.Set(vchKey1, vchKey1 + 32, true);
        pubkey1 = key1.GetPubKey();
        pubkey1C = key1C.GetPubKey();

        key2.Set(vchKey2, vchKey2 + 32, false);
        key2C.Set(vchKey2, vchKey2 + 32, true);
        pubkey2 = key2.GetPubKey();
        pubkey2C = key2C.GetPubKey();
    }
};


class TestBuilder
{
private:
    CScript scriptPubKey;
    CTransaction creditTx;
    CMutableTransaction spendTx;
    bool havePush;
    v_uint8 push;
    string comment;
    int flags;
    uint32_t consensusBranchId;

    void DoPush()
    {
        if (havePush) {
            spendTx.vin[0].scriptSig << push;
            havePush = false;
        }
    }

    void DoPush(const v_uint8& data)
    {
         DoPush();
         push = data;
         havePush = true;
    }

public:
    TestBuilder(const CScript& redeemScript, const string& comment_, int flags_, bool P2SH = false) : scriptPubKey(redeemScript), havePush(false), comment(comment_), flags(flags_), consensusBranchId(0)
    {
        if (P2SH) {
            creditTx = BuildCreditingTransaction(CScript() << OP_HASH160 << ToByteVector(CScriptID(redeemScript)) << OP_EQUAL);
        } else {
            creditTx = BuildCreditingTransaction(redeemScript);
        }
        spendTx = BuildSpendingTransaction(CScript(), creditTx);
    }

    TestBuilder& Add(const CScript& script)
    {
        DoPush();
        spendTx.vin[0].scriptSig += script;
        return *this;
    }

    TestBuilder& Num(int num)
    {
        DoPush();
        spendTx.vin[0].scriptSig << num;
        return *this;
    }

    TestBuilder& Push(const string& hex)
    {
        DoPush(ParseHex(hex));
        return *this;
    }

    TestBuilder& PushSig(const CKey& key, uint8_t nHashType = to_integral_type(SIGHASH::ALL), unsigned int lenR = 32, unsigned int lenS = 32)
    {
        uint256 hash = SignatureHash(scriptPubKey, spendTx, 0, nHashType, 0, consensusBranchId);
        v_uint8 vchSig, r, s;
        uint32_t iter = 0;
        do {
            key.Sign(hash, vchSig, iter++);
            if ((lenS == 33) != (vchSig[5 + vchSig[3]] == 33)) {
                NegateSignatureS(vchSig);
            }
            r = v_uint8(vchSig.begin() + 4, vchSig.begin() + 4 + vchSig[3]);
            s = v_uint8(vchSig.begin() + 6 + vchSig[3], vchSig.begin() + 6 + vchSig[3] + vchSig[5 + vchSig[3]]);
        } while (lenR != r.size() || lenS != s.size());
        vchSig.push_back(nHashType);
        DoPush(vchSig);
        return *this;
    }

    TestBuilder& Push(const CPubKey& pubkey)
    {
        DoPush(v_uint8(pubkey.begin(), pubkey.end()));
        return *this;
    }

    TestBuilder& PushRedeem()
    {
        DoPush(v_uint8(scriptPubKey.begin(), scriptPubKey.end()));
        return *this;
    }

    TestBuilder& EditPush(unsigned int pos, const string& hexin, const string& hexout)
    {
        assert(havePush);
        v_uint8 datain = ParseHex(hexin);
        v_uint8 dataout = ParseHex(hexout);
        assert(pos + datain.size() <= push.size());
        EXPECT_EQ(v_uint8(push.begin() + pos, push.begin() + pos + datain.size()) , datain) << comment;
        push.erase(push.begin() + pos, push.begin() + pos + datain.size());
        push.insert(push.begin() + pos, dataout.begin(), dataout.end());
        return *this;
    }

    TestBuilder& DamagePush(unsigned int pos)
    {
        assert(havePush);
        assert(pos < push.size());
        push[pos] ^= 1;
        return *this;
    }

    TestBuilder& Test(bool expect)
    {
        TestBuilder copy = *this; // Make a copy so we can rollback the push.
        DoPush();
        DoTest(creditTx.vout[0].scriptPubKey, spendTx.vin[0].scriptSig, flags, consensusBranchId, expect, comment);
        *this = copy;
        return *this;
    }

    UniValue GetJSON()
    {
        DoPush();
        UniValue array(UniValue::VARR);
        array.push_back(FormatScript(spendTx.vin[0].scriptSig));
        array.push_back(FormatScript(creditTx.vout[0].scriptPubKey));
        array.push_back(FormatScriptFlags(flags));
        array.push_back(comment);
        return array;
    }

    string GetComment()
    {
        return comment;
    }

    const CScript& GetScriptPubKey()
    {
        return creditTx.vout[0].scriptPubKey;
    }
};
}

TEST(test_script, script_build)
{
    const KeyData keys;

    vector<TestBuilder> good;
    vector<TestBuilder> bad;

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2PK", 0
                              ).PushSig(keys.key0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2PK, bad sig", 0
                             ).PushSig(keys.key0).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                               "P2PKH", 0
                              ).PushSig(keys.key1).Push(keys.pubkey1C));
    bad.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey2C.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                              "P2PKH, bad pubkey", 0
                             ).PushSig(keys.key2).Push(keys.pubkey2C).DamagePush(5));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                               "P2PK anyonecanpay", 0
                              ).PushSig(keys.key1, enum_or(SIGHASH::ALL, SIGHASH::ANYONECANPAY)));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                              "P2PK anyonecanpay marked with normal hashtype", 0
                             ).PushSig(keys.key1, enum_or(SIGHASH::ALL, SIGHASH::ANYONECANPAY)).EditPush(70, "81", "01"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                               "P2SH(P2PK)", SCRIPT_VERIFY_P2SH, true
                              ).PushSig(keys.key0).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0C) << OP_CHECKSIG,
                              "P2SH(P2PK), bad redeemscript", SCRIPT_VERIFY_P2SH, true
                             ).PushSig(keys.key0).PushRedeem().DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                               "P2SH(P2PKH), bad sig but no VERIFY_P2SH", 0, true
                              ).PushSig(keys.key0).DamagePush(10).PushRedeem());
    bad.push_back(TestBuilder(CScript() << OP_DUP << OP_HASH160 << ToByteVector(keys.pubkey1.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG,
                              "P2SH(P2PKH), bad sig", SCRIPT_VERIFY_P2SH, true
                             ).PushSig(keys.key0).DamagePush(10).PushRedeem());

    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "3-of-3", 0
                              ).Num(0).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "3-of-3, 2 sigs", 0
                             ).Num(0).PushSig(keys.key0).PushSig(keys.key1).Num(0));

    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "P2SH(2-of-3)", SCRIPT_VERIFY_P2SH, true
                              ).Num(0).PushSig(keys.key1).PushSig(keys.key2).PushRedeem());
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "P2SH(2-of-3), 1 sig", SCRIPT_VERIFY_P2SH, true
                             ).Num(0).PushSig(keys.key1).Num(0).PushRedeem());

    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too much R padding", 0
                             ).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 31, 32).EditPush(1, "43021F", "44022000"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too much S padding", 0
                             ).PushSig(keys.key1, to_integral_type(SIGHASH::ALL)).EditPush(1, "44", "45").EditPush(37, "20", "2100"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "P2PK with too little R padding", 0
                             ).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with bad sig with too much R padding", 0
                             ).PushSig(keys.key2, to_integral_type(SIGHASH::ALL), 31, 32).EditPush(1, "43021F", "44022000").DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with too much R padding", 0
                             ).PushSig(keys.key2, to_integral_type(SIGHASH::ALL), 31, 32).EditPush(1, "43021F", "44022000"));

    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 1", 0
                             ).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 2", 0
                             ).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 3", 0
                             ).Num(0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                               "BIP66 example 4", 0
                              ).Num(0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG,
                              "BIP66 example 5", 0
                             ).Num(1));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1C) << OP_CHECKSIG << OP_NOT,
                              "BIP66 example 6", 0
                             ).Num(1));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 7", 0
                             ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 8", 0
                             ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220").PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 9", 0
                             ).Num(0).Num(0).PushSig(keys.key2, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                              "BIP66 example 10", 0
                             ).Num(0).Num(0).PushSig(keys.key2, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220"));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG,
                              "BIP66 example 11", 0
                             ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220").Num(0));
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_2 << OP_CHECKMULTISIG << OP_NOT,
                               "BIP66 example 12", 0
                              ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL), 33, 32).EditPush(1, "45022100", "440220").Num(0));

    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                               "P2PK with multi-byte hashtype", 0
                              ).PushSig(keys.key2, to_integral_type(SIGHASH::ALL)).EditPush(70, "01", "0101"));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                               "P2PK with high S but no LOW_S", 0
                              ).PushSig(keys.key2, to_integral_type(SIGHASH::ALL), 32, 33));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2PK with high S", SCRIPT_VERIFY_LOW_S
                             ).PushSig(keys.key2, to_integral_type(SIGHASH::ALL), 32, 33));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                               "P2PK with hybrid pubkey but no STRICTENC", 0
                              ).PushSig(keys.key0, to_integral_type(SIGHASH::ALL)));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG,
                              "P2PK with hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, to_integral_type(SIGHASH::ALL)));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with hybrid pubkey but no STRICTENC", 0
                             ).PushSig(keys.key0, to_integral_type(SIGHASH::ALL)));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, to_integral_type(SIGHASH::ALL)));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with invalid hybrid pubkey but no STRICTENC", 0
                              ).PushSig(keys.key0, to_integral_type(SIGHASH::ALL)).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0H) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with invalid hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key0, to_integral_type(SIGHASH::ALL)).DamagePush(10));
    good.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "1-of-2 with the second 1 hybrid pubkey and no STRICTENC", 0
                              ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL)));
    good.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey0H) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "1-of-2 with the second 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                              ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL)));
    bad.push_back(TestBuilder(CScript() << OP_1 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey0H) << OP_2 << OP_CHECKMULTISIG,
                              "1-of-2 with the first 1 hybrid pubkey", SCRIPT_VERIFY_STRICTENC
                             ).Num(0).PushSig(keys.key1, to_integral_type(SIGHASH::ALL)));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                               "P2PK with undefined hashtype but no STRICTENC", 0
                              ).PushSig(keys.key1, 5));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG,
                              "P2PK with undefined hashtype", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key1, 5));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                               "P2PK NOT with invalid sig and undefined hashtype but no STRICTENC", 0
                              ).PushSig(keys.key1, 5).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey1) << OP_CHECKSIG << OP_NOT,
                              "P2PK NOT with invalid sig and undefined hashtype", SCRIPT_VERIFY_STRICTENC
                             ).PushSig(keys.key1, 5).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                               "3-of-3 with nonzero dummy but no NULLDUMMY", 0
                              ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG,
                              "3-of-3 with nonzero dummy", SCRIPT_VERIFY_NULLDUMMY
                             ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2));
    good.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                               "3-of-3 NOT with invalid sig and nonzero dummy but no NULLDUMMY", 0
                              ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2).DamagePush(10));
    bad.push_back(TestBuilder(CScript() << OP_3 << ToByteVector(keys.pubkey0C) << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey2C) << OP_3 << OP_CHECKMULTISIG << OP_NOT,
                              "3-of-3 NOT with invalid sig with nonzero dummy", SCRIPT_VERIFY_NULLDUMMY
                             ).Num(1).PushSig(keys.key0).PushSig(keys.key1).PushSig(keys.key2).DamagePush(10));

    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "2-of-2 with two identical keys and sigs pushed using OP_DUP but no SIGPUSHONLY", 0
                              ).Num(0).PushSig(keys.key1).Add(CScript() << OP_DUP));
    bad.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                              "2-of-2 with two identical keys and sigs pushed using OP_DUP", SCRIPT_VERIFY_SIGPUSHONLY
                             ).Num(0).PushSig(keys.key1).Add(CScript() << OP_DUP));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2SH(P2PK) with non-push scriptSig but no SIGPUSHONLY", 0
                             ).PushSig(keys.key2).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey2C) << OP_CHECKSIG,
                              "P2SH(P2PK) with non-push scriptSig", SCRIPT_VERIFY_SIGPUSHONLY
                             ).PushSig(keys.key2).PushRedeem());
    good.push_back(TestBuilder(CScript() << OP_2 << ToByteVector(keys.pubkey1C) << ToByteVector(keys.pubkey1C) << OP_2 << OP_CHECKMULTISIG,
                               "2-of-2 with two identical keys and sigs pushed", SCRIPT_VERIFY_SIGPUSHONLY
                              ).Num(0).PushSig(keys.key1).PushSig(keys.key1));

    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2PK with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_P2SH
                              ).Num(11).PushSig(keys.key0));
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2PK with unnecessary input", SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH
                             ).Num(11).PushSig(keys.key0));
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2SH with unnecessary input but no CLEANSTACK", SCRIPT_VERIFY_P2SH, true
                              ).Num(11).PushSig(keys.key0).PushRedeem());
    bad.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                              "P2SH with unnecessary input", SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH, true
                             ).Num(11).PushSig(keys.key0).PushRedeem());
    good.push_back(TestBuilder(CScript() << ToByteVector(keys.pubkey0) << OP_CHECKSIG,
                               "P2SH with CLEANSTACK", SCRIPT_VERIFY_CLEANSTACK | SCRIPT_VERIFY_P2SH, true
                              ).PushSig(keys.key0).PushRedeem());


    set<string> tests_good;
    set<string> tests_bad;

    {
        UniValue json_good = read_json(string(json_tests::script_valid, json_tests::script_valid + sizeof(json_tests::script_valid)));
        UniValue json_bad = read_json(string(json_tests::script_invalid, json_tests::script_invalid + sizeof(json_tests::script_invalid)));

        for (size_t idx = 0; idx < json_good.size(); idx++) {
            const UniValue& tv = json_good[idx];
            tests_good.insert(tv.get_array().write());
        }
        for (size_t idx = 0; idx < json_bad.size(); idx++) {
            const UniValue& tv = json_bad[idx];
            tests_bad.insert(tv.get_array().write());
        }
    }

    string strGood;
    string strBad;

    for(auto& test : good)
    {
        test.Test(true);
        string str = test.GetJSON().write();
#ifndef UPDATE_JSON_TESTS
        if (tests_good.count(str) == 0) {
            EXPECT_FALSE(false) << "Missing auto script_valid test: " + test.GetComment();
        }
#endif
        strGood += str + ",\n";
    }
    for (auto& test : bad)
    {
        test.Test(false);
        string str = test.GetJSON().write();
#ifndef UPDATE_JSON_TESTS
        if (tests_bad.count(str) == 0) {
            EXPECT_FALSE(false) << "Missing auto script_invalid test: " + test.GetComment();
        }
#endif
        strBad += str + ",\n";
    }

#ifdef UPDATE_JSON_TESTS
    FILE* valid = fopen("script_valid.json.gen", "w");
    fputs(strGood.c_str(), valid);
    fclose(valid);
    FILE* invalid = fopen("script_invalid.json.gen", "w");
    fputs(strBad.c_str(), invalid);
    fclose(invalid);
#endif
}

class PTest_Script : public TestWithParam<int>
{};


// Parameterized testing over consensus branch ids
// Note: In the future, we could have different test data files based on epoch.
TEST_P(PTest_Script, script_valid)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));

    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Read tests from test/data/script_valid.json
    // Format is an array of arrays
    // Inner arrays are [ "scriptSig", "scriptPubKey", "flags" ]
    // ... where scriptSig and scriptPubKey are stringified
    // scripts.
    UniValue tests = read_json(string(json_tests::script_valid, json_tests::script_valid + sizeof(json_tests::script_valid)));

    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        string strTest = test.write();
        if (test.size() < 3) // Allow size > 3; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                EXPECT_TRUE(false) << "Bad test: " << strTest;
            }
            continue;
        }
        string scriptSigString = test[0].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        string scriptPubKeyString = test[1].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = ParseScriptFlags(test[2].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, consensusBranchId, true, strTest);
    }
}

// Parameterized testing over consensus branch ids
// Note: In the future, we could have different test data files based on epoch.
TEST_P(PTest_Script, script_invalid)
{    
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Scripts that should evaluate as invalid
    UniValue tests = read_json(string(json_tests::script_invalid, json_tests::script_invalid + sizeof(json_tests::script_invalid)));

    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        string strTest = test.write();
        if (test.size() < 3) // Allow size > 2; extra stuff ignored (useful for comments)
        {
            if (test.size() != 1) {
                EXPECT_TRUE(false) << "Bad test: " << strTest;
            }
            continue;
        }
        string scriptSigString = test[0].get_str();
        CScript scriptSig = ParseScript(scriptSigString);
        string scriptPubKeyString = test[1].get_str();
        CScript scriptPubKey = ParseScript(scriptPubKeyString);
        unsigned int scriptflags = ParseScriptFlags(test[2].get_str());

        DoTest(scriptPubKey, scriptSig, scriptflags, consensusBranchId, false, strTest);
    }
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_Script, script_PushData)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Check that PUSHDATA1, PUSHDATA2, and PUSHDATA4 create the same value on
    // the stack as the 1-75 opcodes do.
    constexpr unsigned char direct[] = { 1, 0x5a };
    constexpr unsigned char pushdata1[] = { OP_PUSHDATA1, 1, 0x5a };
    constexpr unsigned char pushdata2[] = { OP_PUSHDATA2, 1, 0, 0x5a };
    constexpr unsigned char pushdata4[] = { OP_PUSHDATA4, 1, 0, 0, 0, 0x5a };

    ScriptError err;
    vector<v_uint8 > directStack;
    EXPECT_TRUE(EvalScript(directStack, CScript(&direct[0], &direct[sizeof(direct)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    vector<v_uint8 > pushdata1Stack;
    EXPECT_TRUE(EvalScript(pushdata1Stack, CScript(&pushdata1[0], &pushdata1[sizeof(pushdata1)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), consensusBranchId, &err));
    EXPECT_EQ(pushdata1Stack , directStack);
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    vector<v_uint8 > pushdata2Stack;
    EXPECT_TRUE(EvalScript(pushdata2Stack, CScript(&pushdata2[0], &pushdata2[sizeof(pushdata2)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), consensusBranchId, &err));
    EXPECT_EQ(pushdata2Stack , directStack);
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    vector<v_uint8 > pushdata4Stack;
    EXPECT_TRUE(EvalScript(pushdata4Stack, CScript(&pushdata4[0], &pushdata4[sizeof(pushdata4)]), SCRIPT_VERIFY_P2SH, BaseSignatureChecker(), consensusBranchId, &err));
    EXPECT_EQ(pushdata4Stack , directStack);
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);
}

CScript
sign_multisig(CScript scriptPubKey, vector<CKey> keys, CTransaction transaction, uint32_t consensusBranchId)
{
    uint256 hash = SignatureHash(scriptPubKey, transaction, 0, to_integral_type(SIGHASH::ALL), 0, consensusBranchId);

    CScript result;
    //
    // NOTE: CHECKMULTISIG has an unfortunate bug; it requires
    // one extra item on the stack, before the signatures.
    // Putting OP_0 on the stack is the workaround;
    // fixing the bug would mean splitting the block chain (old
    // clients would not accept new CHECKMULTISIG transactions,
    // and vice-versa)
    //
    result << OP_0;
    for (const auto &key : keys)
    {
        v_uint8 vchSig;
        EXPECT_TRUE(key.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)to_integral_type(SIGHASH::ALL));
        result << vchSig;
    }
    return result;
}
CScript
sign_multisig(CScript scriptPubKey, const CKey &key, CTransaction transaction, uint32_t consensusBranchId)
{
    vector<CKey> keys;
    keys.push_back(key);
    return sign_multisig(scriptPubKey, keys, transaction, consensusBranchId);
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_Script, script_CHECKMULTISIG12)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    ScriptError err;
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);

    CScript scriptPubKey12;
    scriptPubKey12 << OP_1 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom12 = BuildCreditingTransaction(scriptPubKey12);
    CMutableTransaction txTo12 = BuildSpendingTransaction(CScript(), txFrom12);

    CScript goodsig1 = sign_multisig(scriptPubKey12, key1, txTo12, consensusBranchId);
    EXPECT_TRUE(VerifyScript(goodsig1, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);
    txTo12.vout[0].nValue = 2;
    EXPECT_TRUE(!VerifyScript(goodsig1, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);

    CScript goodsig2 = sign_multisig(scriptPubKey12, key2, txTo12, consensusBranchId);
    EXPECT_TRUE(VerifyScript(goodsig2, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    CScript badsig1 = sign_multisig(scriptPubKey12, key3, txTo12, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig1, scriptPubKey12, flags, MutableTransactionSignatureChecker(&txTo12, 0, txFrom12.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_Script, script_CHECKMULTISIG23)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    ScriptError err;
    CKey key1, key2, key3, key4;
    key1.MakeNewKey(true);
    key2.MakeNewKey(false);
    key3.MakeNewKey(true);
    key4.MakeNewKey(false);

    CScript scriptPubKey23;
    scriptPubKey23 << OP_2 << ToByteVector(key1.GetPubKey()) << ToByteVector(key2.GetPubKey()) << ToByteVector(key3.GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom23 = BuildCreditingTransaction(scriptPubKey23);
    CMutableTransaction txTo23 = BuildSpendingTransaction(CScript(), txFrom23);

    vector<CKey> keys;
    keys.push_back(key1); keys.push_back(key2);
    CScript goodsig1 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(VerifyScript(goodsig1, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key1); keys.push_back(key3);
    CScript goodsig2 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(VerifyScript(goodsig2, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key2); keys.push_back(key3);
    CScript goodsig3 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(VerifyScript(goodsig3, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key2); keys.push_back(key2); // Can't re-use sig
    CScript badsig1 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig1, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key2); keys.push_back(key1); // sigs must be in correct order
    CScript badsig2 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig2, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key3); keys.push_back(key2); // sigs must be in correct order
    CScript badsig3 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig3, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key4); keys.push_back(key2); // sigs must match pubkeys
    CScript badsig4 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig4, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);

    keys.clear();
    keys.push_back(key1); keys.push_back(key4); // sigs must match pubkeys
    CScript badsig5 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig5, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);

    keys.clear(); // Must have signatures
    CScript badsig6 = sign_multisig(scriptPubKey23, keys, txTo23, consensusBranchId);
    EXPECT_TRUE(!VerifyScript(badsig6, scriptPubKey23, flags, MutableTransactionSignatureChecker(&txTo23, 0, txFrom23.vout[0].nValue), consensusBranchId, &err));
    EXPECT_EQ(err , SCRIPT_ERR_INVALID_STACK_OPERATION) << ScriptErrorString(err);
}



// Parameterized testing over consensus branch ids
TEST_P(PTest_Script, script_combineSigs)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Test the CombineSignatures function
    CAmount amount = 0;
    CBasicKeyStore keystore;
    vector<CKey> keys;
    vector<CPubKey> pubkeys;
    for (int i = 0; i < 3; i++)
    {
        CKey key;
        key.MakeNewKey(i%2 == 1);
        keys.push_back(key);
        pubkeys.push_back(key.GetPubKey());
        keystore.AddKey(key);
    }

    CMutableTransaction txFrom = BuildCreditingTransaction(GetScriptForDestination(keys[0].GetPubKey().GetID()));
    CMutableTransaction txTo = BuildSpendingTransaction(CScript(), txFrom);
    CScript& scriptPubKey = txFrom.vout[0].scriptPubKey;
    CScript& scriptSig = txTo.vin[0].scriptSig;

    SignatureData empty;
    SignatureData combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, empty, consensusBranchId);
    EXPECT_TRUE(combined.scriptSig.empty());

    // Single signature case:
    SignSignature(keystore, txFrom, txTo, 0, to_integral_type(SIGHASH::ALL), consensusBranchId); // changes scriptSig
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSig), empty, consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, SignatureData(scriptSig), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);
    CScript scriptSigCopy = scriptSig;
    // Signing again will give a different, valid signature:
    SignSignature(keystore, txFrom, txTo, 0, to_integral_type(SIGHASH::ALL), consensusBranchId);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSigCopy), SignatureData(scriptSig), consensusBranchId);
    EXPECT_TRUE(combined.scriptSig == scriptSigCopy || combined.scriptSig == scriptSig);

    // P2SH, single-signature case:
    CScript pkSingle; pkSingle << ToByteVector(keys[0].GetPubKey()) << OP_CHECKSIG;
    keystore.AddCScript(pkSingle);
    scriptPubKey = GetScriptForDestination(CScriptID(pkSingle));
    SignSignature(keystore, txFrom, txTo, 0, to_integral_type(SIGHASH::ALL), consensusBranchId);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSig), empty, consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, SignatureData(scriptSig), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);
    scriptSigCopy = scriptSig;
    SignSignature(keystore, txFrom, txTo, 0, to_integral_type(SIGHASH::ALL), consensusBranchId);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSigCopy), SignatureData(scriptSig), consensusBranchId);
    EXPECT_TRUE(combined.scriptSig == scriptSigCopy || combined.scriptSig == scriptSig);
    // dummy scriptSigCopy with placeholder, should always choose non-placeholder:
    scriptSigCopy = CScript() << OP_0 << v_uint8(pkSingle.begin(), pkSingle.end());
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSigCopy), SignatureData(scriptSig), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSig), SignatureData(scriptSigCopy), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);

    // Hardest case:  Multisig 2-of-3
    scriptPubKey = GetScriptForMultisig(2, pubkeys);
    keystore.AddCScript(scriptPubKey);
    SignSignature(keystore, txFrom, txTo, 0, to_integral_type(SIGHASH::ALL), consensusBranchId);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(scriptSig), empty, consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), empty, SignatureData(scriptSig), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , scriptSig);

    // A couple of partially-signed versions:
    v_uint8 sig1;
    uint256 hash1 = SignatureHash(scriptPubKey, txTo, 0, to_integral_type(SIGHASH::ALL), 0, consensusBranchId);
    EXPECT_TRUE(keys[0].Sign(hash1, sig1));
    sig1.push_back(to_integral_type(SIGHASH::ALL));
    v_uint8 sig2;
    uint256 hash2 = SignatureHash(scriptPubKey, txTo, 0, to_integral_type(SIGHASH::NONE), 0, consensusBranchId);
    EXPECT_TRUE(keys[1].Sign(hash2, sig2));
    sig2.push_back(to_integral_type(SIGHASH::NONE));
    v_uint8 sig3;
    uint256 hash3 = SignatureHash(scriptPubKey, txTo, 0, to_integral_type(SIGHASH::SINGLE), 0, consensusBranchId);
    EXPECT_TRUE(keys[2].Sign(hash3, sig3));
    sig3.push_back(to_integral_type(SIGHASH::SINGLE));

    // Not fussy about order (or even existence) of placeholders or signatures:
    CScript partial1a = CScript() << OP_0 << sig1 << OP_0;
    CScript partial1b = CScript() << OP_0 << OP_0 << sig1;
    CScript partial2a = CScript() << OP_0 << sig2;
    CScript partial2b = CScript() << sig2 << OP_0;
    CScript partial3a = CScript() << sig3;
    CScript partial3b = CScript() << OP_0 << OP_0 << sig3;
    CScript partial3c = CScript() << OP_0 << sig3 << OP_0;
    CScript complete12 = CScript() << OP_0 << sig1 << sig2;
    CScript complete13 = CScript() << OP_0 << sig1 << sig3;
    CScript complete23 = CScript() << OP_0 << sig2 << sig3;

    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial1a), SignatureData(partial1b), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , partial1a);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial1a), SignatureData(partial2a), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , complete12);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial2a), SignatureData(partial1a), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , complete12);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial1b), SignatureData(partial2b), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , complete12);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial3b), SignatureData(partial1b), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , complete13);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial2a), SignatureData(partial3a), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , complete23);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial3b), SignatureData(partial2b), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , complete23);
    combined = CombineSignatures(scriptPubKey, MutableTransactionSignatureChecker(&txTo, 0, amount), SignatureData(partial3b), SignatureData(partial3a), consensusBranchId);
    EXPECT_EQ(combined.scriptSig , partial3c);
}

// Parameterized testing over consensus branch ids
TEST_P(PTest_Script, script_standard_push)
{
    const int sample = GetParam();
    EXPECT_TRUE(sample < static_cast<int>(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES));
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    ScriptError err;
    for (int i=0; i<67000; i++) {
        CScript script;
        script << i;
        EXPECT_TRUE(script.IsPushOnly()) << "Number " << i << " is not pure push.";
        EXPECT_TRUE(VerifyScript(script, CScript() << OP_1, SCRIPT_VERIFY_MINIMALDATA, BaseSignatureChecker(), consensusBranchId, &err)) << "Number " << i << " push is not minimal data.";
        EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);
    }

    for (unsigned int i=0; i<=MAX_SCRIPT_ELEMENT_SIZE; i++) {
        v_uint8 data(i, '\111'); //-V536
        CScript script;
        script << data;
        EXPECT_TRUE(script.IsPushOnly()) << "Length " << i << " is not pure push.";
        EXPECT_TRUE(VerifyScript(script, CScript() << OP_1, SCRIPT_VERIFY_MINIMALDATA, BaseSignatureChecker(), consensusBranchId, &err)) << "Length " << i << " push is not minimal data.";
        EXPECT_EQ(err , SCRIPT_ERR_OK) << ScriptErrorString(err);
    }
}

INSTANTIATE_TEST_SUITE_P(script, PTest_Script, Values(
    0,1,2,3
));

TEST(test_script, script_IsPushOnly_on_invalid_scripts)
{
    // IsPushOnly returns false when given a script containing only pushes that
    // are invalid due to truncation. IsPushOnly() is consensus critical
    // because P2SH evaluation uses it, although this specific behavior should
    // not be consensus critical as the P2SH evaluation would fail first due to
    // the invalid push. Still, it doesn't hurt to test it explicitly.
    constexpr unsigned char direct[] = { 1 };
    EXPECT_TRUE(!CScript(direct, direct+sizeof(direct)).IsPushOnly());
}

TEST(test_script, script_GetScriptAsm)
{
    EXPECT_EQ("OP_NOP2", ScriptToAsmStr(CScript() << OP_NOP2, true));
    EXPECT_EQ("OP_NOP2", ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY, true));
    EXPECT_EQ("OP_NOP2", ScriptToAsmStr(CScript() << OP_NOP2));
    EXPECT_EQ("OP_NOP2", ScriptToAsmStr(CScript() << OP_CHECKLOCKTIMEVERIFY));

    string derSig("304502207fa7a6d1e0ee81132a269ad84e68d695483745cde8b541e3bf630749894e342a022100c1f7ab20e13e22fb95281a870f3dcf38d782e53023ee313d741ad0cfbc0c5090");
    string pubKey("03b0da749730dc9b4b1f4a14d6902877a92541f5368778853d9c4a0cb7802dcfb2");
    v_uint8 vchPubKey = ToByteVector(ParseHex(pubKey));

    EXPECT_EQ(derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey, true));
    EXPECT_EQ(derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey, true));
    EXPECT_EQ(derSig + "[ALL] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey, true));
    EXPECT_EQ(derSig + "[NONE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey, true));
    EXPECT_EQ(derSig + "[SINGLE] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey, true));
    EXPECT_EQ(derSig + "[ALL|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey, true));
    EXPECT_EQ(derSig + "[NONE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey, true));
    EXPECT_EQ(derSig + "[SINGLE|ANYONECANPAY] " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey, true));

    EXPECT_EQ(derSig + "00 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "00")) << vchPubKey));
    EXPECT_EQ(derSig + "80 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "80")) << vchPubKey));
    EXPECT_EQ(derSig + "01 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "01")) << vchPubKey));
    EXPECT_EQ(derSig + "02 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "02")) << vchPubKey));
    EXPECT_EQ(derSig + "03 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "03")) << vchPubKey));
    EXPECT_EQ(derSig + "81 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "81")) << vchPubKey));
    EXPECT_EQ(derSig + "82 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "82")) << vchPubKey));
    EXPECT_EQ(derSig + "83 " + pubKey, ScriptToAsmStr(CScript() << ToByteVector(ParseHex(derSig + "83")) << vchPubKey));
}
