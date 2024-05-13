// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <assert.h>
#include <utility>
#include <unordered_map>

#include <utils/scope_guard.hpp>
#include <utils/scheduler.h>
#include <utils/reverselock.h>

using namespace std;

#ifdef __MINGW64__
__thread CServiceThread *funcThreadObj;
#else
thread_local CServiceThread* funcThreadObj = nullptr;
#endif

CScheduler::CScheduler(const char *szThreadName) : 
    m_sThreadName(szThreadName ? szThreadName : "scheduler"),
    m_nThreadsServicingQueue(0),
    m_bStopWhenEmpty(false),
    m_bStopRequested(false),
    m_nWorkerID(0)
{}

CScheduler::~CScheduler()
{
    stop(false);
    join_all();
    assert(m_nThreadsServicingQueue == 0);
}

/**
 * Add worker threads to handle scheduler tasks.
 * 
 * \param nThreadCount - number of worker threads to add
 */
void CScheduler::add_workers(const size_t nThreadCount)
{
    string error;
    for (size_t i = 0; i < nThreadCount; ++i)
    {
        const uint32_t nWorkerID = ++m_nWorkerID;
        m_threadGroup.add_func_thread(error, strprintf("%s-%zu", m_sThreadName, nWorkerID).c_str(),
                                      bind(&CScheduler::serviceQueue, this));
    }
    // trigger scheduler handler
    m_newTaskScheduled.notify_one();
}

/**
 * Main scheduler handler. Need to call add_workers(...) to add some worker threads 
 * to handle tasks in the scheduler queue.
 */
void CScheduler::serviceQueue()
{
    unique_lock lock(m_newTaskMutex);
    {
        ++m_nThreadsServicingQueue;
        auto guard = sg::make_scope_guard([this]() noexcept
            {
                --m_nThreadsServicingQueue;
            });

        // newTaskMutex is locked throughout this loop EXCEPT
        // when the thread is waiting or when the user's function
        // is called.
        while (!shouldStop())
        {
            try
            {
                while (!shouldStop() && m_taskQueue.empty())
                {
                    // Wait until there is something to do.
                    // when task is added - it notifies the thread via cv.notify_one()
                    m_newTaskScheduled.wait(lock);
                }

                // Wait until either there is a new task, or until
                // the time of the first item on the queue
                while (!shouldStop() && !m_taskQueue.empty())
                {
                    // make a copy of task timepoint
                    const auto tp = m_taskQueue.begin()->first;
                    // Keep waiting until timeout
                    if (m_newTaskScheduled.wait_until(lock, tp) == cv_status::timeout)
                        break;
                }

                if (shouldStop())
                    break;
                // If there are multiple threads, the queue can empty while we're waiting (another
                // thread may service the task we were waiting on).
                if (m_taskQueue.empty())
                    continue;

                auto nodeHandle = m_taskQueue.extract(m_taskQueue.begin());
                if (nodeHandle.empty())
                    continue;

                Function f = nodeHandle.mapped();
                {
                    // Unlock before calling f, so it can reschedule itself or another task
                    // without deadlocking:
                    reverse_lock<unique_lock<mutex>> rlock(lock);
                    f();
                }
            }
            catch (...) {
                throw;
            }
        }
    }
    m_newTaskScheduled.notify_one();
}

// thread-safe check if queue is empty
bool CScheduler::empty() const noexcept
{
    unique_lock lock(m_newTaskMutex);
    return m_taskQueue.empty();
}

/**
 * Signal scheduler to stop.
 * 
 * \param bDrain - if true have to empty the task queue before the stop
 */
void CScheduler::stop(bool bDrain)
{
    {
        unique_lock lock(m_newTaskMutex);
        if (bDrain)
            m_bStopWhenEmpty = true;
        else
            m_bStopRequested = true;
        // sets stop flag in CServiceThread only
        m_threadGroup.stop_all();
    }
    m_newTaskScheduled.notify_all();
}

// reset scheduler if task queue is empty
void CScheduler::reset()
{
    unique_lock lock(m_newTaskMutex);
    if (m_taskQueue.empty())
    {
        m_bStopWhenEmpty = false;
        m_bStopRequested = false;
    }
}

void CScheduler::schedule(CScheduler::Function f, const chrono::system_clock::time_point t)
{
    {
        unique_lock lock(m_newTaskMutex);
        m_taskQueue.emplace(t, f);
    }
    m_newTaskScheduled.notify_one();
}

void CScheduler::scheduleFromNow(CScheduler::Function f, const int64_t nDeltaSeconds)
{
    schedule(f, chrono::system_clock::now() + chrono::seconds(nDeltaSeconds));
}

static void RepeatCall(CScheduler* s, CScheduler::Function f, const int64_t nDeltaSeconds)
{
    f();
    s->scheduleFromNow(bind(&RepeatCall, s, f, nDeltaSeconds), nDeltaSeconds);
}

void CScheduler::scheduleEvery(CScheduler::Function f, const int64_t nDeltaSeconds)
{
    scheduleFromNow(bind(&RepeatCall, this, f, nDeltaSeconds), nDeltaSeconds);
}

/**
 * Returns queue information.
 * 
 * \param first - time of the first scheduled item
 * \param last - time of the last scheduled item
 * \return queue size
 */
size_t CScheduler::getQueueInfo(chrono::system_clock::time_point &first,
                                chrono::system_clock::time_point &last) const noexcept
{
    unique_lock lock(m_newTaskMutex);
    const size_t nQueueSize = m_taskQueue.size();
    if (!m_taskQueue.empty())
    {
        first = m_taskQueue.begin()->first;
        last = m_taskQueue.rbegin()->first;
    }
    return nQueueSize;
}
