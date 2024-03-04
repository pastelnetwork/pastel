#pragma once
// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <functional>
#include <chrono>
#include <map>
#include <condition_variable>

#include <utils/svc_thread.h>

//
// Simple class for background tasks that should be run
// periodically or once "after a while"
//
// Usage:
//
// unique_ptr<CScheduler> sch = make_unique<CScheduler>();
// sch->scheduleFromNow(doSomething, 11); // Assuming a: void doSomething() { }
// sch->scheduleFromNow(std::bind(Class::func, this, argument), 3);
//   - add worker threads to handle scheduler tasks
// sch->add_workers(5);
//   - start scheduler handler
// sch->serviceQueue();
//
// ... then at program shutdown
//   - signal scheduler worker threads to stop
// sch->stop_all();
//   - wait for scheduler worker threads to stop
// sch->join_all();

class CScheduler
{
public:
    CScheduler(const char *szThreadName);
    ~CScheduler();

    typedef std::function<void(void)> Function;

    // Call func at/after time t
    void schedule(Function f, const std::chrono::system_clock::time_point t);

    // Convenience method: call f once deltaSeconds from now
    void scheduleFromNow(Function f, const int64_t deltaSeconds);

    // Another convenience method: call f approximately
    // every deltaSeconds forever, starting deltaSeconds from now.
    // To be more precise: every time f is finished, it
    // is rescheduled to run deltaSeconds later. If you
    // need more accurate scheduling, don't use this method.
    void scheduleEvery(Function f, const int64_t nDeltaSeconds);

    // To keep things as simple as possible, there is no unschedule.

    // Services the queue 'forever'. Should be run in a thread
    void serviceQueue();

    // Tell any threads running serviceQueue to stop as soon as they're
    // done servicing whatever task they're currently servicing (drain=false)
    // or when there is no work left to be done (drain=true)
    void stop(bool bDrain = false);
    void join_all()
    {
        m_threadGroup.join_all();
    }
    // reset scheduler if task queue is empty
    void reset();

    // thread-safe check if queue is empty
    bool empty() const noexcept;

    // Returns number of tasks waiting to be serviced,
    // and first and last task times
    size_t getQueueInfo(std::chrono::system_clock::time_point &first,
                        std::chrono::system_clock::time_point& last) const noexcept;

    void add_workers(const size_t nThreadCount = 1);

protected:
    std::string m_sThreadName;
    std::multimap<std::chrono::system_clock::time_point, Function> m_taskQueue;
    std::condition_variable m_newTaskScheduled;
    mutable std::mutex m_newTaskMutex;
    int m_nThreadsServicingQueue;
    std::atomic_bool m_bStopWhenEmpty;  
    std::atomic_bool m_bStopRequested;
    std::atomic_uint32_t m_nWorkerID;
    CServiceThreadGroup m_threadGroup;

    bool shouldStop() const noexcept { return m_bStopRequested || (m_bStopWhenEmpty && m_taskQueue.empty()); }
};
