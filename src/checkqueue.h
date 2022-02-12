#pragma once
// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <vector>
#include <mutex>
#include <condition_variable>

#include <sync.h>
#include <svc_thread.h>

/** 
 * Queue for verifications that have to be performed.
  * The verifications are represented by a type T, which must provide an
  * operator(), returning a bool.
  *
  * One thread (the master) is assumed to push batches of verifications
  * onto the queue, where they are processed by N-1 worker threads. When
  * the master is done adding work, it temporarily joins the worker pool
  * as an N'th worker, until all jobs are done.
  */
template <typename T>
class CCheckQueue
{
public:
    // Mutex to ensure only one concurrent CCheckQueueWorkerManager
    std::mutex ControlMutex;

    //! Create a new check queue
    CCheckQueue(const size_t nBatchSize) :
        m_nIdle(0), 
        m_nTotal(0),
        m_bStopRequested(false),
        fAllOk(true), 
        m_nTodo(0), 
        m_nBatchSize(nBatchSize)
    {}
    CCheckQueue(const CCheckQueue&) = delete;
    CCheckQueue& operator=(const CCheckQueue&) = delete;
    ~CCheckQueue() = default;

    // verification worker
    void Worker()
    {
        Loop(false);
    }

    // master verification worker
    bool MasterWorker()
    {
        return Loop(true);
    }

    //! Add a batch of checks to the queue
    void Add(std::vector<T>& vChecks)
    {
        std::unique_lock<std::mutex> lock(mtx);
        for (T& check : vChecks)
        {
            m_queue.emplace_back();
            check.swap(m_queue.back());
        }
        m_nTodo += vChecks.size();
        if (vChecks.size() == 1)
            m_condWorker.notify_one();
        else if (vChecks.size() > 1)
            m_condWorker.notify_all();
    }

    bool IsIdle() const noexcept
    {
        std::unique_lock<std::mutex> lock(mtx);
        return (m_nTotal == m_nIdle && m_nTodo == 0 && fAllOk);
    }

    // stop all workers
    void stop(const bool bMaster)
    {
        auto& cond = bMaster ? m_condMaster : m_condWorker;
        {
            std::unique_lock<std::mutex> lock(mtx);
            m_bStopRequested = true;
            cond.notify_all();
        }
    }

private:
    //! Mutex to protect the inner state
    mutable std::mutex mtx;

    //! Worker threads block on this when out of work
    std::condition_variable m_condWorker;

    //! Master thread blocks on this when out of work
    std::condition_variable m_condMaster;

    //! The queue of elements to be processed.
    //! As the order of booleans doesn't matter, it is used as a LIFO (stack)
    std::vector<T> m_queue;

    //! The number of workers (including the master) that are idle.
    size_t m_nIdle;

    //! The total number of workers (including the master).
    size_t m_nTotal;

    // if true - stop was requested
    bool m_bStopRequested;

    //! The temporary evaluation result.
    bool fAllOk;

    /**
     * Number of verifications that haven't completed yet.
     * This includes elements that are no longer queued, but still in the
     * worker's own batches.
     */
    size_t m_nTodo;

    //! The maximum number of elements to be processed in one batch
    size_t m_nBatchSize;

    // Worker thread that does bulk of the verification work.
    bool Loop(const bool bMaster)
    {
        auto& cond = bMaster ? m_condMaster : m_condWorker;
        std::vector<T> vChecks;
        vChecks.reserve(m_nBatchSize);
        size_t nNow = 0;
        bool fOk = true;
        do
        {
            {
                std::unique_lock<std::mutex> lock(mtx);
                // first do the clean-up of the previous loop run (allowing us to do it in the same critsect)
                if (nNow)
                {
                    fAllOk &= fOk;
                    m_nTodo -= nNow;
                    if (m_nTodo == 0 && !bMaster)
                        // We processed the last element; inform the master it can exit and return the result
                        m_condMaster.notify_one();
                } else // first iteration
                    m_nTotal++;

                // logically, the do loop starts here
                while (m_queue.empty())
                {
                    if ((bMaster || m_bStopRequested) && m_nTodo == 0)
                    {
                        m_nTotal--;
                        const bool fRet = fAllOk;
                        // reset the status for new work later
                        if (bMaster)
                            fAllOk = true;
                        // return the current status
                        return fRet;
                    }
                    m_nIdle++;
                    cond.wait(lock); // wait
                    m_nIdle--;
                }
                // Decide how many work units to process now.
                // * Do not try to do everything at once, but aim for increasingly smaller batches so
                //   all workers finish approximately simultaneously.
                // * Try to account for idle jobs which will instantly start helping.
                // * Don't do batches smaller than 1 (duh), or larger than nBatchSize.
                nNow = std::max<size_t>(1U, std::min<size_t>(m_nBatchSize, m_queue.size() / (m_nTotal + m_nIdle + 1)));
                vChecks.resize(nNow);
                for (size_t i = 0; i < nNow; ++i)
                {
                    // We want the lock on the mutex to be as short as possible, so swap jobs from the global
                    // queue to the local batch vector instead of copying.
                    vChecks[i].swap(m_queue.back());
                    m_queue.pop_back();
                }
                // Check whether we need to do work at all
                fOk = fAllOk;
            }
            // execute work
            for (T& check : vChecks)
            {
                if (fOk)
                    fOk = check();
            }
            vChecks.clear();
        } while (true);
    }
};

/** 
 * RAII-style controller object for a CCheckQueue that guarantees the passed
 * queue is finished before continuing.
 */
template <typename T>
class CCheckQueueWorkerThread : public CServiceThread
{
public:
    explicit CCheckQueueWorkerThread(CCheckQueue<T> *pQueueManager, const bool bMaster, const char *szThreadName) :
        CServiceThread(szThreadName),
        m_pQueueManager(pQueueManager),
        m_bMaster(bMaster),
        m_bDone(false)
    {
        if (m_pQueueManager && m_bMaster)
            ENTER_CRITICAL_SECTION(pQueueManager->ControlMutex);
    }
    CCheckQueueWorkerThread(const CCheckQueueWorkerThread&) = delete;
    CCheckQueueWorkerThread& operator=(const CCheckQueueWorkerThread&) = delete;

    ~CCheckQueueWorkerThread() override
    {
        if (!m_bDone)
        {
            Wait();
            stop();
        }
        if (m_pQueueManager && m_bMaster)
            LEAVE_CRITICAL_SECTION(m_pQueueManager->ControlMutex);
    }

    bool Wait()
    {
        if (!m_pQueueManager)
            return true;
        const bool bRet = m_pQueueManager->MasterWorker();
        m_bDone = true;
        return bRet;
    }

    void execute() override
    {
        if (m_bMaster)
            Wait();
        else if (m_pQueueManager)
            m_pQueueManager->Worker();
    }

    void Add(std::vector<T>& vChecks)
    {
        if (!m_pQueueManager)
            return;
        m_pQueueManager->Add(vChecks);
    }

    void stop() override
    {
        CServiceThread::stop();
        if (m_pQueueManager)
            m_pQueueManager->stop(m_bMaster);
    }

private:
    CCheckQueue<T>* m_pQueueManager;

    bool m_bMaster; // true - master worker thread
    bool m_bDone;   // true - all checks completed
};
