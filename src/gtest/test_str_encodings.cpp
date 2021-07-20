#include "utilstrencodings.h"
#include "gtest/gtest.h"

using namespace testing;
using namespace std;

class PTest_ASCII85_Encode_Decode : public TestWithParam<tuple<string, string>>
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
