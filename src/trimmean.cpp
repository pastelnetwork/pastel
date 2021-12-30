// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <unistd.h>
#include <sys/types.h>
#include <trimmean.h>
using namespace std;

///////////////////////////////////////////////////////////////////////////
// TRIMMEAN Helper Functions
///////////////////////////////////////////////////////////////////////////

/* Partitioning algorithm for QuickSort and QuickSelect */
static ssize_t partition(vector<CAmount>& v, const ssize_t low, const ssize_t high)
{
    // Pick the first element to be the pivot.
    const ssize_t pivotIndex = low;
    const CAmount pivot = v[low];
    ssize_t nLow(low), nHigh(high);
    do {
        while (nLow <= nHigh && v[nLow] <= pivot)
            ++nLow;
        while (v[nHigh] > pivot)
            nHigh--;
        if (nLow < nHigh)
            swap(v[nLow], v[nHigh]);
    } while (nLow < nHigh);

    swap(v[pivotIndex], v[nHigh]);
    return nHigh;
}

/* QuickSort algorithm */
static void quickSort(vector<CAmount>& v, const ssize_t nFirst, const ssize_t nLast)
{
    if (nLast - nFirst >= 1) {
        const ssize_t pivotIndex = partition(v, nFirst, nLast);

        quickSort(v, nFirst, pivotIndex - 1);
        quickSort(v, pivotIndex + 1, nLast);
    }
}

/* QuickSelect algorithm */
static CAmount quickSelect(vector<CAmount>& v, const CAmount first, const CAmount last, const CAmount k)
{
    if (last - first >= 1) {
        const CAmount pivotIndex = partition(v, first, last);

        if (pivotIndex == k)
            return v[pivotIndex];

        else if (k < pivotIndex)
            return quickSelect(v, first, pivotIndex - 1, k);

        else
            return quickSelect(v, pivotIndex + 1, last, k);
    }
    return v[first];
}

/* Calculate mean given starting and ending array index */
inline static double mean(const vector<CAmount>& v, const ssize_t low, const ssize_t high)
{
    CAmount acc = 0;

    for (ssize_t i = low; i <= high; i++)
        acc += v[i];

    return acc / static_cast<double>(high - low + 1);
}

///////////////////////////////////////////////////////////////////////////
// TRIMMEAN Implementation
///////////////////////////////////////////////////////////////////////////

// Given an array of integers, exclude "percent" percent of data points from the top and bottom tails
// of a data set. Calculate and return the mean of the remaining data.
//
// vInput: data set; vector of integers to examine
// percent: fractional number of data points to exclude, where 0 <= percent < 1
// errorno (optional): pointer to ErrorNumber enumerated type for additional error information
//
// If any errors are encountered, return NaN. If the errorno argument is defined, additional information
// about the offending error will be provided in the form of an error code.
double TRIMMEAN(const std::vector<CAmount>& vInput, const double percent, TrimmeanErrorNumber* pErrNo)
{
    /* Error Handling */
    constexpr double NaN = 0 * (1e308 * 1e308);
    TrimmeanErrorNumber err = TrimmeanErrorNumber::ENOERROR;
    do {
        if (percent < 0 || percent >= 1) // Percent out of range.
        {
            err = TrimmeanErrorNumber::EBADPCNT;
            break;
        }
        if (vInput.empty()) // empty input vector
        {
            err = TrimmeanErrorNumber::EBADINPUT;
            break;
        }
    } while (false);
    if (err != TrimmeanErrorNumber::ENOERROR)
    {
        if (pErrNo)
            *pErrNo = err;
        return NaN;
    }

    // Copy input data into a local array which we will sort: we don't want to modify the original
    // input array.
    vector<CAmount> v(vInput);

    // Use QuickSort algorithm to sort the array.
    quickSort(v, 0, v.size() - 1);

    // Calculate the number of elements to exclude and round down to the nearest even number.
    size_t elementsToExclude = static_cast<size_t>(v.size() * percent);
    if (elementsToExclude % 2 != 0)
        elementsToExclude--;

    // Using our sorted array, exclude the lowest and highest (elementsToExclude / 2) elements and
    // return the trimmed average.
    const ssize_t low = elementsToExclude / 2;
    const ssize_t high = v.size() - (elementsToExclude / 2) - 1;
    const double retVal = mean(v, low, high);
    return retVal;
}
