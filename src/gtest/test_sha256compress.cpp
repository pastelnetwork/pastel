// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "crypto/sha256.h"
#include "uint256.h"

#include <stdexcept>

#include <gtest/gtest.h>

using namespace std;
using namespace testing;

TEST(test_sha256compress, compression)
{
	{
		unsigned char preimage[64] = {};
		CSHA256 hasher;
		hasher.Write(&preimage[0], 64);

		uint256 digest;

		hasher.FinalizeNoPadding(digest.begin());

		EXPECT_EQ(digest , uint256S("d8a93718eaf9feba4362d2c091d4e58ccabe9f779957336269b4b917be9856da")) << digest.GetHex();
	}

	{
		unsigned char preimage[63] = {};
		CSHA256 hasher;
		hasher.Write(&preimage[0], 63);
		uint256 digest;
		EXPECT_THROW(hasher.FinalizeNoPadding(digest.begin()), std::length_error);
	}

	{
		unsigned char preimage[65] = {};
		CSHA256 hasher;
		hasher.Write(&preimage[0], 65);
		uint256 digest;
		EXPECT_THROW(hasher.FinalizeNoPadding(digest.begin()), std::length_error);
	}

	{
		unsigned char n = 0x00;
		CSHA256 hasher;
		for (size_t i = 0; i < 64; i++) {
			hasher.Write(&n, 1);
		}
		uint256 digest;

		hasher.FinalizeNoPadding(digest.begin());

		EXPECT_EQ(digest , uint256S("d8a93718eaf9feba4362d2c091d4e58ccabe9f779957336269b4b917be9856da")) << digest.GetHex();
	}

    {
        unsigned char preimage[64] = { 'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd',
                                       'a', 'b', 'c', 'd'
                                    };
		CSHA256 hasher;
        hasher.Write(&preimage[0], 64);

        uint256 digest;

        hasher.FinalizeNoPadding(digest.begin());

        EXPECT_EQ(digest , uint256S("da70ec41879e36b000281733d4deb27ddf41e8e343a38f2fabbd2d8611987d86")) << digest.GetHex();
    }
}

