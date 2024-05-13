// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2021-2024 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mutex>
#include <gtest/gtest.h>

#include <utils/reverselock.h>

using namespace std;
using namespace testing;

TEST(test_reverselock, reverselock_basics)
{
    mutex mtx;
    unique_lock lock(mtx);

    EXPECT_TRUE(lock.owns_lock());
    {
        reverse_lock<unique_lock<mutex> > rlock(lock);
        EXPECT_TRUE(!lock.owns_lock());
    }
    EXPECT_TRUE(lock.owns_lock());
}

TEST(test_reverselock, reverselock_errors)
{
    mutex mtx;
    unique_lock lock(mtx);

    // Make sure trying to reverse lock an unlocked lock fails
    lock.unlock();

    EXPECT_TRUE(!lock.owns_lock());

    bool failed = false;
    try {
        reverse_lock<unique_lock<mutex> > rlock(lock);
    } catch(...) {
        failed = true;
    }

    EXPECT_TRUE(failed);
    EXPECT_TRUE(!lock.owns_lock());

    // Locking the original lock after it has been taken by a reverse lock
    // makes no sense. Ensure that the original lock no longer owns the lock
    // after giving it to a reverse one.

    lock.lock();
    EXPECT_TRUE(lock.owns_lock());
    {
        reverse_lock<unique_lock<mutex> > rlock(lock);
        EXPECT_TRUE(!lock.owns_lock());
    }

    EXPECT_TRUE(failed);
    EXPECT_TRUE(lock.owns_lock());
}
