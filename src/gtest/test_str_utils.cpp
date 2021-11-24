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
	string sToTrim = get<0>(GetParam());
	const string& sExpectedResult = get<1>(GetParam());
	lowercase(sToTrim);
	EXPECT_EQ(sToTrim, sExpectedResult);
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
	string sToTrim = get<0>(GetParam());
	const string& sExpectedResult = get<1>(GetParam());
	uppercase(sToTrim);
	EXPECT_EQ(sToTrim, sExpectedResult);
}

INSTANTIATE_TEST_SUITE_P(str_utils, PTest_StrUtils_uppercase,
	Values(
		make_tuple("aBc", "ABC"),
		make_tuple("tEsT sTrInG", "TEST STRING")
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