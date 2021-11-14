#pragma once
// Copyright (c) 2014 The Pastel Core developers
// Copyright (c) 2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <vector>

class CNetAddr;

/** 
 * Median filter over a stream of values.
 * Returns the median of the last N numbers
 */
template <typename T>
class CMedianFilter
{
private:
    std::vector<T> vValues;
    std::vector<T> vSorted;
    size_t nSize;

public:
    CMedianFilter(const size_t size, const T initial_value) : 
        nSize(size)
    {
        vValues.reserve(size);
        vValues.push_back(initial_value);
        vSorted = vValues;
    }

    void input(const T value) noexcept
    {
        if (vValues.size() == nSize)
            vValues.erase(vValues.cbegin());
        vValues.push_back(value);

        vSorted.resize(vValues.size());
        std::copy(vValues.begin(), vValues.end(), vSorted.begin());
        std::sort(vSorted.begin(), vSorted.end());
    }

    T median() const noexcept
    {
        const size_t nSize = vSorted.size();
        assert(nSize > 0);
        if (nSize & 1) // Odd number of elements
            return vSorted[nSize / 2];
        // Even number of elements
        return (vSorted[nSize / 2 - 1] + vSorted[nSize / 2]) / 2;
    }

    size_t size() const noexcept
    {
        return vValues.size();
    }

    std::vector<T> sorted() const noexcept
    {
        return vSorted;
    }
};

/** Functions to keep track of adjusted P2P time */
int64_t GetTimeOffset();
int64_t GetAdjustedTime();
void AddTimeData(const CNetAddr& ip, int64_t nTime);

