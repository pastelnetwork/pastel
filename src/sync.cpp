// Copyright (c) 2011-2012 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdio>
#include <unordered_map>

#include <sync.h>
#include <util.h>
#include <enum_util.h>
#include <utilstrencodings.h>

using namespace std;

#ifdef DEBUG_LOCKCONTENTION
void PrintLockContention(const char* szLockName, const char* szFile, const size_t nLine)
{
    LogPrintf("LOCKCONTENTION: %s\n", szLockName);
    LogPrintf("Locker: %s:%zu\n", szFile, nLine);
}
#endif /* DEBUG_LOCKCONTENTION */

#define ASSERT_ONLY_MAYBE_DEADLOCK 1

#ifdef DEBUG_LOCKORDER
//
// Early deadlock detection for mutexes and shared/exclusive locks.
// 
// Problem being solved:
// 1. For simple mutexes:
//    Thread 1 locks A, then B, then C
//    Thread 2 locks D, then C, then A
//     --> may result in deadlock between the two threads, depending on when they run.
// 
// 2: For shared/exclusive locks:
//    Thread 1 acquires a shared lock on A, then an exclusive lock on B.
//    Thread 2 acquires an exclusive lock on B and then a shared lock on A.
//    --> Thread 2 will block because of Thread 1's shared lock on A, while Thread 1 will block due to Thread 2's exclusive lock on B.
//
//    Thread 1 acquires a shared lock on A.
//    Thread 2 attempts to acquire an exclusive lock on A but blocks because of the shared lock held by Thread 1.
//    If Thread 1 then attempts to upgrade its shared lock to an exclusive lock, it will block as well, resulting in a deadlock.
// 
// Solution implemented here:
// Keep track of pairs of locks: (A before B), (A before C), etc., and their types (MUTEX, SHARED, EXCLUSIVE).
// Complain if any thread tries to lock in a different order or in a manner that could lead to a deadlock.
//

string LockTypeToString(LockType lockType) noexcept
{
    string typeStr;
    switch (lockType)
    {
        case LockType::MUTEX: 
            typeStr = "MUTEX";
            break;

        case LockType::SHARED:
            typeStr = "SHARED_LOCK";
			break;

        case LockType::EXCLUSIVE:
			typeStr = "EXCLUSIVE_LOCK";
			break;
    }
    return typeStr;
}

struct CLockLocation
{
    CLockLocation(const char* pszLockName, const char* pszFile, const size_t nLine, const bool fTryIn, LockType lockType = LockType::MUTEX) :
        m_sMutexName(pszLockName),
        m_sSourceFile(pszFile),
        m_nSourceLine(nLine),
        fTry(fTryIn),
        m_LockType(lockType)
    {}

    string ToString() const noexcept
    {
        string typeStr = LockTypeToString(m_LockType);
        return m_sMutexName + " (" + typeStr + ") " + m_sSourceFile + ":" + to_string(m_nSourceLine) + (fTry ? " (TRY)" : "");
    }

    string MutexName() const noexcept { return m_sMutexName; }
    LockType GetLockType() const noexcept { return m_LockType; }

    bool fTry;

private:
    string m_sMutexName;
    string m_sSourceFile;
    size_t m_nSourceLine;
    LockType m_LockType;
};

using lockid_t = pair<void*, LockType>;
using lockstack_t = vector<pair<lockid_t, CLockLocation>>;

struct LockIdTypeHasher
{
    size_t operator()(const pair<void*, LockType>& p) const
    {
        size_t h1 = hash<void*>{} (p.first);
        size_t h2 = hash<int>{} (to_integral_type(p.second));
        return h1 ^ h2;
    }
};

struct LockIdPairTypeHasher
{
    LockIdTypeHasher single_hasher;
    size_t operator()(const pair<pair<void*, LockType>, pair<void*, LockType>>& pp) const
    {
        size_t h1 = single_hasher(pp.first);
        size_t h2 = single_hasher(pp.second);
        return h1 ^ (h2 << 1);  // Shift h2 so it doesn't collide with h1
    }
};
static mutex dd_mutex;
static unordered_map<pair<lockid_t, lockid_t>, lockstack_t, LockIdPairTypeHasher> gl_LockOrders;
static thread_local unique_ptr<lockstack_t> gl_LockStack;

static void potential_deadlock_detected(const pair<lockid_t, lockid_t>& mismatch,
                                        const lockstack_t& s1, const lockstack_t& s2)
{
    // We attempt to not assert on probably-not deadlocks by assuming that
    // a try lock will immediately have otherwise bailed if it had
    // failed to get the lock
    // We do this by, for the locks which triggered the potential deadlock,
    // in either lockorder, checking that the second of the two which is locked
    // is only a TRY_LOCK, ignoring locks if they are reentrant.
    bool firstLocked = false;
    bool secondLocked = false;
    bool onlyMaybeDeadlock = false;

    const auto& p1 = mismatch.first;
    const auto& p2 = mismatch.second;

    // Check if it's a reader-writer or writer-reader deadlock
    const bool isSharedExclusiveDeadlock = p1.second == LockType::SHARED && p2.second == LockType::EXCLUSIVE;
    const bool isExclusiveSharedDeadlock = p1.second == LockType::EXCLUSIVE && p2.second == LockType::SHARED;
    const bool isExclusiveExclusiveDeadlock = p1.second == LockType::EXCLUSIVE && p2.second == LockType::EXCLUSIVE;
    const bool isRWDeadlock = isSharedExclusiveDeadlock || isExclusiveSharedDeadlock || isExclusiveExclusiveDeadlock;

    LogPrintf("POTENTIAL %s DEADLOCK DETECTED:\n", isRWDeadlock ? "RW" : "");
    string sMark;
    if (isRWDeadlock)
    {
        if (isSharedExclusiveDeadlock)
            LogPrintf("Shared lock followed by Exclusive lock is not allowed!\n");
        else if (isExclusiveSharedDeadlock)
            LogPrintf("Exclusive lock followed by Shared lock can lead to deadlocks!");
        else if (isExclusiveExclusiveDeadlock)
            LogPrintf("Two Exclusive locks can lead to deadlocks!");
        LogPrintf("Previous lock order was:\n");
        for (const auto& [lockID, lockLocation] : s2)
        {
            sMark.clear();
            if (lockID == p1)
                sMark = " (1)";
            else if (lockID == p2)
                sMark = " (2)";
            LogPrintf("%s %s\n", sMark, lockLocation.ToString());
        }
        LogPrintf("Current lock order is:\n");
        for (const auto& [lockID, lockLocation] : s1)
        {
            sMark.clear();
            if (lockID == p1)
                sMark = " (1)";
            else if (lockID == p2)
                sMark = " (2)";
            LogPrintf("%s %s\n", sMark, lockLocation.ToString());
        }
    }
    else
    {
        LogPrintf("Previous lock order was:\n");
        for (const auto& [lockID, lockLocation] : s2)
        {
            sMark.clear();
            if (lockID == p1)
            {
                sMark = " (1)";
                if (!firstLocked && secondLocked && lockLocation.fTry)
                    onlyMaybeDeadlock = true;
                firstLocked = true;
            }
            if (lockID == p2)
            {
                sMark = " (2)";
                if (!secondLocked && firstLocked && lockLocation.fTry)
                    onlyMaybeDeadlock = true;
                secondLocked = true;
            }
            LogPrintf("%s %s\n", sMark, lockLocation.ToString());
        }

        firstLocked = false;
        secondLocked = false;

        LogPrintf("Current lock order is:\n");
        for (const auto& [lockID, lockLocation] : s1)
        {
            sMark.clear();
            if (lockID == p1)
            {
                sMark = " (1)";
                if (!firstLocked && secondLocked && lockLocation.fTry)
                    onlyMaybeDeadlock = true;
                firstLocked = true;
            }
            if (lockID == p2)
            {
                sMark = " (2)";
                if (!secondLocked && firstLocked && lockLocation.fTry)
                    onlyMaybeDeadlock = true;
                secondLocked = true;
            }
            LogPrintf("%s %s\n", sMark, lockLocation.ToString());
        }
    }

#ifdef ASSERT_ONLY_MAYBE_DEADLOCK
    assert(onlyMaybeDeadlock);
#else
    cout << "POTENTIAL DEADLOCK DETECTED" << endl;
#endif
}

static void push_lock(void* c, CLockLocation&& lockLocation, const bool fTry)
{
    if (!gl_LockStack)
    {
        gl_LockStack = make_unique<lockstack_t>();
        gl_LockStack->reserve(8);
    }

    lockid_t currentLock = { c, lockLocation.GetLockType()};
    unique_lock<mutex> lock(dd_mutex);

    // register the lock in a thread-local stack
    gl_LockStack->emplace_back(currentLock, lockLocation);

    if (!fTry)
    {
        for (const auto& [prevLock, lockLocation] : *gl_LockStack)
        {
            if (prevLock == currentLock)
                break;

            auto p1 = make_pair(prevLock, currentLock);
            if (gl_LockOrders.find(p1) != gl_LockOrders.cend())
                continue;
            gl_LockOrders[p1] = (*gl_LockStack);

            auto p2 = make_pair(currentLock, prevLock);
            if (gl_LockOrders.find(p2) != gl_LockOrders.cend())
                potential_deadlock_detected(p1, gl_LockOrders[p2], gl_LockOrders[p1]);
        }
    }
}

static void pop_lock()
{
    unique_lock<mutex> lock(dd_mutex);
    gl_LockStack->pop_back();
}

void EnterCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry)
{
    push_lock(cs, CLockLocation(szLockName, szFile, nLine, fTry, LockType::MUTEX), fTry);
}

void EnterSharedCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry)
{
    push_lock(cs, CLockLocation(szLockName, szFile, nLine, fTry, LockType::SHARED), fTry);
}

void EnterExclusiveCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry)
{
    push_lock(cs, CLockLocation(szLockName, szFile, nLine, fTry, LockType::EXCLUSIVE), fTry);
}

void LeaveCritical()
{
    pop_lock();
}

string LocksHeld()
{
    string result;
    for (const auto &[lockID, lockLocation] : *gl_LockStack)
        result += lockLocation.ToString() + string("\n");
    return result;
}

bool IsLockHeld(void* cs, LockType lockType)
{
    if (!gl_LockStack)
        return false;

    for (const auto& [lockID, lockLocation] : *gl_LockStack)
    {
        if (lockID.first == cs && lockID.second == lockType)
            return true;
    }
    return false;
}

void AssertLockHeldInternal(const char* szLockName, const char* szFile, const size_t nLine, void* cs, LockType lockType)
{
    if (IsLockHeld(cs, lockType))
        return;

    LogPrintf("ERROR ! Assertion failed: lock %s of type %s not held in %s:%zu; locks held:\n%s",
        szLockName, LockTypeToString(lockType).c_str(), szFile, nLine, LocksHeld().c_str());
    abort();
}

void AssertLockNotHeldInternal(const char* szLockName, const char* szFile, const size_t nLine, void* cs, LockType lockType)
{
    if (!IsLockHeld(cs, lockType))
        return;

    LogPrintf("ERROR ! Assertion failed: lock %s of type %s held in %s:%zu; expected it not to be held; locks held:\n%s",
        szLockName, LockTypeToString(lockType).c_str(), szFile, nLine, LocksHeld().c_str());
    abort();
}

#else
void EnterCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry) {}
void EnterSharedCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry) {}
void EnterExclusiveCritical(const char* szLockName, const char* szFile, const size_t nLine, void* cs, const bool fTry) {}
void LeaveCritical() {}
string LocksHeld() { return ""; }
void AssertLockHeldInternal(const char* szLockName, const char* szFile, const size_t nLine, void* cs, LockType lockType) {}
void AssertLockNotHeldInternal(const char* pszName, const char* pszFile, const size_t nLine, void* cs, LockType type) {}
#endif /* DEBUG_LOCKORDER */
