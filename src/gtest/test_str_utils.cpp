#include "str_utils.h"
#include "gtest/gtest.h"

using namespace testing;
using namespace std;

class PTest_StrUtils_isspaceex : public TestWithParam<tuple<char, bool>>
{};

TEST_P(PTest_StrUtils_isspaceex, test)
{
	const char ch = get<0>(GetParam());
	const bool bExpectedResult = get<1>(GetParam());
	EXPECT_EQ(isspaceex(ch), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_isspaceex,
	Values(
		make_tuple(' ', true),
		make_tuple('\n', true),
		make_tuple('\t', true),
		make_tuple('\r', true),
		make_tuple('a', false),
		make_tuple('1', false)
	));

class PTest_StrUtils_islowerex : public TestWithParam<tuple<char, bool>>
{};

TEST_P(PTest_StrUtils_islowerex, test)
{
	const char ch = get<0>(GetParam());
	const bool bExpectedResult = get<1>(GetParam());
	EXPECT_EQ(islowerex(ch), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_islowerex,
	Values(
		make_tuple('a', true),
		make_tuple('b', true),
		make_tuple('z', true),
		make_tuple('A', false),
		make_tuple('1', false),
		make_tuple('Z', false)
	));

class PTest_StrUtils_isupperex : public TestWithParam<tuple<char, bool>>
{};

TEST_P(PTest_StrUtils_isupperex, test)
{
	const char ch = get<0>(GetParam());
	const bool bExpectedResult = get<1>(GetParam());
	EXPECT_EQ(isupperex(ch), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_isupperex,
	Values(
		make_tuple('a', false),
		make_tuple('b', false),
		make_tuple('z', false),
		make_tuple('A', true),
		make_tuple('1', false),
		make_tuple('Z', true)
	));

class PTest_StrUtils_isalphaex : public TestWithParam<tuple<char, bool>>
{};

TEST_P(PTest_StrUtils_isalphaex, test)
{
	const char ch = get<0>(GetParam());
	const bool bExpectedResult = get<1>(GetParam());
	EXPECT_EQ(isalphaex(ch), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_isalphaex,
	Values(
		make_tuple('a', true),
		make_tuple('B', true),
		make_tuple('1', false),
		make_tuple('-', false)
	));

class PTest_StrUtils_isdigitex : public TestWithParam<tuple<char, bool>>
{};

TEST_P(PTest_StrUtils_isdigitex, test)
{
	const char ch = get<0>(GetParam());
	const bool bExpectedResult = get<1>(GetParam());
	EXPECT_EQ(isdigitex(ch), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_isdigitex,
	Values(
		make_tuple('0', true),
		make_tuple('5', true),
		make_tuple('9', true),
		make_tuple('a', false),
		make_tuple('A', false)
	));

class PTest_StrUtils_isalnumex : public TestWithParam<tuple<char, bool>>
{};

TEST_P(PTest_StrUtils_isalnumex, test)
{
	const char ch = get<0>(GetParam());
	const bool bExpectedResult = get<1>(GetParam());
	EXPECT_EQ(isalnumex(ch), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_isalnumex,
	Values(
		make_tuple('0', true),
		make_tuple('7', true),
		make_tuple('a', true),
		make_tuple('B', true),
		make_tuple('-', false),
		make_tuple(' ', false)
	));

class PTest_StrUtils_ltrim : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_StrUtils_ltrim, test)
{
	string sToTrim = get<0>(GetParam());
	const string& sExpectedResult = get<1>(GetParam());
	ltrim(sToTrim);
	EXPECT_EQ(sToTrim, sExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_ltrim,
	Values(
		make_tuple("", ""),
		make_tuple(" a", "a"),
		make_tuple(" \t\t\n\rb \t", "b \t"),
		make_tuple("c \t \r\n", "c \t \r\n")
	));

class PTest_StrUtils_rtrim : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_StrUtils_rtrim, test)
{
	string sToTrim = get<0>(GetParam());
	const string& sExpectedResult = get<1>(GetParam());
	rtrim(sToTrim);
	EXPECT_EQ(sToTrim, sExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_rtrim,
	Values(
		make_tuple("", ""),
		make_tuple("a ", "a"),
		make_tuple(" \t\tb \t \r\n", " \t\tb"),
		make_tuple(" \t\n\rc", " \t\n\rc")
	));

class PTest_StrUtils_trim : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_StrUtils_trim, test)
{
	string sToTrim = get<0>(GetParam());
	const string& sExpectedResult = get<1>(GetParam());
	trim(sToTrim);
	EXPECT_EQ(sToTrim, sExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_trim,
	Values(
		make_tuple("", ""),
		make_tuple("a ", "a"),
		make_tuple(" \t\t\rb \t \r\n", "b"),
		make_tuple(" \t\n\rc", "c")
	));

class PTest_StrUtils_lowercase : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_StrUtils_lowercase, test)
{
    string sToLowercase = get<0>(GetParam());
    const string& sToLowercaseConst = sToLowercase;
    const string& sExpectedResult = get<1>(GetParam());
    // test first const version
    string sResult = lowercase(sToLowercaseConst);
    EXPECT_EQ(sResult, sExpectedResult);
    EXPECT_EQ(sToLowercaseConst, sToLowercase);
	// test in-place version
    sResult = lowercase(sToLowercase);
	EXPECT_EQ(sToLowercase, sExpectedResult);
    EXPECT_EQ(sResult, sToLowercase);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_lowercase,
	Values(
		make_tuple("aBc", "abc"),
		make_tuple("tEsT sTrInG", "test string")
	));

class PTest_StrUtils_uppercase : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_StrUtils_uppercase, test)
{
    string sToUppercase = get<0>(GetParam());
    const string& sToUppercaseConst = sToUppercase;
    const string& sExpectedResult = get<1>(GetParam());
    // test first const version
    string sResult = uppercase(sToUppercaseConst);
    EXPECT_EQ(sResult, sExpectedResult);
    EXPECT_EQ(sToUppercaseConst, sToUppercase);
    // test in-place version
    sResult = uppercase(sToUppercase);
	EXPECT_EQ(sToUppercase, sExpectedResult);
    EXPECT_EQ(sResult, sToUppercase);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_uppercase,
	Values(
		make_tuple("aBc", "ABC"),
		make_tuple("tEsT sTrInG", "TEST STRING")
	));

class PTest_StrUtils_lowerstring_first_capital : public TestWithParam<tuple<string, string>>
{};

TEST_P(PTest_StrUtils_lowerstring_first_capital, test)
{
    string sToConvert = get<0>(GetParam());
    const string& sToConvertConst = sToConvert;
    const string& sExpectedResult = get<1>(GetParam());
    // test first const version
    string sResult = lowerstring_first_capital(sToConvertConst);
    EXPECT_EQ(sResult, sExpectedResult);
    EXPECT_EQ(sToConvertConst, sToConvert);
    // test in-place version
    sResult = lowerstring_first_capital(sToConvert);
    EXPECT_EQ(sToConvert, sExpectedResult);
    EXPECT_EQ(sResult, sToConvert);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_lowerstring_first_capital, 
	Values(
		make_tuple("tEsT STRING", "Test string"), 
		make_tuple("s", "S"),
		make_tuple("T", "T"),
		make_tuple("Nochange", "Nochange")
	));

class PTest_StrUtils_replaceAll : public TestWithParam<tuple<string, string, string, string>>
{};

TEST_P(PTest_StrUtils_replaceAll, test)
{
	string s = get<0>(GetParam());
	const string& sFrom = get<1>(GetParam());
	const string& sTo = get<2>(GetParam());
	const string& sExpectedResult = get<3>(GetParam());
	replaceAll(s, sFrom, sTo);
	EXPECT_EQ(s, sExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_replaceAll,
	Values(
		make_tuple(" abT abE abS abT", " ab", "", "TEST"),
		make_tuple("remove all", "remove all", "", ""),
		make_tuple("1_2_", "_2_", "_3_", "1_3_")
	));

TEST(str_utils, SAFE_SZ)
{
	EXPECT_STREQ(SAFE_SZ(nullptr), "");
	EXPECT_STREQ(SAFE_SZ("abc"), "abc");
}

class PTest_StrUtils_str_icmp : public TestWithParam<tuple<string, string, bool>>
{};

TEST_P(PTest_StrUtils_str_icmp, test)
{
	const string &s1 = get<0>(GetParam());
	const string& s2 = get<1>(GetParam());
	const bool bExpectedResult = get<2>(GetParam());
	EXPECT_EQ(str_icmp(s1, s2), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_str_icmp,
	Values(
		make_tuple("abc", "abc ", false),
		make_tuple("MiXeD CaSe", "mIxEd cAsE", true),
		make_tuple("lowercased", "lowercased", true),
		make_tuple("UPPERCASED", "UPPERCASED", true),
		make_tuple("st1", "st2", false)
	));

class PTest_StrUtils_str_ifind : public TestWithParam<tuple<string, string, bool>>
{};

TEST_P(PTest_StrUtils_str_ifind, test)
{
	const string& s = get<0>(GetParam());
	const string& sSearchFor = get<1>(GetParam());
	const bool bExpectedResult = get<2>(GetParam());
	EXPECT_EQ(str_ifind(s, sSearchFor), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_str_ifind,
	Values(
		make_tuple("find sTr", "STR", true),
		make_tuple("test", "abc", false),
		make_tuple("str in thE middle", "The", true),
		make_tuple("Start with str", "start", true)
	));

class PTest_StrUtils_str_ends_with : public TestWithParam<tuple<string, string, bool>>
{};

TEST_P(PTest_StrUtils_str_ends_with, test)
{
    const string& s = get<0>(GetParam());
    const string& suffix = get<1>(GetParam());
    const bool bExpectedResult = get<2>(GetParam());
    EXPECT_EQ(str_ends_with(s, suffix.c_str()), bExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_str_ends_with,
	Values(
		make_tuple("Test Ends with", "with", true),
		make_tuple("test sfx", "Sfx", false), // not case insensitive
		make_tuple("Str", "S", false),
		make_tuple("Str", "tr", true)
	));

// test some special cases for str_ends_with
TEST(str_utils, str_ends_with)
{
    string s;
    EXPECT_FALSE(str_ends_with(s, "a"));
    s = "test";
    EXPECT_FALSE(str_ends_with(s, nullptr));
    EXPECT_FALSE(str_ends_with(s, ""));
}

TEST(str_utils, str_append_field)
{
    string s;
    str_append_field(s, nullptr, nullptr);
    EXPECT_TRUE(s.empty());
    str_append_field(s, "a", ",");
    EXPECT_EQ(s, "a");
    str_append_field(s, "b", nullptr);
    EXPECT_EQ(s, "ab");
    str_append_field(s, "c", ",");
    EXPECT_EQ(s, "ab,c");
    str_append_field(s, "d", ", ");
    EXPECT_EQ(s, "ab,c, d");
}
