#pragma once
// Copyright (c) 2022-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_map>

#include <compat.h>
#include <extlibs/scope_guard.hpp>
#include <utils/str_utils.h>
#include <utils/util.h>

class CServiceThread;

/** 
 * Exception that thrown by func_thread_interrupt_point() to use in thread functions.
 */
class func_thread_interrupted : public std::exception
{};

#ifdef __MINGW64__
extern __thread CServiceThread *funcThreadObj;
#else
extern thread_local CServiceThread *funcThreadObj;
#endif

/** 
 * Class to enhance std::thread.
 * Inherit your class from this and override execute() function.
 * Child class should call shouldStop() method to check if the stop was requested.
 */
class CServiceThread
{
public:
    CServiceThread(const char *szThreadName) noexcept: 
        m_bTrace(true),
        m_bRunning(false),
        m_bStopRequested(false)
    {
        m_sThreadName = strprintf("psl-%s", SAFE_SZ(szThreadName));
    }

    // disable copy
    CServiceThread(const CServiceThread &) = delete;
    CServiceThread& operator=(const CServiceThread &) = delete;

    // wait for thread stop on shutdown
    virtual ~CServiceThread() { waitForStop(); }

    /**
     * Create new thread and call execute().
     * 
     * \return true if thread created successfully
     */
    bool start(std::string &error) noexcept
    {
        error.clear();
        try
        {
            m_Thread = std::thread(&CServiceThread::run, this);
        }
        catch (const std::exception& exc)
        {
            error = strprintf("Exception occurred on thread [%s] creation: %s", m_sThreadName, exc.what());
			LogPrintf("%s\n", error);
			return false;
		}
        catch(...) {
            error = strprintf("Exception occurred on thread [%s] creation", m_sThreadName);
			LogPrintf("%s\n", error);
            return false;
        }
        return true;
    }

    /** 
     * Main thread run() function.
     * Does not throw exceptions.
     */
    void run() noexcept
    {
        m_bRunning = true; // atomic
        funcThreadObj = this;
        auto guard = sg::make_scope_guard([&]() noexcept
        {
            m_bRunning = false;
        });
        try
        {
            // rename thread
            RenameThread(m_sThreadName.c_str(), reinterpret_cast<void*>(m_Thread.native_handle()));
            if (m_bTrace)
                LogPrintf("[%s] thread start\n", m_sThreadName);
            execute();
            if (m_bTrace)
                LogPrintf("[%s] thread exit\n", m_sThreadName);
        } catch ([[maybe_unused]] const func_thread_interrupted &fe) {
            LogPrintf("[%s] thread interrupted\n", m_sThreadName);
        } catch (const std::exception& e) {
            PrintExceptionContinue(&e, m_sThreadName.c_str());
        } catch (...) {
            PrintExceptionContinue(nullptr, m_sThreadName.c_str());
        }
    }

    /**
     * Request thread to stop - does not wait for thread to join.
     */
    virtual void stop() noexcept
    {
        if (!m_bRunning)
            return;
        m_bStopRequested = true;
    }

    /** 
     * Wait for the running thread to join.
     * Calls stop() if stop is not requested yet.
     */
    virtual void waitForStop()
    {
        if (!m_bStopRequested)
            stop();
        if (m_Thread.joinable())
            m_Thread.join();
    }

    // returns true if thread stop has been requested
    virtual bool shouldStop() const noexcept { return m_bStopRequested; }
    // returns true if thread is running
    bool isRunning() const noexcept { return m_bRunning; }
    std::string get_thread_name() const noexcept { return m_sThreadName; }
    // log thread start/stop
    void setTrace(const bool bTrace) noexcept { m_bTrace = bTrace; }

    // main thread loop - should override this method
    virtual void execute() { throw new std::runtime_error("CServiceThread::execute method should be overriden"); }

protected:
    bool m_bTrace; // trace thread start/exit
    std::string m_sThreadName;         // thread name
    std::atomic_bool m_bRunning;       // true when thread is running
    std::atomic_bool m_bStopRequested; // true if stop was requested
    std::thread m_Thread;
};

inline void func_thread_interrupt_point()
{
    if (funcThreadObj && funcThreadObj->shouldStop())
        throw func_thread_interrupted();
}

/** 
 * Class to run any callable function in a thread.
 * This can be lambda, standalone function, class member function.
 * Function parameters can be bound using std::bind.
 */
template <typename Callable>
class CFuncThread : public CServiceThread
{
public:
    CFuncThread(const char *szName, Callable func) : 
        CServiceThread(szName),
        m_func(func)
    {}

    void execute() override
    {
        m_func();
    }

private:
    Callable m_func;
};

/** 
  * Stoppable thread class.
  */
class CStoppableServiceThread : public CServiceThread
{
public:
    CStoppableServiceThread(const char *szThreadName) noexcept: 
        CServiceThread(szThreadName)
    {}

    void stop() noexcept override
    {
        CServiceThread::stop();
        m_condVar.notify_one();
    }

    void sendSignal() noexcept { m_condVar.notify_one(); }

protected:
    std::mutex m_mutex;
    std::condition_variable m_condVar;
};

constexpr size_t INVALID_THREAD_OBJECT_ID = std::numeric_limits<size_t>::max();

/** 
 * Group of threads.
 */
class CServiceThreadGroup
{
public:
    CServiceThreadGroup() : 
        m_nCurrentID(0)
    {}

    /**
     * Add thread to thread group.
     * 
     * \param error - error message 
     * \param t - instance of class inherited from CServiceThread (create with make_shared)
     * \param bStartThread - if true - start thread after it was added
     * 
     * \return - id of the thread object that can be used later on to access it
     *           INVALID_THREAD_OBJECT_ID if failed to add thread, error message returned in this case
     */
    size_t add_thread(std::string &error, std::shared_ptr<CServiceThread> t, const bool bStartThread = true)
    {
        error.clear();
        std::unique_lock lck(m_Lock);
        ++m_nCurrentID;
        auto pr = m_vThreads.emplace(m_nCurrentID, std::move(t));
        if (pr.second && bStartThread)
        {
            if (!pr.first->second->start(error))
            {
                // failed to create thread
                // remove thread from map and return -1
                m_vThreads.erase(m_nCurrentID);
                return INVALID_THREAD_OBJECT_ID;
            }
        }
        return pr.second ? m_nCurrentID : INVALID_THREAD_OBJECT_ID;
    }

    /** 
     * Add Callable function to thread group.
     * 
     * \param error - error message
     * \param szThreadName - thread name used for identification
     * \param func - callable function (any lambfa, std::function or std::bind(...))
     * \param bStartThread - if true - start thread after it was added
     * 
     * \return id of the thread object that can be used later on to access it
     *         INVALID_THREAD_OBJECT_ID if failed to add thread
     */
    template <typename Callable>
    size_t add_func_thread(std::string &error, const char *szThreadName, Callable &&func, const bool bStartThread = true)
    {
        return add_thread(error, std::make_shared<CFuncThread<Callable>>(szThreadName, func), bStartThread);
    }

    /** 
     * Start thread by its id.
     * 
     * \param error - error message
     * \param nThreadObjectID - id of the thread object
     * 
     * \return true if thread started successfully, false otherwise
     */
    bool start_thread(std::string &error, const size_t nThreadObjectID)
    {
		std::unique_lock lck(m_Lock);
		auto it = m_vThreads.find(nThreadObjectID);
        if (it == m_vThreads.end())
        {
            error = strprintf("Thread object with ID=%zu not found in a thread group", nThreadObjectID);
            return false;
        }
        auto threadObj = it->second;
        if (!threadObj)
        {
			error = strprintf("Thread object with ID=%zu is not defined", nThreadObjectID);
			return false;
		}
        if (threadObj->isRunning())
            return true;
        if (threadObj->start(error))
        {
            LogFnPrintf("Thread '%s' (ID=%zu) has been started", threadObj->get_thread_name(), nThreadObjectID);
            return true;
        }
        error = strprintf("Failed to start thread '%s' (ID=%zu). %s",
            threadObj->get_thread_name(), nThreadObjectID, error);
        return false;
	}

    /**
     * Call stop() for all thread objects.
     * Does not wait for threads to join.
     */
    void stop_all()
    {
        std::unique_lock lck(m_Lock);
        for (auto& [nID, threadObj] : m_vThreads)
        {
            if (threadObj)
                threadObj->stop();
        }
    }

    /**  
     * Wait for all threads to join.
     */
    void join_all()
    {
        std::unique_lock lck(m_Lock);
        for (auto& [nID, threadObj] : m_vThreads) {
            if (threadObj)
                threadObj->waitForStop();
        }
        m_vThreads.clear();
        m_nCurrentID = 0;
    }

    size_t size() const noexcept
    {
        std::unique_lock lck(m_Lock);
        return m_vThreads.size();
    }

    bool empty() const noexcept { return size() == 0; }

private:
    mutable std::mutex m_Lock;
    size_t m_nCurrentID;
    std::unordered_map<size_t, std::shared_ptr<CServiceThread>> m_vThreads;
};
