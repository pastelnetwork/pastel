#include "pastelid/pastel_key.h"
#include "gtest/gtest.h"
#include <tuple>
#include <string>

using namespace std;
using namespace testing;

class PTest_PastelID_Alg : public TestWithParam<tuple<string, CPastelID::SIGN_ALGORITHM>>
{};

TEST_P(PTest_PastelID_Alg, GetAlgorithmByName)
{
    auto &Param = GetParam();
    const auto alg = CPastelID::GetAlgorithmByName(get<0>(Param));
    EXPECT_EQ(alg, get<1>(Param));
}

INSTANTIATE_TEST_SUITE_P(PastelID_Alg, PTest_PastelID_Alg,
    Values(
        make_tuple("", CPastelID::SIGN_ALGORITHM::ed448),
        make_tuple(SIGN_ALG_ED448, CPastelID::SIGN_ALGORITHM::ed448),
        make_tuple(SIGN_ALG_LEGROAST, CPastelID::SIGN_ALGORITHM::legroast),
        make_tuple("myalg", CPastelID::SIGN_ALGORITHM::not_defined)
    ));
