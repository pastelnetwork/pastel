#include <gtest/gtest.h>

#include "primitives/block.h"
#include "version.h"


TEST(block_tests, header_size_is_expected) {
    // Dummy header with an empty Equihash solution.
    CBlockHeader header;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    // CBlockHeader without nSolution is 140 bytes
    // nSolution size serialized using WriteCompactSize
    //   for nSize < 253 -> writes as uint8_t
    ss << header;

    ASSERT_EQ(ss.size(), CBlockHeader::EMPTY_HEADER_SIZE + 1);
}
