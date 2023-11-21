// Copyright (c) 2013 The Bitcoin Core developers
// Copyright (c) 2021-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for alert system
//

#include <fstream>
#include <iostream>

#include <gtest/gtest.h>

#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <utils/serialize.h>
#include <utils/streams.h>
#include <alert.h>
#include <chain.h>
#include <chainparams.h>
#include <clientversion.h>
#include <data/alertTests.raw.h>
#include <main.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <key.h>
#include <alertkeys.h>

using namespace std;
using namespace testing;

/*
 * If the alert key pairs have changed, the test suite will fail as the
 * test data is now invalid.  To create valid test data, signed with a
 * new alert private key, follow these steps:
 *
 * 1. Copy your private key into alertkeys.h.  Don't commit this file!
 *    See sendalert.cpp for more info.
 *
 * 2. Set the GENERATE_ALERTS_FLAG to true.
 *
 * 3. Build and run:
 *    pastel_gtest -t Generate_Alert_Test_Data
 *
 * 4. Test data is saved in your current directory as alertTests.raw.NEW
 *    Copy this file to: src/test/data/alertTests.raw
 *
 *    For debugging purposes, terminal output can be copied into:
 *    src/test/data/alertTests.raw.h
 *
 * 5. Clean up...
 *    - Set GENERATE_ALERTS_FLAG back to false.
 *    - Remove your private key from alertkeys.h
 *
 * 6. Build and verify the new test data:
 *    test_bitcoin -t Alert_tests
 *
 */
#define GENERATE_ALERTS_FLAG false

#if GENERATE_ALERTS_FLAG

// NOTE:
// A function SignAndSave() was used by Bitcoin Core to create alert test data
// but it has not been made publicly available.  So instead, we have adapted
// some publicly available code which achieves the intended result:
// https://gist.github.com/lukem512/9b272bd35e2cdefbf386


// Code to output a C-style array of values
template<typename T>
string HexStrArray(const T itbegin, const T itend, int lineLength)
{
    string rv;
    static const char hexmap[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
    rv.reserve((itend-itbegin)*3);
    int i = 0;
    for(T it = itbegin; it < itend; ++it)
    {
        unsigned char val = (unsigned char)(*it);
        if(it != itbegin)
        {
            if (i % lineLength == 0)
                rv.push_back('\n');
            else
                rv.push_back(' ');
        }
        rv.push_back('0');
        rv.push_back('x');
        rv.push_back(hexmap[val>>4]);
        rv.push_back(hexmap[val&15]);
        rv.push_back(',');
        i++;
    }

    return rv;
}

template<typename T>
inline string HexStrArray(const T& vch, int lineLength)
{
    return HexStrArray(vch.begin(), vch.end(), lineLength);
}

// Sign CAlert with alert private key
bool SignAlert(CAlert &alert)
{
    // serialize alert data
    CDataStream sMsg(SER_NETWORK, PROTOCOL_VERSION);
    sMsg << *(CUnsignedAlert*)&alert;
    alert.vchMsg = v_uint8(sMsg.begin(), sMsg.end());

    // sign alert
    v_uint8 vchTmp(ParseHex(pszPrivKey));
    CPrivKey vchPrivKey(vchTmp.begin(), vchTmp.end());
    CKey key;
    if (!key.SetPrivKey(vchPrivKey, false))
    {
        printf("key.SetPrivKey failed\n");
        return false;
    }
    if (!key.Sign(Hash(alert.vchMsg.begin(), alert.vchMsg.end()), alert.vchSig))
    {
        printf("SignAlert() : key.Sign failed\n");
        return false;
    }
    return true;
}

// Sign a CAlert and serialize it
bool SignAndSerialize(CAlert &alert, CDataStream &buffer)
{
    // Sign
    if(!SignAlert(alert))
    {
        printf("SignAndSerialize() : could not sign alert\n");
        return false;
    }
    // ...and save!
    buffer << alert;
    return true;
}

void GenerateAlertTests()
{
    CDataStream sBuffer(SER_DISK, CLIENT_VERSION);

    CAlert alert;
    alert.nRelayUntil   = 60;
    alert.nExpiration   = 24 * 60 * 60;
    alert.nID           = 1;
    alert.nCancel       = 0;  // cancels previous messages up to this ID number
    alert.nMinVer       = 0;  // These versions are protocol versions
    alert.nMaxVer       = 999001;
    alert.nPriority     = 1;
    alert.strComment    = "Alert comment";
    alert.strStatusBar  = "Alert 1";

    // Replace SignAndSave with SignAndSerialize
    SignAndSerialize(alert, sBuffer);

    // More tests go here ...
    alert.setSubVer.insert(string("/MagicBean:0.1.0/"));
    alert.strStatusBar  = "Alert 1 for MagicBean 0.1.0";
    SignAndSerialize(alert, sBuffer);

    alert.setSubVer.insert(string("/MagicBean:0.2.0/"));
    alert.strStatusBar  = "Alert 1 for MagicBean 0.1.0, 0.2.0";
    SignAndSerialize(alert, sBuffer);

    alert.setSubVer.clear();
    ++alert.nID;
    alert.nCancel = 1;
    alert.nPriority = 100;
    alert.strStatusBar  = "Alert 2, cancels 1";
    SignAndSerialize(alert, sBuffer);

    alert.nExpiration += 60;
    ++alert.nID;
    SignAndSerialize(alert, sBuffer);

    ++alert.nID;
    alert.nPriority = 5000;
    alert.strStatusBar  = "Alert 3, disables RPC";
    alert.strRPCError = "RPC disabled";
    SignAndSerialize(alert, sBuffer);

    ++alert.nID;
    alert.nPriority = 5000;
    alert.strStatusBar  = "Alert 4, re-enables RPC";
    alert.strRPCError = "";
    SignAndSerialize(alert, sBuffer);

    ++alert.nID;
    alert.nMinVer = 11;
    alert.nMaxVer = 22;
    alert.nPriority = 100;
    SignAndSerialize(alert, sBuffer);

    ++alert.nID;
    alert.strStatusBar  = "Alert 2 for MagicBean 0.1.0";
    alert.setSubVer.insert(string("/MagicBean:0.1.0/"));
    SignAndSerialize(alert, sBuffer);

    ++alert.nID;
    alert.nMinVer = 0;
    alert.nMaxVer = 999999;
    alert.strStatusBar  = "Evil Alert'; /bin/ls; echo '";
    alert.setSubVer.clear();
    bool b = SignAndSerialize(alert, sBuffer);

    if (b) {
        // Print the hex array, which will become the contents of alertTest.raw.h
        v_uint8 vch = v_uint8(sBuffer.begin(), sBuffer.end());
        printf("%s\n", HexStrArray(vch, 8).c_str());

        // Write the data to alertTests.raw.NEW, to be copied to src/test/data/alertTests.raw
        ofstream outfile("alertTests.raw.NEW", ios::out | ios::binary);
        outfile.write((const char*)&vch[0], vch.size());
        outfile.close();
    }
}

struct GenerateAlertTestsFixture : public Test {
};

TEST_F(GenerateAlertTestsFixture, GenerateTheAlertTests)
{
    GenerateAlertTests();
}

#else

struct ReadAlerts : public Test
{
    ReadAlerts()
    {
        v_uint8 vch(alert_tests::alertTests, alert_tests::alertTests + sizeof(alert_tests::alertTests));
        CDataStream stream(vch, SER_DISK, CLIENT_VERSION);
        try {
            while (!stream.eof())
            {
                CAlert alert;
                stream >> alert;
                alerts.push_back(alert);
            }
        }
        catch (const exception&) { }
    }
    ~ReadAlerts() { }

    static v_strings read_lines(fs::path filepath)
    {
        v_strings result;

        ifstream f(filepath.string().c_str());
        string line;
        while (getline(f,line))
            result.push_back(line);

        return result;
    }

    vector<CAlert> alerts;
};

TEST_F(ReadAlerts, AlertApplies)
{
    SetMockTime(11);
    auto pChainParams = CreateChainParams(ChainNetwork::MAIN);
    const auto& alertKey = pChainParams->AlertKey();

    for (const auto& alert : alerts)
    {
        EXPECT_TRUE(alert.CheckSignature(alertKey));
    }

    EXPECT_TRUE(alerts.size() >= 3);

    // Matches:
    EXPECT_TRUE(alerts[0].AppliesTo(1, ""));
    EXPECT_TRUE(alerts[0].AppliesTo(999001, ""));
    EXPECT_TRUE(alerts[0].AppliesTo(1, "/MagicBean:11.11.11/"));

    EXPECT_TRUE(alerts[1].AppliesTo(1, "/MagicBean:0.1.0/"));
    EXPECT_TRUE(alerts[1].AppliesTo(999001, "/MagicBean:0.1.0/"));

    EXPECT_TRUE(alerts[2].AppliesTo(1, "/MagicBean:0.1.0/"));
    EXPECT_TRUE(alerts[2].AppliesTo(1, "/MagicBean:0.2.0/"));

    // Don't match:
    EXPECT_TRUE(!alerts[0].AppliesTo(-1, ""));
    EXPECT_TRUE(!alerts[0].AppliesTo(999002, ""));

    EXPECT_TRUE(!alerts[1].AppliesTo(1, ""));
    EXPECT_TRUE(!alerts[1].AppliesTo(1, "MagicBean:0.1.0"));
    EXPECT_TRUE(!alerts[1].AppliesTo(1, "/MagicBean:0.1.0"));
    EXPECT_TRUE(!alerts[1].AppliesTo(1, "MagicBean:0.1.0/"));
    EXPECT_TRUE(!alerts[1].AppliesTo(-1, "/MagicBean:0.1.0/"));
    EXPECT_TRUE(!alerts[1].AppliesTo(999002, "/MagicBean:0.1.0/"));
    EXPECT_TRUE(!alerts[1].AppliesTo(1, "/MagicBean:0.2.0/"));

    EXPECT_TRUE(!alerts[2].AppliesTo(1, "/MagicBean:0.3.0/"));

    SetMockTime(0);
}


TEST_F(ReadAlerts, AlertNotify)
{
    SetMockTime(11);
    auto pChainParams = CreateChainParams(ChainNetwork::MAIN);
    const auto& alertKey = pChainParams->AlertKey();

    fs::path temp = GetTempPath() /
        fs::unique_path("alertnotify-%%%%.txt");

    mapArgs["-alertnotify"] = string("echo %s >> ") + temp.string();

    for (auto &alert : alerts)
        alert.ProcessAlert(alertKey, false);

    vector<string> r = read_lines(temp);
    EXPECT_EQ(r.size(), 6u);

// Windows built-in echo semantics are different than posixy shells. Quotes and
// whitespace are printed literally.

#ifndef WIN32
    EXPECT_EQ(r[0], "Alert 1");
    EXPECT_EQ(r[1], "Alert 2, cancels 1");
    EXPECT_EQ(r[2], "Alert 2, cancels 1");
    EXPECT_EQ(r[3], "Alert 3, disables RPC");
    EXPECT_EQ(r[4], "Alert 4, reenables RPC"); // dashes should be removed
    EXPECT_EQ(r[5], "Evil Alert; /bin/ls; echo "); // single-quotes should be removed
#else
    EXPECT_EQ(r[0], "'Alert 1' ");
    EXPECT_EQ(r[1], "'Alert 2, cancels 1' ");
    EXPECT_EQ(r[2], "'Alert 2, cancels 1' ");
    EXPECT_EQ(r[3], "'Alert 3, disables RPC' ");
    EXPECT_EQ(r[4], "'Alert 4, reenables RPC' "); // dashes should be removed
    EXPECT_EQ(r[5], "'Evil Alert; /bin/ls; echo ' ");
#endif
    fs::remove(temp);

    SetMockTime(0);
    mapAlerts.clear();
}

TEST_F(ReadAlerts, AlertDisablesRPC)
{
    SetMockTime(11);
    auto pChainParams = CreateChainParams(ChainNetwork::MAIN);
    const auto& alertKey = pChainParams->AlertKey();

    // Command should work before alerts
    EXPECT_EQ(GetWarnings("rpc"), "");

    // First alert should disable RPC
    alerts[5].ProcessAlert(alertKey, false);
    EXPECT_EQ(alerts[5].strRPCError, "RPC disabled");
    EXPECT_EQ(GetWarnings("rpc"), "RPC disabled");

    // Second alert should re-enable RPC
    alerts[6].ProcessAlert(alertKey, false);
    EXPECT_EQ(alerts[6].strRPCError, "");
    EXPECT_EQ(GetWarnings("rpc"), "");

    SetMockTime(0);
    mapAlerts.clear();
}

static bool InitialDownloadCheckFalseFunc(const Consensus::Params &) { return false; }
#define GTEST_COUT cerr << "[          ] "

TEST_F(ReadAlerts, PartitionAlert)
{
    // Test PartitionCheck
    CCriticalSection csDummy;
    CBlockIndex indexDummy[400];
    auto pChainParams = CreateChainParams(ChainNetwork::MAIN);
    const auto& consensusParams = pChainParams->GetConsensus();
    int64_t nPowTargetSpacing = consensusParams.nPowTargetSpacing;

    // Generate fake blockchain timestamps relative to
    // an arbitrary time:
    int64_t now = 1427379054;
    SetMockTime(now);
    for (int i = 0; i < 400; i++)
    {
        indexDummy[i].phashBlock = nullptr;
        if (i == 0)
            indexDummy[i].pprev = nullptr;
        else indexDummy[i].pprev = &indexDummy[i-1];
        indexDummy[i].nHeight = i;
        indexDummy[i].nTime = static_cast<unsigned int>(now - (400-i)*nPowTargetSpacing);
        // Other members don't matter, the partition check code doesn't
        // use them
    }


    // Test 1: chain with blocks every nPowTargetSpacing seconds,
    // as normal, no worries:
    PartitionCheck(consensusParams, InitialDownloadCheckFalseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    EXPECT_TRUE(strMiscWarning.empty());

    // Test 2: go 3.5 hours without a block, expect a warning:
    now += 3*60*60+30*60;
    SetMockTime(now);
    PartitionCheck(consensusParams, InitialDownloadCheckFalseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    EXPECT_TRUE(!strMiscWarning.empty());
    GTEST_COUT << string("Got alert text: ") << strMiscWarning << endl;
    strMiscWarning = "";

    // Test 3: test the "partition alerts only go off once per day"
    // code:
    now += 60*10;
    SetMockTime(now);
    PartitionCheck(consensusParams, InitialDownloadCheckFalseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    EXPECT_TRUE(strMiscWarning.empty());

    // Test 4: get 2.5 times as many blocks as expected:
    now += 60*60*24; // Pretend it is a day later
    SetMockTime(now);
    int64_t quickSpacing = nPowTargetSpacing*2/5;
    for (int i = 0; i < 400; i++) // Tweak chain timestamps:
        indexDummy[i].nTime = static_cast<unsigned int>(now - (400-i)*quickSpacing);
    PartitionCheck(consensusParams, InitialDownloadCheckFalseFunc, csDummy, &indexDummy[399], nPowTargetSpacing);
    EXPECT_TRUE(!strMiscWarning.empty());
    GTEST_COUT << string("Got alert text: ") << strMiscWarning << endl;
    strMiscWarning = "";

    SetMockTime(0);
}

#endif
