#include <inttypes.h>

#include "utilstrencodings.h"
#include "tinyformat.h"
#include "vector_types.h"
#include "gtest/gtest.h"

using namespace testing;
using namespace std;

class PTest_ASCII85_Encode_Decode : 
    public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_ASCII85_Encode_Decode, ASCII85_Encode_Decode)
{
    const string to_be_encoded_1 = get<0>(GetParam()); //"hello"; // Encoded shall be : "BOu!rDZ"
    const string to_be_decoded_1 = get<1>(GetParam()); //"BOu!rDZ"; // Decoded shall be: "hello"
    string encoded = EncodeAscii85(to_be_encoded_1);
    string decoded = DecodeAscii85(to_be_decoded_1);

    EXPECT_EQ(to_be_encoded_1, decoded);
    EXPECT_EQ(to_be_decoded_1, encoded);
}

INSTANTIATE_TEST_SUITE_P(encode_decode_tests, 
    PTest_ASCII85_Encode_Decode, 
        Values(
            make_tuple("hello", "BOu!rDZ"), 
            make_tuple("how are you", "BQ&);@<,p%H#Ig"), 
            make_tuple("0x56307893281ndjnskdndsfhdsufiolm", "0R,H51GCaI3AWEM0lCN:DKBT(DIdg#BOl1,Anc1\"D#")
        ));

class PTest_ParseFixedPoint :
    public TestWithParam<tuple<string, int64_t, bool>>
{
public:
    inline static size_t m_nTestItem = 0;
};

TEST_P(PTest_ParseFixedPoint, test)
{
    ++m_nTestItem;
    int64_t nAmount = 0;
    const auto& param = GetParam();
    const bool bRet = ParseFixedPoint(get<0>(param), 8, &nAmount);
    EXPECT_EQ(bRet, get<2>(param)) << strprintf("ParseFixedPoint Item #%zu (%s)", m_nTestItem, get<0>(param));
    if (bRet)
        EXPECT_EQ(nAmount, get<1>(param)) << strprintf("ParseFixedPoint Item #%zu ('%s' -> " PRId64 ")", m_nTestItem, get<0>(param), get<1>(param));
}

INSTANTIATE_TEST_SUITE_P(ParseFixedPoint, PTest_ParseFixedPoint, 
    Values(
        make_tuple("0", 0, true),
        make_tuple("1", 100'000'000, true),
        make_tuple("0.0", 0, true),
        make_tuple("-0.1", -10'000'000, true), 
        make_tuple("1.1", 110'000'000, true), 
        make_tuple("1.10000000000000000", 110'000'000, true), 
        make_tuple("1.1e1", 1'100'000'000, true),
        make_tuple("1.1e-1", 11'000'000, true),
        make_tuple("1000", 100'000'000'000, true), 
        make_tuple("-1000", -100'000'000'000, true), 
        make_tuple("0.00000001", 1, true),
        make_tuple("0.0000000100000000", 1, true),
        make_tuple("-0.00000001", -1, true), 
        make_tuple("1000000000.00000001", 100'000'000'000'000'001, true), 
        make_tuple("9999999999.99999999", 999'999'999'999'999'999, true),
        make_tuple("-9999999999.99999999", -999'999'999'999'999'999, true),

        make_tuple("", 0, false), 
        make_tuple("-", 0, false),
        make_tuple("a-1000", 0, false), 
        make_tuple("-a1000", 0, false),
        make_tuple("-1000a", 0, false), 
        make_tuple("-01000", 0, false),
        make_tuple("00.1", 0, false), 
        make_tuple(".1", 0, false),
        make_tuple("--0.1", 0, false), 
        make_tuple("0.000000001", 0, false),
        make_tuple("-0.000000001", 0, false), 
        make_tuple("-0.000000001", 0, false),
        make_tuple("0.00000001000000001", 0, false),
        make_tuple("-10000000000.00000000", 0, false),
        make_tuple("10000000000.00000000", 0, false),
        make_tuple("-10000000000.00000001", 0, false),
        make_tuple("10000000000.00000001", 0, false),
        make_tuple("-10000000000.00000009", 0, false),
        make_tuple("10000000000.00000009", 0, false),
        make_tuple("-99999999999.99999999", 0, false),
        make_tuple("99999909999.09999999", 0, false),
        make_tuple("92233720368.54775807", 0, false),
        make_tuple("92233720368.54775808", 0, false),
        make_tuple("-92233720368.54775808", 0, false),
        make_tuple("-92233720368.54775809", 0, false),
        make_tuple("1.1e", 0, false),
        make_tuple("1.1e-", 0, false),
        make_tuple("1.", 0, false)
     ));

constexpr int32_t TEST_INT_NULLVALUE = -9999;
class PTest_ParseInt32 : public TestWithParam<
      tuple<
        string,  // input value
        int32_t, // result value to check (special value -99999 for test only: pass nullptr)
        bool>>    // expected function return bool value
{
public:
    inline static size_t m_nTestItem = 0;
};
 
TEST_P(PTest_ParseInt32, test)
{
    ++m_nTestItem;
    int32_t n = 0;
    const auto& param = GetParam();
    const int32_t nResult = get<1>(param);
    const bool bRet = ParseInt32(get<0>(param), (nResult == TEST_INT_NULLVALUE) ? nullptr : &n);
    EXPECT_EQ(bRet, get<2>(param)) << strprintf("ParseInt32 Item #%zu (%s)", m_nTestItem, get<0>(param));
    if (bRet && (nResult != TEST_INT_NULLVALUE))
        EXPECT_EQ(n, nResult) << strprintf("ParseInt32 Item #%zu ('%s' -> %f)", m_nTestItem, get<0>(param), nResult);
}

INSTANTIATE_TEST_SUITE_P(ParseInt32, PTest_ParseInt32,
    Values(
        // valid values
        make_tuple("1234", TEST_INT_NULLVALUE, true),
        make_tuple("0", 0, true),
        make_tuple("1234", 1234, true),
        make_tuple("01234", 1234, true), // no octal
        make_tuple("2147483647", 2'147'483'647, true),
        make_tuple("-2147483648", -2'147'483'647 - 1, true),
        make_tuple("-1234", -1234, true),

        // invalid values
        make_tuple("", 0, false),
        make_tuple(" 1", 0, false),      // no padding inside
        make_tuple("1 ", 0, false),
        make_tuple("1a", 0, false),
        make_tuple("aap", 0, false),
        make_tuple("0x1", 0, false),     // no hex
        make_tuple(string("1\0" "1", 3), 0, false), // no embedded NULs

        // Overflow and underflow
        make_tuple("-2147483649", 0, false),
        make_tuple("2147483648", 0, false),
        make_tuple("-32482348723847471234", 0, false),
        make_tuple("32482348723847471234", 0, false)
        ));

class PTest_ParseInt64 : public TestWithParam<tuple<
    string,  // input value
    int64_t, // expected result value (special value -9999: pass nullptr)
    bool>>   // expected function return value
{
public:
    inline static size_t m_nTestItem = 0;
};

TEST_P(PTest_ParseInt64, test)
{
    ++m_nTestItem;
    int64_t n = 0;
    const auto& param = GetParam();
    const int64_t nResult = get<1>(param);
    const bool bRet = ParseInt64(get<0>(param), (nResult == TEST_INT_NULLVALUE) ? nullptr : &n);
    EXPECT_EQ(bRet, get<2>(param)) << strprintf("ParseInt64 Item #%zu (%s)", m_nTestItem, get<0>(param));
    if (bRet && (nResult != TEST_INT_NULLVALUE))
        EXPECT_EQ(n, nResult) << strprintf("ParseInt64 Item #%zu ('%s')", m_nTestItem, get<0>(param));
}

INSTANTIATE_TEST_SUITE_P(ParseInt64, PTest_ParseInt64, Values(
    // valid values
    make_tuple("1234", TEST_INT_NULLVALUE, true),
    make_tuple("0", 0, true),
    make_tuple("1234", 1234, true),
    make_tuple("01234", 1234, true), // no octal
    make_tuple("2147483647", 2'147'483'647, true),
    make_tuple("-2147483648", -2'147'483'647 - 1, true),
    make_tuple("9223372036854775807", 9'223'372'036'854'775'807, true),
    make_tuple("-9223372036854775808", -9'223'372'036'854'775'807 - 1, true),
    make_tuple("-1234", -1234, true),
    // invalid values
    make_tuple("", 0, false),
    make_tuple(" 1", 0, false), // no padding inside
    make_tuple("1 ", 0, false),
    make_tuple("aap", 0, false),
    make_tuple("0x1", 0, false), // no hex
    make_tuple(string("1\0" "1", 3), 0, false), // no embedded NULs
    // Overflow and underflow
    make_tuple("-9223372036854775809", 0, false),
    make_tuple("9223372036854775808", 0, false),
    make_tuple("-32482348723847471234", 0, false),
    make_tuple("32482348723847471234", 0, false)
));

constexpr double TEST_DOUBLE_NULLVALUE = -9999.0;
class PTest_ParseDouble : public TestWithParam<tuple<
    string, // input value
    double, // expected result (special value -9999.0: pass nullptr)
    bool>>  // expected  function return value
{
public:
    inline static size_t m_nTestItem = 0;
};

TEST_P(PTest_ParseDouble, test)
{
    ++m_nTestItem;
    double fValue = 0;
    const auto& param = GetParam();
    const double fResult = get<1>(param);
    const bool bRet = ParseDouble(get<0>(param), (fResult == TEST_DOUBLE_NULLVALUE) ? nullptr : &fValue);
    EXPECT_EQ(bRet, get<2>(param)) << strprintf("ParseDouble Item #%zu (%s)", m_nTestItem, get<0>(param));
    if (bRet && (fResult != TEST_INT_NULLVALUE))
        EXPECT_EQ(fValue, fResult) << strprintf("ParseDouble Item #%zu ('%s' -> %d)", m_nTestItem, get<0>(param), fResult);
}

INSTANTIATE_TEST_SUITE_P(ParseDouble, PTest_ParseDouble, Values(
    // valid values
    make_tuple("1234", TEST_DOUBLE_NULLVALUE, true),
    make_tuple("0", 0.0, true),
    make_tuple("1234", 1234.0, true),
    make_tuple("01234", 1234.0, true), // no octal
    make_tuple("2147483647", 2'147'483'647.0, true),
    make_tuple("-2147483648", -2'147'483'648.0, true),
    make_tuple("-1234", -1234.0, true),
    make_tuple("1e6", 1e6, true),
    make_tuple("-1e6", -1e6, true),
    // invalid values
    make_tuple("", 0.0, false),
    make_tuple(" 1", 0.0, false), // no padding inside
    make_tuple("1 ", 0.0, false),
    make_tuple("1a", 0.0, false),
    make_tuple("aap", 0.0, false),
    make_tuple("0x1", 0.0, false), // no hex
    make_tuple(string("1\0" "1", 3), 0.0, false), // no embedded NULs
    // Overflow and underflow
    make_tuple("-1e10000", 0.0, false),
    make_tuple("1e10000", 0.0, false)
));

static const unsigned char TEST_PARSEHEX_ARRAY[] =
{
    0x04, 0x67, 0x8a, 0xfd, 0xb0, 0xfe, 0x55, 0x48, 0x27, 0x19, 0x67, 0xf1, 0xa6, 0x71, 0x30, 0xb7,
    0x10, 0x5c, 0xd6, 0xa8, 0x28, 0xe0, 0x39, 0x09, 0xa6, 0x79, 0x62, 0xe0, 0xea, 0x1f, 0x61, 0xde,
    0xb6, 0x49, 0xf6, 0xbc, 0x3f, 0x4c, 0xef, 0x38, 0xc4, 0xf3, 0x55, 0x04, 0xe5, 0x1e, 0xc1, 0x12,
    0xde, 0x5c, 0x38, 0x4d, 0xf7, 0xba, 0x0b, 0x8d, 0x57, 0x8a, 0x4c, 0x70, 0x2b, 0x6b, 0xf1, 0x1d,
    0x5f
};

constexpr auto TEST_PARSEHEX_STR = "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f";

TEST(str_encodings, ParseHex)
{
    v_uint8 vExpected(TEST_PARSEHEX_ARRAY, TEST_PARSEHEX_ARRAY + sizeof(TEST_PARSEHEX_ARRAY));
    // Basic test vector
    v_uint8 vResult = ParseHex(TEST_PARSEHEX_STR);
    EXPECT_EQ(vResult, vExpected);

    // Spaces between bytes must be supported
    vResult = ParseHex("12 34 56 78");
    EXPECT_EQ(vResult.size(), 4u);
    vExpected = { 0x12, 0x34, 0x56, 0x78 };
    EXPECT_EQ(vResult, vExpected);

    // Stop parsing at invalid value
    vResult = ParseHex("1234 invalid 1234");
    EXPECT_EQ(vResult.size(), 2u);
    vExpected = { 0x12, 0x34 };
    EXPECT_EQ(vResult, vExpected);
}

TEST(str_encodings, HexStr)
{
    string s = HexStr(TEST_PARSEHEX_ARRAY, TEST_PARSEHEX_ARRAY + sizeof(TEST_PARSEHEX_ARRAY));
    EXPECT_STREQ(s.c_str(), TEST_PARSEHEX_STR);

    s = HexStr(TEST_PARSEHEX_ARRAY, TEST_PARSEHEX_ARRAY + 5, true);
    EXPECT_STREQ(s.c_str(), "04 67 8a fd b0");

    s = HexStr(TEST_PARSEHEX_ARRAY, TEST_PARSEHEX_ARRAY, true);
    EXPECT_TRUE(s.empty());

    v_uint8 vParseHex(TEST_PARSEHEX_ARRAY, TEST_PARSEHEX_ARRAY + 5);
    s = HexStr(vParseHex, true);
    EXPECT_STREQ(s.c_str(), "04 67 8a fd b0");
}
