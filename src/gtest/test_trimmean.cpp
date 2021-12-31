// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <cmath>
#include <limits>
#include <gtest/gtest.h>

#include <trimmean.h>

using namespace testing;
using namespace std;

class PTest_TrimMean : public TestWithParam<tuple<vector<CAmount>, double, double>>
{};

inline double round3(const double value) noexcept
{
	constexpr double m = 1000.0;
	return round(value * m) / m;
}

TEST_P(PTest_TrimMean, data)
{
	const auto& param = GetParam();
	const auto& vData = get<0>(param);
	const double percent = get<1>(param);
	TrimmeanErrorNumber errNo = TrimmeanErrorNumber::ENOERROR;
	double fResult = TRIMMEAN(vData, percent, &errNo);
	// round fResult
	fResult = round3(fResult);
	EXPECT_EQ(errNo, TrimmeanErrorNumber::ENOERROR);
	EXPECT_EQ(fResult, get<2>(param));
}

INSTANTIATE_TEST_SUITE_P(TrimMean, PTest_TrimMean, Values(
	make_tuple(vector<CAmount>({ 4, 5, 6, 7, 2, 3, 4, 5, 1, 2, 3 }), 0.2, 3.778),
	make_tuple(vector<CAmount>({ 3,4,14,20,22,30,36,41,44,52,59,65,66,72,78,81,84,85,86,97}), 0.25, 53.063)
));

TEST(trimMean, invalid_data)
{
	TrimmeanErrorNumber errNo = TrimmeanErrorNumber::ENOERROR;
	vector<CAmount> vData{ 1, 2, 3 };

	// no errNo parameter
	EXPECT_NO_THROW(TRIMMEAN(vData, -0.25));

	// negative percent
	double fResult = TRIMMEAN(vData, -0.25, &errNo);
	EXPECT_EQ(errNo, TrimmeanErrorNumber::EBADPCNT);
    EXPECT_TRUE(isnan(fResult));

	// 100% percent
	fResult = TRIMMEAN(vData, 1.0, &errNo);
	EXPECT_EQ(errNo, TrimmeanErrorNumber::EBADPCNT);
    EXPECT_TRUE(isnan(fResult));

	// 120% percent
	fResult = TRIMMEAN(vData, 1.2, &errNo);
	EXPECT_EQ(errNo, TrimmeanErrorNumber::EBADPCNT);
    EXPECT_TRUE(isnan(fResult));

	// empty data
	vData.clear();
	fResult = TRIMMEAN(vData, 0.2, &errNo);
	EXPECT_EQ(errNo, TrimmeanErrorNumber::EBADINPUT);
    EXPECT_TRUE(isnan(fResult));
}

