#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "chainparams.h"
#include "key.h"
#include "miner.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <boost/optional.hpp>

using ::testing::Return;

#ifdef ENABLE_WALLET
class MockReserveKey : public CReserveKey {
public:
    MockReserveKey() : CReserveKey(nullptr) { }

    MOCK_METHOD1(GetReservedKey, bool(CPubKey &pubkey));
};
#endif

TEST(Miner, GetMinerScriptPubKey) {
    SelectParams(CBaseChainParams::MAIN);

    boost::optional<CScript> scriptPubKey;
#ifdef ENABLE_WALLET
    MockReserveKey reservekey;
    EXPECT_CALL(reservekey, GetReservedKey(::testing::_))
        .WillRepeatedly(Return(false));
#endif

    // No miner address set
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_FALSE((bool) scriptPubKey);

    mapArgs["-mineraddress"] = "notAnAddress";
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_FALSE((bool) scriptPubKey);

    // Partial address
    mapArgs["-mineraddress"] = "AeWFpn4BfwQk3S6hVyDBF";
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_FALSE((bool) scriptPubKey);

    // Typo in address
    mapArgs["-mineraddress"] = "AeWFpn48fwQk3S6hVyDBFeh9PyTFATwAUuz"; //48 instead of 4B
    
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_FALSE((bool) scriptPubKey);

    // Set up expected scriptPubKey for AeWFpn4BfwQk3S6hVyDBFeh9PyTFATwAUuz
    CKeyID keyID;
    keyID.SetHex("6bc0b8196c163a95b5aece93b8efeebdd083a77d");
    CScript expectedScriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;

    // Valid address
    mapArgs["-mineraddress"] = "AeWFpn4BfwQk3S6hVyDBFeh9PyTFATwAUuz";
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_TRUE((bool) scriptPubKey);
    EXPECT_EQ(expectedScriptPubKey, *scriptPubKey);

    // Valid address with leading whitespace
    mapArgs["-mineraddress"] = "  AeWFpn4BfwQk3S6hVyDBFeh9PyTFATwAUuz";
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_TRUE((bool) scriptPubKey);
    EXPECT_EQ(expectedScriptPubKey, *scriptPubKey);

    // Valid address with trailing whitespace
    mapArgs["-mineraddress"] = "AeWFpn4BfwQk3S6hVyDBFeh9PyTFATwAUuz  ";
#ifdef ENABLE_WALLET
    scriptPubKey = GetMinerScriptPubKey(reservekey);
#else
    scriptPubKey = GetMinerScriptPubKey();
#endif
    EXPECT_TRUE((bool) scriptPubKey);
    EXPECT_EQ(expectedScriptPubKey, *scriptPubKey);
}
