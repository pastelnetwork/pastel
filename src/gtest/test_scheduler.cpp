// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <random>

#include <gtest/gtest.h>

#include <utils/svc_thread.h>
#include <utils/scheduler.h>

using namespace std;
using namespace testing;

static void microTask(CScheduler &scheduler, mutex& mtx, int& counter, const int delta, const chrono::system_clock::time_point rescheduleTime)
{
    {
        unique_lock<mutex> lock(mtx);
        counter += delta;
    }
    const chrono::system_clock::time_point noTime = chrono::system_clock::time_point::min();
    if (rescheduleTime != noTime)
    {
        CScheduler::Function f = bind(&microTask, std::ref(scheduler), std::ref(mtx), std::ref(counter), -delta + 1, noTime);
        scheduler.schedule(f, rescheduleTime);
    }
}

static void MicroSleep(const uint64_t n)
{
    this_thread::sleep_for(chrono::microseconds(n));
}

TEST(scheduler, manythreads)
{
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
    CScheduler scheduler("scheduler");

    mutex counterMutex[10];
    int counter[10] = { 0 };
    random_device rd;
    mt19937 rng(rd());
    uniform_int_distribution<> zeroToNine(0, 9);
    uniform_int_distribution<> randomMsec(-11, 1000);
    uniform_int_distribution<> randomDelta(-1000, 1000);

    chrono::system_clock::time_point start = chrono::system_clock::now();
    chrono::system_clock::time_point now = start;
    chrono::system_clock::time_point first, last;
    size_t nTasks = scheduler.getQueueInfo(first, last);
    EXPECT_EQ(nTasks, 0u);

    constexpr size_t TEST_TASK_COUNT = 10'000;
    for (size_t i = 0; i < TEST_TASK_COUNT; ++i)
    {
        chrono::system_clock::time_point t = now + chrono::microseconds(randomMsec(rng));
        chrono::system_clock::time_point tReschedule = now + chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = bind(&microTask, std::ref(scheduler),
                                      std::ref(counterMutex[whichCounter]), std::ref(counter[whichCounter]),
                                      randomDelta(rng), tReschedule);
        scheduler.schedule(f, t);
    }
    nTasks = scheduler.getQueueInfo(first, last);
    EXPECT_EQ(nTasks, TEST_TASK_COUNT);
    EXPECT_LT(first, last);
    EXPECT_GT(last, now);

    // As soon as these are created they will start running and servicing the queue
    scheduler.add_workers(5);

    MicroSleep(600);
    now = chrono::system_clock::now();

    // More threads and more tasks:
    scheduler.add_workers(5);
    for (size_t i = 0; i < TEST_TASK_COUNT; ++i)
    {
        auto t = now + chrono::microseconds(randomMsec(rng));
        auto tReschedule = now + chrono::microseconds(500 + randomMsec(rng));
        int whichCounter = zeroToNine(rng);
        CScheduler::Function f = bind(&microTask, std::ref(scheduler),
                                             std::ref(counterMutex[whichCounter]), std::ref(counter[whichCounter]),
                                             randomDelta(rng), tReschedule);
        scheduler.schedule(f, t);
    }

    // Drain the task queue then exit threads
    scheduler.stop(true);
    scheduler.join_all(); // ... wait until all the threads are done

    int counterSum = 0;
    for (int i = 0; i < 10; i++)
    {
        EXPECT_NE(counter[i], 0);
        counterSum += counter[i];
    }
    EXPECT_EQ(counterSum, TEST_TASK_COUNT * 2);
}

static void test_scheduler_task(bool &bExecuted)
{
    bExecuted = true;
}

class TestScheduler : 
    public CScheduler, 
    public Test
{
public:
    TestScheduler() : 
        CScheduler("scheduler")
    {}
};

TEST_F(TestScheduler, ctor)
{
    EXPECT_TRUE(empty());
    EXPECT_TRUE(m_taskQueue.empty());
    EXPECT_FALSE(m_bStopWhenEmpty);
    EXPECT_FALSE(m_bStopRequested);
    EXPECT_EQ(m_threadGroup.size(), 0u);
    EXPECT_TRUE(!m_sThreadName.empty());
}

TEST_F(TestScheduler, exec)
{
    bool bExecuted = false;
    // first check that it executed successfully with valid time
    schedule(bind(&test_scheduler_task, ref(bExecuted)), chrono::system_clock::now() + 1s);
    add_workers(1);
    stop(true);
    join_all();
    EXPECT_TRUE(bExecuted);

    reset();
    EXPECT_FALSE(m_bStopWhenEmpty);
    EXPECT_FALSE(m_bStopRequested);
}

