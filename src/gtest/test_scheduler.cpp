// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <random>
#include <gtest/gtest.h>

#include "random.h"
#include "scheduler.h"

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
    this_thread::sleep_for(chrono::microseconds(n));
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

    chrono::system_clock::time_point start = chrono::system_clock::now();
    chrono::system_clock::time_point now = start;
    chrono::system_clock::time_point first, last;
    size_t nTasks = microTasks.getQueueInfo(first, last);
    EXPECT_EQ(nTasks , 0);

    for (int i = 0; i < 100; i++) {
        chrono::system_clock::time_point t = now + chrono::microseconds(randomMsec(rng));
        chrono::system_clock::time_point tReschedule = now + chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = bind(&microTask, ref(microTasks),
                                             ref(counterMutex[whichCounter]), ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        microTasks.schedule(f, t);
    }
    nTasks = microTasks.getQueueInfo(first, last);
    EXPECT_EQ(nTasks , 100);
    EXPECT_TRUE(first < last);
    EXPECT_TRUE(last > now);

    // As soon as these are created they will start running and servicing the queue
    vector<thread> microThreads;
    for (int i = 0; i < 5; i++)
        microThreads.emplace_back(bind(&CScheduler::serviceQueue, &microTasks));

    MicroSleep(600);
    now = chrono::system_clock::now();

    // More threads and more tasks:
    for (int i = 0; i < 5; i++)
        microThreads.emplace_back(bind(&CScheduler::serviceQueue, &microTasks));
    for (int i = 0; i < 100; i++) {
        chrono::system_clock::time_point t = now + chrono::microseconds(randomMsec(rng));
        chrono::system_clock::time_point tReschedule = now + chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = bind(&microTask, ref(microTasks),
                                             ref(counterMutex[whichCounter]), ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        microTasks.schedule(f, t);
    }

    // Drain the task queue then exit threads
    microTasks.stop(true);
    for (auto& thread: microThreads) {
        if (thread.joinable()) thread.join();
    }

    int counterSum = 0;
    for (int i = 0; i < 10; i++) {
        EXPECT_NE(counter[i] , 0);
        counterSum += counter[i];
    }
    EXPECT_EQ(counterSum, 200);
}

TEST(test_scheduler, wait_until_past)
{
    condition_variable condvar;
    mutex mtx;
    unique_lock<mutex> lock(mtx);

    const auto no_wait= [&](const chrono::seconds& d) {
        return condvar.wait_until(lock, chrono::system_clock::now() - d);
    };

    EXPECT_EQ(cv_status::timeout , no_wait(chrono::seconds{1}));
    EXPECT_EQ(cv_status::timeout , no_wait(chrono::minutes{1}));
    EXPECT_EQ(cv_status::timeout , no_wait(chrono::hours{1}));
    EXPECT_EQ(cv_status::timeout , no_wait(chrono::hours{10}));
    EXPECT_EQ(cv_status::timeout , no_wait(chrono::hours{100}));
    EXPECT_EQ(cv_status::timeout , no_wait(chrono::hours{1000}));
}