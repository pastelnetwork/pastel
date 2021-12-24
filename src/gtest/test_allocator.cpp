// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <gtest/gtest.h>

#include "support/allocators/secure.h"

using namespace std;
using namespace testing;

TEST(AllocatorTests, arena_tests)
{
    // Fake memory base address for testing
    // without actually using memory.
    void *synth_base = reinterpret_cast<void*>(0x08000000);
    const size_t synth_size = 1024*1024;
    Arena b(synth_base, synth_size, 16);
    void *chunk = b.alloc(1000);
#ifdef ARENA_DEBUG
    b.walk();
#endif
    EXPECT_NE(chunk , nullptr);
    EXPECT_EQ(b.stats().used , 1008); // Aligned to 16
    EXPECT_EQ(b.stats().total , synth_size); // Nothing has disappeared?
    b.free(chunk);
#ifdef ARENA_DEBUG
    b.walk();
#endif
    EXPECT_EQ(b.stats().used , 0);
    EXPECT_EQ(b.stats().free , synth_size);

    void *a0 = b.alloc(128);
    void *a1 = b.alloc(256);
    void *a2 = b.alloc(512);
    EXPECT_EQ(b.stats().used , 896);
    EXPECT_EQ(b.stats().total , synth_size);
#ifdef ARENA_DEBUG
    b.walk();
#endif
    b.free(a0);
#ifdef ARENA_DEBUG
    b.walk();
#endif
    EXPECT_EQ(b.stats().used , 768);
    b.free(a1);
    EXPECT_EQ(b.stats().used , 512);
    void *a3 = b.alloc(128);
#ifdef ARENA_DEBUG
    b.walk();
#endif
    EXPECT_EQ(b.stats().used , 640);
    b.free(a2);
    EXPECT_EQ(b.stats().used , 128);
    b.free(a3);
    EXPECT_EQ(b.stats().used , 0);
    EXPECT_EQ(b.stats().chunks_used, 0);
    EXPECT_EQ(b.stats().total , synth_size);
    EXPECT_EQ(b.stats().free , synth_size);
    EXPECT_EQ(b.stats().chunks_free, 1);

    vector<void*> addr;
    EXPECT_EQ(b.alloc(0) , nullptr); // allocating 0 always returns nullptr
#ifdef ARENA_DEBUG
    b.walk();
#endif
    // Sweeping allocate all memory
    for (int x=0; x<1024; ++x)
        addr.push_back(b.alloc(1024));
    EXPECT_EQ(b.stats().free , 0);
    EXPECT_EQ(b.alloc(1024) , nullptr); // memory is full, this must return nullptr
    EXPECT_EQ(b.alloc(0) , nullptr);
    for (int x=0; x<1024; ++x)
        b.free(addr[x]);
    addr.clear();
    EXPECT_EQ(b.stats().total , synth_size);
    EXPECT_EQ(b.stats().free , synth_size);

    // Now in the other direction...
    for (int x=0; x<1024; ++x)
        addr.push_back(b.alloc(1024));
    for (int x=0; x<1024; ++x)
        b.free(addr[1023-x]);
    addr.clear();

    // Now allocate in smaller unequal chunks, then deallocate haphazardly
    // Not all the chunks will succeed allocating, but freeing nullptr is
    // allowed so that is no problem.
    for (int x=0; x<2048; ++x)
        addr.push_back(b.alloc(x+1));
    for (int x=0; x<2048; ++x)
        b.free(addr[((x*23)%2048)^242]);
    addr.clear();

    // Go entirely wild: free and alloc interleaved,
    // generate targets and sizes using pseudo-randomness.
    for (int x=0; x<2048; ++x)
        addr.push_back(0);
    uint32_t s = 0x12345678;
    for (int x=0; x<5000; ++x) {
        int idx = s & (addr.size()-1);
        if (s & 0x80000000) {
            b.free(addr[idx]);
            addr[idx] = 0;
        } else if(!addr[idx]) {
            addr[idx] = b.alloc((s >> 16) & 2047);
        }
        bool lsb = s & 1;
        s >>= 1;
        if (lsb)
            s ^= 0xf00f00f0; // LFSR period 0xf7ffffe1
    }
    for (void *ptr: addr)
        b.free(ptr);
    addr.clear();

    EXPECT_EQ(b.stats().total , synth_size);
    EXPECT_EQ(b.stats().free , synth_size);

    // Check that Arena::free may be called on nullptr.
    b.free(nullptr);
}

/** Mock LockedPageAllocator for testing */
class TestLockedPageAllocator: public LockedPageAllocator
{
public:
    TestLockedPageAllocator(int count_in, int lockedcount_in): count(count_in), lockedcount(lockedcount_in) {}
    void* AllocateLocked(size_t len, bool *lockingSuccess)
    {
        *lockingSuccess = false;
        if (count > 0) {
            --count;

            if (lockedcount > 0) {
                --lockedcount;
                *lockingSuccess = true;
            }

            return reinterpret_cast<void*>(0x08000000 + (count<<24)); // Fake address, do not actually use this memory
        }
        return 0;
    }
    void FreeLocked(void* addr, size_t len)
    {
    }
    size_t GetLimit()
    {
        return numeric_limits<size_t>::max();
    }
private:
    int count;
    int lockedcount;
};

TEST(AllocatorTests, lockedpool_tests_mock)
{
    // Test over three virtual arenas, of which one will succeed being locked
    unique_ptr<LockedPageAllocator> x(new TestLockedPageAllocator(3, 1));
    LockedPool pool(move(x));
    EXPECT_EQ(pool.stats().total , 0);
    EXPECT_EQ(pool.stats().locked , 0);

    // Ensure unreasonable requests are refused without allocating anything
    void *invalid_toosmall = pool.alloc(0);
    EXPECT_EQ(invalid_toosmall , nullptr);
    EXPECT_EQ(pool.stats().used , 0);
    EXPECT_EQ(pool.stats().free , 0);
    void *invalid_toobig = pool.alloc(LockedPool::ARENA_SIZE+1);
    EXPECT_EQ(invalid_toobig , nullptr);
    EXPECT_EQ(pool.stats().used , 0);
    EXPECT_EQ(pool.stats().free , 0);

    void *a0 = pool.alloc(LockedPool::ARENA_SIZE / 2);
    EXPECT_TRUE(a0);
    EXPECT_EQ(pool.stats().locked , LockedPool::ARENA_SIZE);
    void *a1 = pool.alloc(LockedPool::ARENA_SIZE / 2);
    EXPECT_TRUE(a1);
    void *a2 = pool.alloc(LockedPool::ARENA_SIZE / 2);
    EXPECT_TRUE(a2);
    void *a3 = pool.alloc(LockedPool::ARENA_SIZE / 2);
    EXPECT_TRUE(a3);
    void *a4 = pool.alloc(LockedPool::ARENA_SIZE / 2);
    EXPECT_TRUE(a4);
    void *a5 = pool.alloc(LockedPool::ARENA_SIZE / 2);
    EXPECT_TRUE(a5);
    // We've passed a count of three arenas, so this allocation should fail
    void *a6 = pool.alloc(16);
    EXPECT_TRUE(!a6);

    pool.free(a0);
    pool.free(a2);
    pool.free(a4);
    pool.free(a1);
    pool.free(a3);
    pool.free(a5);
    EXPECT_EQ(pool.stats().total , 3*LockedPool::ARENA_SIZE);
    EXPECT_EQ(pool.stats().locked , LockedPool::ARENA_SIZE);
    EXPECT_EQ(pool.stats().used , 0);
}

// These tests used the live LockedPoolManager object, this is also used
// by other tests so the conditions are somewhat less controllable and thus the
// tests are somewhat more error-prone.
TEST(AllocatorTests, lockedpool_tests_live)
{
    LockedPoolManager &pool = LockedPoolManager::Instance();
    LockedPool::Stats initial = pool.stats();

    void *a0 = pool.alloc(16);
    EXPECT_TRUE(a0);
    // Test reading and writing the allocated memory
    *((uint32_t*)a0) = 0x1234;
    EXPECT_EQ(*((uint32_t*)a0) , 0x1234);

    pool.free(a0);
    // If more than one new arena was allocated for the above tests, something is wrong
    EXPECT_TRUE(pool.stats().total <= (initial.total + LockedPool::ARENA_SIZE));
    // Usage must be back to where it started
    EXPECT_EQ(pool.stats().used , initial.used);

    // Check that LockedPool::free may be called on nullptr.
    pool.free(nullptr);
}

TEST(AllocatorTests, LockedPoolAbortOnDoubleFree) {
    LockedPoolManager &pool = LockedPoolManager::Instance();

    // We should be able to allocate and free memory.
    void *a0 = pool.alloc(16);
    pool.free(a0);

    // Process terminates on double-free.
    EXPECT_DEATH(pool.free(a0), "Arena: invalid or double free");
}

TEST(AllocatorTests, LockedPoolAbortOnFreeInvalidPointer) {
    LockedPoolManager &pool = LockedPoolManager::Instance();
    bool notInPool = false;

    // Process terminates if we try to free memory that wasn't allocated by the pool.
    EXPECT_DEATH(pool.free(&notInPool), "LockedPool: invalid address not pointing to any arena");
}
