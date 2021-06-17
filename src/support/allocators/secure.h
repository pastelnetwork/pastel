#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "support/lockedpool.h"
#include "support/cleanse.h"
#include <string>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif // _MSC_VER
//
// Allocator that locks its contents from being paged
// out of memory and clears its contents before deletion.
//
template <typename T>
struct secure_allocator : public std::allocator<T> {
    using allocator_type = std::allocator<T>;
    using value_type = T;
    using reference = value_type&;
    using const_reference = const value_type&;
    using difference_type = std::ptrdiff_t;
    using size_type = std::size_t;
    using pointer = typename std::allocator_traits<allocator_type>::pointer;
    using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;
    secure_allocator() noexcept {}
    secure_allocator(const secure_allocator& a) noexcept : 
        allocator_type(a)
    {}
    template <typename U>
    secure_allocator(const secure_allocator<U>& a) noexcept : 
        allocator_type(a)
    {}
    ~secure_allocator() noexcept {}
    template <typename _Other>
    struct rebind {
        typedef secure_allocator<_Other> other;
    };

    T* allocate(std::size_t n, const void* hint = 0)
    {
        T* allocation = static_cast<T*>(LockedPoolManager::Instance().alloc(sizeof(T) * n));
        if (!allocation) {
            throw std::bad_alloc();
        }
        return allocation;
    }

    void deallocate(T* p, std::size_t n) noexcept
    {
        assert(p != nullptr);
        memory_cleanse(p, sizeof(T) * n);
        LockedPoolManager::Instance().free(p);
    }
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

// This is exactly like std::string, but with a custom allocator.
typedef std::basic_string<char, std::char_traits<char>, secure_allocator<char> > SecureString;
