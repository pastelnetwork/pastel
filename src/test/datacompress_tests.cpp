// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "datacompressor.h"
#include "util.h"
#include "test/test_bitcoin.h"

#include <stdint.h>
#include <string>
#include <boost/test/unit_test.hpp>
#include <vector>


BOOST_FIXTURE_TEST_SUITE(datacompress_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(datacompress_basic)
{
    std::string input("HelloPastel");
    std::string output;

    // Compress data
    CDataStream data_stream(SER_NETWORK, 1);
    data_stream << input;
    CDataCompressor compressor(data_stream);

    CDataStream compress_stream(SER_NETWORK, 1);
    compress_stream << compressor;

    // Descompress data
    CDataStream decrompress_stream(SER_NETWORK, 1);
    CDataCompressor decompressor(decrompress_stream);
    compress_stream >> decompressor;
    decrompress_stream >> output;

    BOOST_CHECK(input.compare(output) == 0);
}

typedef std::vector<uint8_t> TestData;
bool isEqual(TestData& in, TestData&out) {
    if (in.size() != out.size()) {
        return false;
    }

    for (auto i = 0; i < in.size(); i++) {
        if (in[i] != out[i]) {
            return false;
        }
    }

    return true;
}

BOOST_AUTO_TEST_CASE(datacompress_stress)
{
    TestData in{};

    for (auto i = 1; i < 256; i++) {
        TestData out{};
        in.push_back(i);

        // Compress data
        CDataStream data_stream(SER_NETWORK, 1);
        data_stream << in;
        CDataCompressor compressor(data_stream);

        CDataStream compress_stream(SER_NETWORK, 1);
        compress_stream << compressor;

        // Descompress data
        CDataStream decrompress_stream(SER_NETWORK, 1);
        CDataCompressor decompressor(decrompress_stream);
        compress_stream >> decompressor;
        decrompress_stream >> out;

        BOOST_CHECK(isEqual(in, out));
    }  
}
BOOST_AUTO_TEST_SUITE_END()
