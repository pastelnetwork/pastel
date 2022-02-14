#pragma once
// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

/**
 * An RAII-style reverse lock. Unlocks on construction and locks on destruction.
 */
template<typename Lock>
class reverse_lock
{
public:

    explicit reverse_lock(Lock& lock) : lock(lock)
    {
        lock.unlock();
        lock.swap(templock);
    }

    ~reverse_lock()
    {
        templock.lock();
        templock.swap(lock);
    }

private:
    reverse_lock(reverse_lock const&) = delete;
    reverse_lock& operator=(reverse_lock const&) = delete;

    Lock& lock;
    Lock templock;
};
