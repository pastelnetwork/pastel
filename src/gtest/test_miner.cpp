#include <gmock/gmock.h>

#include "chainparams.h"
#include "key.h"
#include "miner.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <optional>

using ::testing::Return;

#ifdef ENABLE_WALLET
class MockReserveKey : public CReserveKey {
public:
    MockReserveKey() : CReserveKey(nullptr) { }

    MOCK_METHOD(bool, GetReservedKey, (CPubKey &pubkey), ());
};
#endif

TEST(Miner, GetMinerScriptPubKey) {
    SelectParams(CBaseChainParams::Network::MAIN);

    const auto& chainparams = Params();
    
#ifdef ENABLE_WALLET
    MockReserveKey reservekey;
    EXPECT_CALL(reservekey, GetReservedKey(::testing::_))
        .WillRepeatedly(Return(false));
#endif

    const auto TestGetMinerScriptPubKey = [&]() -> std::optional<CScript>
    {
#ifdef ENABLE_WALLET
        return GetMinerScriptPubKey(reservekey, chainparams);
#else
        return GetMinerScriptPubKey(chainparams);
#endif
    };

    // No miner address set
    auto scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_FALSE((bool) scriptPubKey);

    mapArgs["-mineraddress"] = "notAnAddress";
    scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_FALSE((bool) scriptPubKey);

    // Partial address
    mapArgs["-mineraddress"] = "Ptq6hqeeAXta25PGaKHs1";
    scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_FALSE((bool) scriptPubKey);

    // Typo in address
    mapArgs["-mineraddress"] = "Ptq6hqeeAXta25PGaKHs1ymktHbEbBugxeG"; //bB instead of b8
    scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_FALSE((bool) scriptPubKey);

    // Set up expected scriptPubKey for Ptq6hqeeAXta25PGaKHs1ymktHbEb8ugxeG
    CKeyID keyID;
    keyID.SetHex("9E7848625B3B465D273EC83851907A143B483BF2");
    CScript expectedScriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;

    // Valid address
    mapArgs["-mineraddress"] = "Ptq6hqeeAXta25PGaKHs1ymktHbEb8ugxeG";
    scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_TRUE((bool) scriptPubKey);
    EXPECT_EQ(expectedScriptPubKey, *scriptPubKey);

    // Valid address with leading whitespace
    mapArgs["-mineraddress"] = "  Ptq6hqeeAXta25PGaKHs1ymktHbEb8ugxeG";
    scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_TRUE((bool) scriptPubKey);
    EXPECT_EQ(expectedScriptPubKey, *scriptPubKey);

    // Valid address with trailing whitespace
    mapArgs["-mineraddress"] = "Ptq6hqeeAXta25PGaKHs1ymktHbEb8ugxeG  ";
    scriptPubKey = TestGetMinerScriptPubKey();
    EXPECT_TRUE((bool)scriptPubKey);
    EXPECT_EQ(expectedScriptPubKey, *scriptPubKey);
}
