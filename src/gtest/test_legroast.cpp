/*****************************************************************//**
 * \file   test_legroast.cpp
 * \brief  Google tests for LegRoast library
 * 
 * Post-Quantum signatures based on the Legendre PRF by WardBeullens
 * https://github.com/WardBeullens/LegRoast
 * 
 * \date   June 2021
 *********************************************************************/
#include <gtest/gtest.h>
#include <string>

#include "legroast.h"
using namespace testing;
using namespace std;
using namespace legroast;

using u_string = basic_string<unsigned char>;

inline u_string make_ustring(const char* szStr) noexcept
{
	if (!szStr)
		return u_string();
	return u_string(szStr, szStr + strlen(szStr));
}

class PTest_LegRoast : public TestWithParam<u_string>
{};

TEST_P(PTest_LegRoast, sign_Legendre_Middle)
{
    string error;
    CLegRoast<algorithm::Legendre_Middle> lr;
	const auto &sMsg = GetParam();
	// lr.keygen();
    array<unsigned char, SEED_BYTES> seed = { 42, 27, 251, 236, 198, 244, 224, 12, 145, 63, 239, 83, 159, 251, 242, 158 };
    lr.set_private_key(error, seed.data(), seed.size());

 	// sign the message
	EXPECT_TRUE(lr.sign(error, sMsg.c_str(), sMsg.length())) << error;
	// verify signature
    const bool bRet = lr.verify(error, sMsg.c_str(), sMsg.length());
    EXPECT_TRUE(bRet) << "LegRoast signature is invalid. " << error;
}

TEST_P(PTest_LegRoast, sign_Legendre_Fast)
{
    string error;
    CLegRoast<algorithm::Legendre_Fast> lr;
    const auto& sMsg = GetParam();
    lr.keygen();
    // sign the message
    EXPECT_TRUE(lr.sign(error, sMsg.c_str(), sMsg.length())) << error;
    // verify signature
    const bool bRet = lr.verify(error, sMsg.c_str(), sMsg.length());
    EXPECT_TRUE(bRet) << "LegRoast signature is invalid. " << error;
}

INSTANTIATE_TEST_SUITE_P(LegRoast, PTest_LegRoast,
	Values(
		make_ustring("42"),
		make_ustring("test message")
	));
