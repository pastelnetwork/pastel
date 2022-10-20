// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <event2/event.h>

#ifdef EVENT_SET_MEM_FUNCTIONS_IMPLEMENTED

#include <map>
#include <stdlib.h>
#include <vector>

#include <gtest/gtest.h>

#include <support/events.h>

using namespace std;
using namespace testing;

static map<void*, short> tags;
static map<void*, uint16_t> orders;
static uint16_t tagSequence = 0;

static void* tag_malloc(size_t sz) {
    void* mem = malloc(sz);
    if (!mem) return mem;
    tags[mem]++;
    orders[mem] = tagSequence++;
    return mem;
}

static void tag_free(void* mem) {
    tags[mem]--;
    orders[mem] = tagSequence++;
    free(mem);
}


TEST(test_raii_event, raii_event_creation)
{
    event_set_mem_functions(tag_malloc, realloc, tag_free);
    
    void* base_ptr = nullptr;
    {
        auto base = obtain_event_base();
        base_ptr = (void*)base.get();
        EXPECT_EQ(tags[base_ptr] , 1);
    }
    EXPECT_EQ(tags[base_ptr] , 0);
    
    void* event_ptr = nullptr;
    {
        auto base = obtain_event_base();
        auto event = obtain_event(base.get(), -1, 0, nullptr, nullptr);

        base_ptr = (void*)base.get();
        event_ptr = (void*)event.get();

        EXPECT_EQ(tags[base_ptr] , 1);
        EXPECT_EQ(tags[event_ptr] , 1);
    }
    EXPECT_EQ(tags[base_ptr] , 0);
    EXPECT_EQ(tags[event_ptr] , 0);
    
    event_set_mem_functions(malloc, realloc, free);
}

TEST(test_raii_event, raii_event_order)
{
    event_set_mem_functions(tag_malloc, realloc, tag_free);
    
    void* base_ptr = nullptr;
    void* event_ptr = nullptr;
    {
        auto base = obtain_event_base();
        auto event = obtain_event(base.get(), -1, 0, nullptr, nullptr);

        base_ptr = (void*)base.get();
        event_ptr = (void*)event.get();

        // base should have allocated before event
        EXPECT_TRUE(orders[base_ptr] < orders[event_ptr]);
    }
    // base should be freed after event
    EXPECT_TRUE(orders[base_ptr] > orders[event_ptr]);

    event_set_mem_functions(malloc, realloc, free);
}

#endif  // EVENT_SET_MEM_FUNCTIONS_IMPLEMENTED
