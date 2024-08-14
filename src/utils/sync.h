#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <string>

#include <utils/threadsafety.h>

////////////////////////////////////////////////
//                                            //
// THE SIMPLE DEFINITION, EXCLUDING DEBUG CODE //
//                                            //
////////////////////////////////////////////////

/*
CCriticalSection mutex;
    std::recursive_mutex mutex;

LOCK(mutex);
    std::unique_lock<std::recursive_mutex> criticalblock(mutex);

LOCK2(mutex1, mutex2);
    std::unique_lock<std::recursive_mutex> criticalblock1(mutex1);
    std::unique_lock<std::recursive_mutex> criticalblock2(mutex2);

SHARED_LOCK(shared_mutex)
    std::shared_lock<std::shared_mutex> criticalSharedBlock(shared_mutex);

EXCLUSIVE_LOCK(shared_mutex)
    std::unique_lock<std::shared_mutex> criticalExclusiveBlock(shared_mutex);

TRY_LOCK(mutex, name);
    std::unique_lock<std::recursive_mutex> name(mutex, std::try_to_lock_t);

ENTER_CRITICAL_SECTION(mutex); // no RAII
    mutex.lock();

LEAVE_CRITICAL_SECTION(mutex); // no RAII
    mutex.unlock();
 */

///////////////////////////////
//                           //
// THE ACTUAL IMPLEMENTATION //
//                           //
///////////////////////////////

/**
 * Template mixin that adds -Wthread-safety locking
 * annotations to a subset of the mutex API.
 */
template <typename LOCKTYPE>
class LOCKABLE AnnotatedMixin : public LOCKTYPE
{
public:
    void lock() noexcept EXCLUSIVE_LOCK_FUNCTION()
    {
        LOCKTYPE::lock();
    }

    void unlock() noexcept UNLOCK_FUNCTION()
    {
        LOCKTYPE::unlock();
    }

    bool try_lock() noexcept EXCLUSIVE_TRYLOCK_FUNCTION(true)
    {
        return LOCKTYPE::try_lock();
    }
};

template <typename SHARED_LOCK_TYPE>
class LOCKABLE AnnotatedSharedMixin : public SHARED_LOCK_TYPE
{
public:
    void lock() noexcept EXCLUSIVE_LOCK_FUNCTION()
    {
        SHARED_LOCK_TYPE::lock();
    }

    void unlock() noexcept UNLOCK_FUNCTION()
    {
        SHARED_LOCK_TYPE::unlock();
    }

    bool try_lock() noexcept EXCLUSIVE_TRYLOCK_FUNCTION(true)
    {
        return SHARED_LOCK_TYPE::try_lock();
    }

    void lock_shared() noexcept SHARED_LOCK_FUNCTION()
	{
		SHARED_LOCK_TYPE::lock_shared();
	}

    void unlock_shared() noexcept UNLOCK_FUNCTION()
	{
		SHARED_LOCK_TYPE::unlock_shared();
	}

    bool try_lock_shared() noexcept SHARED_TRYLOCK_FUNCTION(true)
	{
		return SHARED_LOCK_TYPE::try_lock_shared();
	}
};

/**
 * Wrapped mutex: supports recursive locking, but no waiting
 * TODO: We should move away from using the recursive lock by default.
 */
typedef AnnotatedMixin<std::recursive_mutex> CCriticalSection;

/** Wrapped mutex: supports waiting but not recursive locking */
typedef AnnotatedMixin<std::mutex> CWaitableCriticalSection;

typedef AnnotatedSharedMixin<std::shared_mutex> CSharedMutex;
typedef AnnotatedSharedMixin<std::shared_timed_mutex> CSharedTimedMutex;

/** Just a typedef for std::condition_variable, can be wrapped later if desired */
typedef std::condition_variable CConditionVariable;

enum class LockType : int
{
    MUTEX = 1,  // Regular mutex lock
    SHARED,     // Shared mutex lock (read lock)
    EXCLUSIVE   // Exclusive mutex lock (write lock)
};

enum class LockingStrategy : int
{
    IMMEDIATE = 1,  // Lock immediately
    TRY,			// Try to lock
    DEFERRED		// Don't lock immediately
};

void EnterCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry = false);
void EnterSharedCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry = false);
void EnterExclusiveCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry = false);
void LeaveCritical();
void CleanupLockOrders(const void* lock);
std::string LocksHeld();
void AssertLockHeldInternal(const char* pszName, const char* pszFile, const size_t nLine, void* cs, LockType type);
void AssertLockNotHeldInternal(const char* pszName, const char* pszFile, const size_t nLine, void* cs, LockType type);
#define AssertLockHeld(cs) AssertLockHeldInternal(#cs, __FILE__, __LINE__, &cs, LockType::MUTEX)
#define AssertLockNotHeld(cs) AssertLockNotHeldInternal(#cs, __FILE__, __LINE__, &cs, LockType::MUTEX)

#ifdef DEBUG_LOCKCONTENTION
void PrintLockContention(const char* pszName, const char* pszFile, const size_t nLine);
#endif

template <typename Mutex>
class SCOPED_LOCKABLE CMutexLock
{
public:
    CMutexLock(Mutex& mutexIn, const char* szLockName, const char* szFile, const size_t nLine, 
               const LockingStrategy strategy = LockingStrategy::IMMEDIATE) : 
        lock(mutexIn, std::defer_lock)
    {
        switch (strategy)
        {
            case LockingStrategy::IMMEDIATE:
                Enter(szLockName, szFile, nLine);
                break;

            case LockingStrategy::TRY:
                TryEnter(szLockName, szFile, nLine);
                break;

            case LockingStrategy::DEFERRED:
                break;
        }
    }

    CMutexLock(Mutex* pmutexIn, const char* szLockName, const char* szFile, const size_t nLine,
        const LockingStrategy strategy = LockingStrategy::IMMEDIATE)
    {
        if (!pmutexIn)
            return;

        lock = std::unique_lock<Mutex>(*pmutexIn, std::defer_lock);

        switch (strategy)
        {
            case LockingStrategy::IMMEDIATE:
                Enter(szLockName, szFile, nLine);
                break;

            case LockingStrategy::TRY:
                TryEnter(szLockName, szFile, nLine);
                break;

            case LockingStrategy::DEFERRED:
                break;
        }
    }

    void Lock(const char* szLockName, const char* szFile, const size_t nLine)
    {
        Enter(szLockName, szFile, nLine);
    }

    bool TryLock(const char* szLockName, const char* szFile, const size_t nLine)
    {
		return TryEnter(szLockName, szFile, nLine);
	}

    void Unlock(const char* szLockName, const char* szFile, const size_t nLine) UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
        {
            LeaveCritical();
            lock.unlock();
        }
    }

    ~CMutexLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
        {
            LeaveCritical();
            CleanupLockOrders((void*)(lock.mutex()));
        }
    }

    operator bool()
    {
        return lock.owns_lock();
    }

private:
    std::unique_lock<Mutex> lock;

    void Enter(const char* szLockName, const char* szFile, const size_t nLine) EXCLUSIVE_LOCK_FUNCTION(lock.mutex())
    {
        EnterCritical(szLockName, szFile, nLine, (void*)(lock.mutex()));
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock())
        {
            PrintLockContention(szLockName, pszFile, nLine);
#endif
            lock.lock();
#ifdef DEBUG_LOCKCONTENTION
        }
#endif
    }

    bool TryEnter(const char* szLockName, const char* szFile, const size_t nLine) EXCLUSIVE_TRYLOCK_FUNCTION(true)
    {
        EnterCritical(szLockName, szFile, nLine, (void*)(lock.mutex()), true);
        const bool bLocked = lock.try_lock();
        if (!bLocked || !lock.owns_lock())
            LeaveCritical();
        return lock.owns_lock(); //-V1020
    }
};

template <typename SharedMutex>
class SCOPED_LOCKABLE CSharedMutexLock
{
public:
    CSharedMutexLock(SharedMutex& mutexIn, const char* szLockName, const char* szFile, const size_t nLine,
                     const LockingStrategy strategy = LockingStrategy::IMMEDIATE) :
        lock(mutexIn, std::defer_lock)
    {
        switch (strategy)
		{
            case LockingStrategy::IMMEDIATE:
				EnterShared(szLockName, szFile, nLine);
				break;

			case LockingStrategy::TRY:
				TryEnterShared(szLockName, szFile, nLine);
				break;

			case LockingStrategy::DEFERRED:
				break;
		}
    }

    CSharedMutexLock(SharedMutex* pmutexIn, const char* szLockName, const char* szFile, const size_t nLine,
                     const LockingStrategy strategy = LockingStrategy::IMMEDIATE)
    {
        if (!pmutexIn)
            return;

        lock = std::shared_lock<SharedMutex>(*pmutexIn, std::defer_lock);
        switch (strategy)
		{
			case LockingStrategy::IMMEDIATE:
				EnterShared(szLockName, szFile, nLine);
				break;

			case LockingStrategy::TRY:
				TryEnterShared(szLockName, szFile, nLine);
				break;

			case LockingStrategy::DEFERRED:
				break;
		}
    }

    void Lock(const char* szLockName, const char* szFile, const size_t nLine)
	{
		EnterShared(szLockName, szFile, nLine);
	}

    bool TryLock(const char* szLockName, const char* szFile, const size_t nLine)
    {
        return TryEnterShared(szLockName, szFile, nLine);
    }

    void Unlock() UNLOCK_FUNCTION()
    {
		if (lock.owns_lock())
		{
			LeaveCritical();
			lock.unlock();
		}
	}

    ~CSharedMutexLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
        {
            LeaveCritical();
            CleanupLockOrders((void*)(lock.mutex()));
        }
    }

    operator bool()
    {
        return lock.owns_lock();
    }

private:
    std::shared_lock<SharedMutex> lock;

    void EnterShared(const char* szLockName, const char* szFile, const size_t nLine) SHARED_LOCK_FUNCTION(lock.mutex())
    {
        EnterSharedCritical(szLockName, szFile, nLine, (void*)(lock.mutex()));
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock())
        {
            PrintLockContention(szLockName, szFile, nLine);
#endif
            lock.lock();
#ifdef DEBUG_LOCKCONTENTION
        }
#endif
    }

    bool TryEnterShared(const char* szLockName, const char* szFile, const size_t nLine) SHARED_TRYLOCK_FUNCTION(true)
    {
        EnterSharedCritical(szLockName, szFile, nLine, (void*)(lock.mutex()), true);
        const bool bLocked = lock.try_lock();
        if (!bLocked || !lock.owns_lock())
            LeaveCritical();
        return lock.owns_lock();
    }
};

template <typename SharedMutex>
class SCOPED_LOCKABLE CSharedMutexExclusiveLock
{
public:
    CSharedMutexExclusiveLock(SharedMutex& mutexIn, const char* szLockName, const char* szFile, const size_t nLine,
                              const LockingStrategy strategy = LockingStrategy::IMMEDIATE) :
        lock(mutexIn, std::defer_lock)
    {
        switch (strategy)
        {
            case LockingStrategy::IMMEDIATE:
				EnterExclusive(szLockName, szFile, nLine);
				break;

			case LockingStrategy::TRY:
				TryEnterExclusive(szLockName, szFile, nLine);
				break;

			case LockingStrategy::DEFERRED:
				
                break;
        }
    }

    CSharedMutexExclusiveLock(SharedMutex* pmutexIn, const char* szLockName, const char* szFile, const size_t nLine,
                              const LockingStrategy strategy = LockingStrategy::IMMEDIATE)
    {
        if (!pmutexIn)
            return;

        lock = std::unique_lock<SharedMutex>(*pmutexIn, std::defer_lock);
        switch (strategy)
        {
            case LockingStrategy::IMMEDIATE:
				EnterExclusive(szLockName, szFile, nLine);
				break;

			case LockingStrategy::TRY:
				TryEnterExclusive(szLockName, szFile, nLine);
				break;

			case LockingStrategy::DEFERRED:
				
                break;
        }
    }

    void Lock(const char* szLockName, const char* szFile, const size_t nLine)
    {
        EnterExclusive(szLockName, szFile, nLine);
    }

    bool TryLock(const char* szLockName, const char* szFile, const size_t nLine)
	{
		return TryEnterExclusive(szLockName, szFile, nLine);
	}

    void Unlock() UNLOCK_FUNCTION()
    {
		if (lock.owns_lock())
		{
			LeaveCritical();
			lock.unlock();
		}
	}

    ~CSharedMutexExclusiveLock() UNLOCK_FUNCTION()
    {
        if (lock.owns_lock())
        {
            LeaveCritical();
            CleanupLockOrders((void*)(lock.mutex()));
        }
    }

    operator bool()
    {
        return lock.owns_lock();
    }

private:
    std::unique_lock<SharedMutex> lock;

    void EnterExclusive(const char* szLockName, const char* szFile, const size_t nLine) EXCLUSIVE_LOCK_FUNCTION(lock.mutex())
    {
        EnterExclusiveCritical(szLockName, szFile, nLine, (void*)(lock.mutex()));
#ifdef DEBUG_LOCKCONTENTION
        if (!lock.try_lock())
        {
            PrintLockContention(szLockName, szFile, nLine);
#endif
            lock.lock();
#ifdef DEBUG_LOCKCONTENTION
        }
#endif
    }

    bool TryEnterExclusive(const char* szLockName, const char* szFile, const size_t nLine)
    {
        EnterExclusiveCritical(szLockName, szFile, nLine, (void*)(lock.mutex()), true);
        const bool bLocked = lock.try_lock();
        if (!bLocked || !lock.owns_lock())
            LeaveCritical();
        return lock.owns_lock();
    }
};

typedef CMutexLock<CCriticalSection> CCriticalBlock;
typedef CMutexLock<CWaitableCriticalSection> CWaitableCriticalBlock;
typedef CSharedMutexLock<CSharedMutex> CCriticalSharedBlock;
typedef CSharedMutexExclusiveLock<CSharedMutex> CCriticalExclusiveBlock;

constexpr bool USE_LOCK = true;
constexpr bool SKIP_LOCK = false;

#define LOCK(cs) CCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__)
#define LOCK_DEFERRED(cs) CCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__, LockingStrategy::DEFERRED)
#define LOCK_COND(condition, cs) \
    CCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__, LockingStrategy::DEFERRED); \
    if (condition) \
		criticalblock.Lock(#cs, __FILE__, __LINE__);
#define SIMPLE_LOCK(cs) CWaitableCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__)
#define SIMPLE_LOCK_DEFERRED(cs) CWaitableCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__, LockingStrategy::DEFERRED)
#define SIMPLE_LOCK_COND(condition, cs) \
	CWaitableCriticalBlock criticalblock(cs, #cs, __FILE__, __LINE__, LockingStrategy::DEFERRED); \
	if (condition) \
		criticalblock.Lock(#cs, __FILE__, __LINE__);
#define DEFERRED_LOCK(condition, cs) \
    if (condition) \
        criticalblock.Lock(#cs, __FILE__, __LINE__);
#define LOCK2(cs1, cs2) CCriticalBlock criticalblock1(cs1, #cs1, __FILE__, __LINE__), criticalblock2(cs2, #cs2, __FILE__, __LINE__)
#define LOCK2_RS(cs1_recursive, cs2_simple) \
    CCriticalBlock criticalblock1(cs1_recursive, #cs1_recursive, __FILE__, __LINE__); \
    CWaitableCriticalBlock criticalblock2(cs2_simple, #cs2_simple, __FILE__, __LINE__)
#define LOCK2_COND(condition1, cs1, condition2, cs2) \
    CCriticalBlock criticalblock1(cs1, #cs1, __FILE__, __LINE__, LockingStrategy::DEFERRED); \
    CCriticalBlock criticalblock2(cs2, #cs2, __FILE__, __LINE__, LockingStrategy::DEFERRED); \
    if (condition1) \
		criticalblock1.Lock(#cs1, __FILE__, __LINE__); \
    if (condition2) \
		criticalblock2.Lock(#cs2, __FILE__, __LINE__);

#define TRY_LOCK(cs, name) CCriticalBlock name(cs, #cs, __FILE__, __LINE__, LockingStrategy::TRY)
#define TRY_LOCK_COND(condition, cs, name) \
    CCriticalBlock name(cs, #cs, __FILE__, __LINE__, LockingStrategy::DEFERRED); \
	if (condition) \
		name.TryLock(#cs, __FILE__, __LINE__);
#define TRY_SIMPLE_LOCK(cs, name) CWaitableCriticalBlock name(cs, #cs, __FILE__, __LINE__, LockingStrategy::TRY)
#define SHARED_LOCK(cs) CCriticalSharedBlock sharedBlock(cs, #cs, __FILE__, __LINE__)
#define EXCLUSIVE_LOCK(cs) CCriticalExclusiveBlock exclusiveBlock(cs, #cs, __FILE__, __LINE__)

#define ENTER_CRITICAL_SECTION(cs)                            \
    {                                                         \
        EnterCritical(#cs, __FILE__, __LINE__, (void*)(&cs)); \
        (cs).lock();                                          \
    }

#define LEAVE_CRITICAL_SECTION(cs) \
    {                              \
        (cs).unlock();             \
        LeaveCritical();           \
    }

class CSemaphore
{
private:
    std::condition_variable condition;
    std::mutex mtx;
    size_t nValue;

public:
    CSemaphore(size_t nInitValue) noexcept : 
        nValue(nInitValue)
    {}

    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (nValue < 1)
            condition.wait(lock);
        nValue--;
    }

    bool try_wait()
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (nValue < 1)
            return false;
        nValue--;
        return true;
    }

    void post()
    {
        {
            std::unique_lock<std::mutex> lock(mtx);
            nValue++;
        }
        condition.notify_one();
    }
};

/** RAII-style semaphore lock */
class CSemaphoreGrant
{
private:
    std::shared_ptr<CSemaphore> m_semaphore;
    bool fHaveGrant;

public:
    CSemaphoreGrant() noexcept : 
        m_semaphore(nullptr), 
        fHaveGrant(false)
    {}

    explicit CSemaphoreGrant(std::shared_ptr<CSemaphore> semaphore, bool fTry = false) : 
        m_semaphore(semaphore),
        fHaveGrant(false)
    {
        if (fTry)
            TryAcquire();
        else
            Acquire();
    }

    ~CSemaphoreGrant()
    {
        Release();
    }

    void Acquire()
    {
        if (fHaveGrant || !m_semaphore)
            return;
        m_semaphore->wait();
        fHaveGrant = true;
    }

    void Release()
    {
        if (!fHaveGrant || !m_semaphore)
            return;
        m_semaphore->post();
        fHaveGrant = false;
    }

    bool TryAcquire()
    {
        if (!m_semaphore)
			return false;
        if (!fHaveGrant && m_semaphore->try_wait())
            fHaveGrant = true;
        return fHaveGrant;
    }

    void MoveTo(CSemaphoreGrant& grant)
    {
        grant.Release();
        grant.m_semaphore = move(m_semaphore);
        grant.fHaveGrant = fHaveGrant;
        fHaveGrant = false;
    }

    operator bool() const noexcept
    {
        return fHaveGrant;
    }
};
