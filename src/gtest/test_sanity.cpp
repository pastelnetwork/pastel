// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include <compat/sanity.h>
#include <key.h>

TEST(sanity, basic)
{
    EXPECT_TRUE(glibc_sanity_test()) << "libc sanity test";
    EXPECT_TRUE(glibcxx_sanity_test()) << "stdlib sanity test";
    EXPECT_TRUE(ECC_InitSanityCheck()) << "openssl ECC test";
}
