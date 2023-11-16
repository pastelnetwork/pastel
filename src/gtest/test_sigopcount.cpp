// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <vector>

#include <gtest/gtest.h>

#include <utils/uint256.h>
#include <pubkey.h>
#include <key.h>
#include <script/script.h>
#include <script/standard.h>

using namespace std;
using namespace testing;

// Helpers:
static v_uint8
Serialize(const CScript& s)
{
    v_uint8 sSerialized(s.begin(), s.end());
    return sSerialized;
}


TEST(test_sigopcount, GetSigOpCount)
{
    // Test CScript::GetSigOpCount()
    CScript s1;
    EXPECT_EQ(s1.GetSigOpCount(false), 0U);
    EXPECT_EQ(s1.GetSigOpCount(true), 0U);

    uint160 dummy;
    s1 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << OP_2 << OP_CHECKMULTISIG;
    EXPECT_EQ(s1.GetSigOpCount(true), 2U);
    s1 << OP_IF << OP_CHECKSIG << OP_ENDIF;
    EXPECT_EQ(s1.GetSigOpCount(true), 3U);
    EXPECT_EQ(s1.GetSigOpCount(false), 21U);

    CScript p2sh = GetScriptForDestination(CScriptID(s1));
    CScript scriptSig;
    scriptSig << OP_0 << Serialize(s1);
    EXPECT_EQ(p2sh.GetSigOpCount(scriptSig), 3U);

    vector<CPubKey> keys;
    for (int i = 0; i < 3; i++)
    {
        CKey k;
        k.MakeNewKey(true);
        keys.push_back(k.GetPubKey());
    }
    CScript s2 = GetScriptForMultisig(1, keys);
    EXPECT_EQ(s2.GetSigOpCount(true), 3U);
    EXPECT_EQ(s2.GetSigOpCount(false), 20U);

    p2sh = GetScriptForDestination(CScriptID(s2));
    EXPECT_EQ(p2sh.GetSigOpCount(true), 0U);
    EXPECT_EQ(p2sh.GetSigOpCount(false), 0U);
    CScript scriptSig2;
    scriptSig2 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << Serialize(s2);
    EXPECT_EQ(p2sh.GetSigOpCount(scriptSig2), 3U);
}
