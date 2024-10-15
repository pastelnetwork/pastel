#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#ifdef _MSC_VER
#include <intrin.h>
#include <cstdint>

static inline int __builtin_ctz(unsigned x)
{
    unsigned long ret;
    _BitScanForward(&ret, x);
    return static_cast<int>(ret);
}

static inline int __builtin_ctzll(unsigned long long x)
{
    unsigned long ret;
    _BitScanForward64(&ret, x);
    return static_cast<int>(ret);
}

static inline int __builtin_ctzl(unsigned long x)
{
    return sizeof(x) == 8 ? __builtin_ctzll(x) : __builtin_ctz((uint32_t)x);
}

static inline int __builtin_clz(unsigned x)
{
    return __lzcnt(x);
}

static inline int __builtin_clzll(unsigned long long x)
{
     return static_cast<int>(__lzcnt64(x));
}

static inline int __builtin_clzl(unsigned long x)
{
    return sizeof(x) == 8 ? __builtin_clzll(x) : __builtin_clz((uint32_t)x);
}

#ifdef __cplusplus
static inline int __builtin_ctzl(unsigned long long x)
{
    return __builtin_ctzll(x);
}

static inline int __builtin_clzl(unsigned long long x)
{
    return __builtin_clzll(x);
}
#endif
#endif

