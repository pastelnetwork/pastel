#include "numeric_range.h"
#include "gtest/gtest.h"

using namespace testing;
using namespace std;

template <typename _IntType>
class TTest_numeric_range : public Test
{};

typedef Types<int, unsigned int, short, unsigned short, int8_t, uint8_t, int32_t, uint32_t, int64_t, uint64_t, size_t> TestIntTypes;

TYPED_TEST_SUITE(TTest_numeric_range, TestIntTypes);

TYPED_TEST(TTest_numeric_range, test)
{
	if (is_signed_v<TypeParam>)
	{
		TypeParam nSignedSum = 0;
		numeric_range<TypeParam> rng1(-5, 3);
		EXPECT_EQ(rng1.min(), -5);
		EXPECT_EQ(rng1.max(), 3);
		for (const auto n : rng1)
			nSignedSum += n;
		EXPECT_EQ(nSignedSum, -9);
		EXPECT_TRUE(rng1.contains(-3));
		EXPECT_TRUE(rng1.contains(2));
		EXPECT_FALSE(rng1.contains(4));
	}
	numeric_range<TypeParam> rng2(2, 7);
	EXPECT_EQ(rng2.min(), 2);
	EXPECT_EQ(rng2.max(), 7);
	size_t nSum = 0;
	// range version
	for (const auto n : rng2)
		nSum += n;
	EXPECT_EQ(nSum, 27u);
	EXPECT_TRUE(rng2.contains(3));
	EXPECT_FALSE(rng2.contains(10));
	nSum = 0;
	for (auto it = rng2.cbegin(); it != rng2.cend(); ++it)
		nSum += *it;
	EXPECT_EQ(nSum, 27u);
	nSum = 0;
	for (auto it = rng2.begin(); it != rng2.end(); ++it)
		nSum += *it;
	EXPECT_EQ(nSum, 27u);
	EXPECT_NE(rng2.cbegin(), rng2.cend());
	EXPECT_EQ(rng2.begin(), rng2.cbegin());
}
