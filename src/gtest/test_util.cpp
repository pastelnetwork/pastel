// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <vector>
#include <unistd.h>

#include <gtest/gtest.h>

#include <util.h>
#include <clientversion.h>
#include <primitives/transaction.h>
#include <random.h>
#include <sync.h>
#include <utilstrencodings.h>
#include <utilmoneystr.h>

using namespace std;
using namespace testing;

TEST(test_util, util_criticalsection)
{
    CCriticalSection cs;

    do {
        LOCK(cs);
        break; //-V612

        EXPECT_TRUE(false) << "break was swallowed!"; //-V779
    } while(0);

    do {
        TRY_LOCK(cs, lockTest);
        if (lockTest)
            break;

        EXPECT_TRUE(false) << "break was swallowed!";
    } while(0);
}

class PTest_Util: public TestWithParam<tuple<string, int64_t, string>>
{};

TEST_P(PTest_Util, util_DateTimeStrFormat)
{
    const auto &pszFormat = get<0>(GetParam());
    const auto &nTime = get<1>(GetParam());
    const auto &expected = get<2>(GetParam());
    EXPECT_EQ(DateTimeStrFormat(pszFormat.c_str(), nTime), expected);
}

INSTANTIATE_TEST_SUITE_P(util_DateTimeStrFormat, PTest_Util, Values(
    make_tuple("%Y-%m-%d %H:%M:%S", 0U, "1970-01-01 00:00:00"),
    make_tuple("%Y-%m-%d %H:%M:%S", 0x7FFFFFFFU, "2038-01-19 03:14:07"),
    make_tuple("%Y-%m-%d %H:%M:%S", 1317425777U, "2011-09-30 23:36:17"),
    make_tuple("%Y-%m-%d %H:%M", 1317425777U, "2011-09-30 23:36"),
    make_tuple("%a, %d %b %Y %H:%M:%S +0000", 1317425777U, "Fri, 30 Sep 2011 23:36:17 +0000")
));

TEST(test_util, util_ParseParameters)
{
    const char *argv_test[] = {"-ignored", "-a", "-b", "-ccc=argument", "-ccc=multiple", "f", "-d=e"};

    ParseParameters(0, (char**)argv_test);
    EXPECT_TRUE(mapArgs.empty() && mapMultiArgs.empty());

    ParseParameters(1, (char**)argv_test);
    EXPECT_TRUE(mapArgs.empty() && mapMultiArgs.empty());

    ParseParameters(5, (char**)argv_test);
    // expectation: -ignored is ignored (program name argument),
    // -a, -b and -ccc end up in map, -d ignored because it is after
    // a non-option argument (non-GNU option parsing)
    EXPECT_TRUE(mapArgs.size() == 3 && mapMultiArgs.size() == 3);
    EXPECT_TRUE(mapArgs.count("-a") && mapArgs.count("-b") && mapArgs.count("-ccc")
                && !mapArgs.count("f") && !mapArgs.count("-d"));
    EXPECT_TRUE(mapMultiArgs.count("-a") && mapMultiArgs.count("-b") && mapMultiArgs.count("-ccc")
                && !mapMultiArgs.count("f") && !mapMultiArgs.count("-d"));

    EXPECT_TRUE(mapArgs["-a"] == "" && mapArgs["-ccc"] == "multiple");
    EXPECT_TRUE(mapMultiArgs["-ccc"].size() == 2);
}

TEST(test_util, util_GetArg)
{
    mapArgs.clear();
    mapArgs["strtest1"] = "string...";
    // strtest2 undefined on purpose
    mapArgs["inttest1"] = "12345";
    mapArgs["inttest2"] = "81985529216486895";
    // inttest3 undefined on purpose
    mapArgs["booltest1"] = "";
    // booltest2 undefined on purpose
    mapArgs["booltest3"] = "0";
    mapArgs["booltest4"] = "1";

    EXPECT_EQ(GetArg("strtest1", "default"), "string...");
    EXPECT_EQ(GetArg("strtest2", "default"), "default");
    EXPECT_EQ(GetArg("inttest1", -1), 12345);
    EXPECT_EQ(GetArg("inttest2", -1), 81985529216486895LL);
    EXPECT_EQ(GetArg("inttest3", -1), -1);
    EXPECT_EQ(GetBoolArg("booltest1", false), true);
    EXPECT_EQ(GetBoolArg("booltest2", false), false);
    EXPECT_EQ(GetBoolArg("booltest3", false), false);
    EXPECT_EQ(GetBoolArg("booltest4", false), true);
}

class PTest_Util1: public TestWithParam<tuple<CAmount, string>>
{};

TEST_P(PTest_Util1, util_FormatMoney)
{
    const auto &amount = get<0>(GetParam());
    const auto &expected = get<1>(GetParam());
    
    EXPECT_EQ( FormatMoney( amount ), expected );
}

INSTANTIATE_TEST_SUITE_P(util_FormatMoney, PTest_Util1, Values(
    make_tuple(0, "0.00"),
    make_tuple((COIN/10000)*123456789, "12345.6789"),
    make_tuple(-COIN, "-1.00"),

    make_tuple(COIN*100000000, "100000000.00"),
    make_tuple(COIN*10000000, "10000000.00"),
    make_tuple(COIN*1000000, "1000000.00"),
    make_tuple(COIN*100000, "100000.00"),
    make_tuple(COIN*10000, "10000.00"),
    make_tuple(COIN*1000, "1000.00"),
    make_tuple(COIN*100, "100.00"),
    make_tuple(COIN*10, "10.00"),
    make_tuple(COIN, "1.00"),
    make_tuple(COIN/10, "0.10"),
    make_tuple(COIN/100, "0.01"),
    make_tuple(COIN/1000, "0.001"),
    make_tuple(COIN/10000, "0.0001"),
    make_tuple(COIN/100000, "0.00001")
));

class PTest_Util2: public TestWithParam<tuple<CAmount, string>>
{};

TEST_P(PTest_Util2, util_ParseMoney)
{
    static bool runOnce = true;
    CAmount ret = 0;
    
    const auto &amountStr = get<1>(GetParam());
    const auto &amountExpected = get<0>(GetParam());

    EXPECT_TRUE(ParseMoney(amountStr.c_str(), ret));
    EXPECT_EQ(ret, amountExpected);

    // Attempted 63 bit overflow should fail
    if( runOnce ) {
        runOnce = false;
        EXPECT_TRUE(!ParseMoney("92233720368.54775808", ret));
    }
}
INSTANTIATE_TEST_SUITE_P(util_ParseMoney, PTest_Util2, Values(
    make_tuple(0, "0.0"),
    make_tuple((COIN/10000)*123456789, "12345.6789"),
    make_tuple(COIN*100000000, "100000000.00"),
    make_tuple(COIN*10000000, "10000000.00"),
    make_tuple(COIN*1000000, "1000000.00"),
    make_tuple(COIN*100000, "100000.00"),
    make_tuple(COIN*10000, "10000.00"),
    make_tuple(COIN*1000, "1000.00"),
    make_tuple(COIN*100, "100.00"),
    make_tuple(COIN*10, "10.00"),
    make_tuple(COIN, "1.00"),
    make_tuple(COIN/10, "0.10"),
    make_tuple(COIN/100, "0.01"),
    make_tuple(COIN/1000, "0.001"),
    make_tuple(COIN/10000, "0.0001"),
    make_tuple(COIN/100000, "0.00001")
));

// BOOST_AUTO_TEST_CASE(util_IsHex)
// {
//     BOOST_CHECK(IsHex("00"));
//     BOOST_CHECK(IsHex("00112233445566778899aabbccddeeffAABBCCDDEEFF"));
//     BOOST_CHECK(IsHex("ff"));
//     BOOST_CHECK(IsHex("FF"));

//     BOOST_CHECK(!IsHex(""));
//     BOOST_CHECK(!IsHex("0"));
//     BOOST_CHECK(!IsHex("a"));
//     BOOST_CHECK(!IsHex("eleven"));
//     BOOST_CHECK(!IsHex("00xx00"));
//     BOOST_CHECK(!IsHex("0x0000"));
// }

// BOOST_AUTO_TEST_CASE(util_seed_insecure_rand)
// {
//     seed_insecure_rand(true);
//     for (int mod=2;mod<11;mod++)
//     {
//         int mask = 1;
//         // Really rough binomal confidence approximation.
//         int err = 30*10000./mod*sqrt((1./mod*(1-1./mod))/10000.);
//         //mask is 2^ceil(log2(mod))-1
//         while(mask<mod-1)mask=(mask<<1)+1;

//         int count = 0;
//         //How often does it get a zero from the uniform range [0,mod)?
//         for (int i = 0; i < 10000; i++) {
//             uint32_t rval;
//             do{
//                 rval=insecure_rand()&mask;
//             }while(rval>=(uint32_t)mod);
//             count += rval==0;
//         }
//         BOOST_CHECK(count<=10000/mod+err);
//         BOOST_CHECK(count>=10000/mod-err);
//     }
// }

// BOOST_AUTO_TEST_CASE(util_TimingResistantEqual)
// {
//     BOOST_CHECK(TimingResistantEqual(std::string(""), std::string("")));
//     BOOST_CHECK(!TimingResistantEqual(std::string("abc"), std::string("")));
//     BOOST_CHECK(!TimingResistantEqual(std::string(""), std::string("abc")));
//     BOOST_CHECK(!TimingResistantEqual(std::string("a"), std::string("aa")));
//     BOOST_CHECK(!TimingResistantEqual(std::string("aa"), std::string("a")));
//     BOOST_CHECK(TimingResistantEqual(std::string("abc"), std::string("abc")));
//     BOOST_CHECK(!TimingResistantEqual(std::string("abc"), std::string("aba")));
// }

// /* Test strprintf formatting directives.
//  * Put a string before and after to ensure sanity of element sizes on stack. */
// #define B "check_prefix"
// #define E "check_postfix"
// BOOST_AUTO_TEST_CASE(strprintf_numbers)
// {
//     int64_t s64t = -9223372036854775807LL; /* signed 64 bit test value */
//     uint64_t u64t = 18446744073709551615ULL; /* unsigned 64 bit test value */
//     BOOST_CHECK(strprintf("%s %d %s", B, s64t, E) == B" -9223372036854775807 " E);
//     BOOST_CHECK(strprintf("%s %u %s", B, u64t, E) == B" 18446744073709551615 " E);
//     BOOST_CHECK(strprintf("%s %x %s", B, u64t, E) == B" ffffffffffffffff " E);

//     size_t st = 12345678; /* unsigned size_t test value */
//     ssize_t sst = -12345678; /* signed size_t test value */
//     BOOST_CHECK(strprintf("%s %d %s", B, sst, E) == B" -12345678 " E);
//     BOOST_CHECK(strprintf("%s %u %s", B, st, E) == B" 12345678 " E);
//     BOOST_CHECK(strprintf("%s %x %s", B, st, E) == B" bc614e " E);

//     ptrdiff_t pt = 87654321; /* positive ptrdiff_t test value */
//     ptrdiff_t spt = -87654321; /* negative ptrdiff_t test value */
//     BOOST_CHECK(strprintf("%s %d %s", B, spt, E) == B" -87654321 " E);
//     BOOST_CHECK(strprintf("%s %u %s", B, pt, E) == B" 87654321 " E);
//     BOOST_CHECK(strprintf("%s %x %s", B, pt, E) == B" 5397fb1 " E);
// }
// #undef B
// #undef E

// /* Check for mingw/wine issue #3494
//  * Remove this test before time.ctime(0xffffffff) == 'Sun Feb  7 07:28:15 2106'
//  */
// BOOST_AUTO_TEST_CASE(gettime)
// {
//     BOOST_CHECK((GetTime() & ~0xFFFFFFFFLL) == 0);
// }

// BOOST_AUTO_TEST_CASE(test_FormatParagraph)
// {
//     BOOST_CHECK_EQUAL(FormatParagraph("", 79, 0), "");
//     BOOST_CHECK_EQUAL(FormatParagraph("test", 79, 0), "test");
//     BOOST_CHECK_EQUAL(FormatParagraph(" test", 79, 0), "test");
//     BOOST_CHECK_EQUAL(FormatParagraph("test test", 79, 0), "test test");
//     BOOST_CHECK_EQUAL(FormatParagraph("test test", 4, 0), "test\ntest");
//     BOOST_CHECK_EQUAL(FormatParagraph("testerde test ", 4, 0), "testerde\ntest");
//     BOOST_CHECK_EQUAL(FormatParagraph("test test", 4, 4), "test\n    test");
//     BOOST_CHECK_EQUAL(FormatParagraph("This is a very long test string. This is a second sentence in the very long test string."), "This is a very long test string. This is a second sentence in the very long\ntest string.");
// }

// BOOST_AUTO_TEST_CASE(test_FormatSubVersion)
// {
//     std::vector<std::string> comments;
//     comments.push_back(std::string("comment1"));
//     std::vector<std::string> comments2;
//     comments2.push_back(std::string("comment1"));
//     comments2.push_back(SanitizeString(std::string("Comment2; .,_?@; !\"#$%&'()*+-/<=>[]\\^`{|}~"), SAFE_CHARS_UA_COMMENT)); // Semicolon is discouraged but not forbidden by BIP-0014
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, std::vector<std::string>()), std::string("/Test:0.9.99-beta1/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99924, std::vector<std::string>()), std::string("/Test:0.9.99-beta25/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99925, std::vector<std::string>()), std::string("/Test:0.9.99-rc1/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99949, std::vector<std::string>()), std::string("/Test:0.9.99-rc25/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99950, std::vector<std::string>()), std::string("/Test:0.9.99/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99951, std::vector<std::string>()), std::string("/Test:0.9.99-1/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99999, std::vector<std::string>()), std::string("/Test:0.9.99-49/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, comments),  std::string("/Test:0.9.99-beta1(comment1)/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99950, comments),  std::string("/Test:0.9.99(comment1)/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99900, comments2), std::string("/Test:0.9.99-beta1(comment1; Comment2; .,_?@; )/"));
//     BOOST_CHECK_EQUAL(FormatSubVersion("Test", 99950, comments2), std::string("/Test:0.9.99(comment1; Comment2; .,_?@; )/"));
// }

// BOOST_AUTO_TEST_SUITE_END()
