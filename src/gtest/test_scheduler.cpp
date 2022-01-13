// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <random>
// #include <mutex>
#include <gtest/gtest.h>

#include "random.h"
#include "scheduler.h"

#include "test/test_bitcoin.h"

#include <boost/bind/bind.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <boost/thread.hpp>
#include <boost/test/unit_test.hpp>

// BOOST_AUTO_TEST_SUITE(scheduler_tests)

using namespace std;
using namespace testing;

static void microTask(CScheduler& s, mutex& mtx, int& counter, int delta, chrono::system_clock::time_point rescheduleTime)
{
    {
        unique_lock<mutex> lock(mtx);
        counter += delta;
    }
    chrono::system_clock::time_point noTime = chrono::system_clock::time_point::min();
    if (rescheduleTime != noTime) {
        CScheduler::Function f = bind(&microTask, ref(s), ref(mtx), ref(counter), -delta + 1, noTime);
        s.schedule(f, rescheduleTime);
    }
}

static void MicroSleep(uint64_t n)
{
    boost::this_thread::sleep_for(boost::chrono::microseconds(n));
}

TEST(test_scheduler, manythreads)
{
    seed_insecure_rand(false);

    // Stress test: hundreds of microsecond-scheduled tasks,
    // serviced by 10 threads.
    //
    // So... ten shared counters, which if all the tasks execute
    // properly will sum to the number of tasks done.
    // Each task adds or subtracts from one of the counters a
    // random amount, and then schedules another task 0-1000
    // microseconds in the future to subtract or add from
    // the counter -random_amount+1, so in the end the shared
    // counters should sum to the number of initial tasks performed.
    CScheduler microTasks;

    mutex counterMutex[10];
    int counter[10] = { 0 };

    mt19937 rng(insecure_rand());
    uniform_int_distribution<> zeroToNine(0, 9);
    uniform_int_distribution<> randomMsec(-11, 1000);
    uniform_int_distribution<> randomDelta(-1000, 1000);

    chrono::time_point<chrono::system_clock> start = chrono::system_clock::now();
    chrono::time_point<chrono::system_clock> now = start;
    chrono::time_point<chrono::system_clock> first, last;
    size_t nTasks = microTasks.getQueueInfo(first, last);
    BOOST_CHECK(nTasks == 0);

    for (int i = 0; i < 100; i++) {
        chrono::time_point<chrono::microseconds> t = now + chrono::microseconds(randomMsec(rng));
        chrono::time_point<chrono::microseconds> tReschedule = now + chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = bind(&microTask, ref(microTasks),
                                             ref(counterMutex[whichCounter]), ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        microTasks.schedule(f, t);
    }
    nTasks = microTasks.getQueueInfo(first, last);
    BOOST_CHECK(nTasks == 100);
    BOOST_CHECK(first < last);
    BOOST_CHECK(last > now);

    // As soon as these are created they will start running and servicing the queue
    thread_group microThreads;
    for (int i = 0; i < 5; i++)
        microThreads.create_thread(bind(&CScheduler::serviceQueue, &microTasks));

    MicroSleep(600);
    now = chrono::system_clock::now();

    // More threads and more tasks:
    for (int i = 0; i < 5; i++)
        microThreads.create_thread(bind(&CScheduler::serviceQueue, &microTasks));
    for (int i = 0; i < 100; i++) {
        chrono::time_point<chrono::microseconds> t = now + chrono::microseconds(randomMsec(rng));
        chrono::time_point<chrono::microseconds> tReschedule = now + chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = bind(&microTask, ref(microTasks),
                                             ref(counterMutex[whichCounter]), ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        microTasks.schedule(f, t);
    }

    // Drain the task queue then exit threads
    microTasks.stop(true);
    microThreads.join_all(); // ... wait until all the threads are done

    int counterSum = 0;
    for (int i = 0; i < 10; i++) {
        BOOST_CHECK(counter[i] != 0);
        counterSum += counter[i];
    }
    BOOST_CHECK_EQUAL(counterSum, 200);
}

