// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cinttypes>

#include <timedata.h>
#include <netbase.h>
#include <sync.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>

using namespace std;

static CCriticalSection cs_nTimeOffset;
static int64_t gl_nTimeOffset = 0;

/**
 * "Never go to sea with two chronometers; take one or three."
 * Our three time sources are:
 *  - System clock
 *  - Median of other nodes clocks
 *  - The user (asking the user to fix the system clock if the first two disagree)
 */
int64_t GetTimeOffset() noexcept
{
    LOCK(cs_nTimeOffset);
    return gl_nTimeOffset;
}

int64_t GetAdjustedTime() noexcept
{
    return GetTime() + GetTimeOffset();
}

static int64_t abs64(int64_t n)
{
    return (n >= 0 ? n : -n);
}

constexpr size_t PASTEL_TIMEDATA_MAX_SAMPLES = 200;

void AddTimeData(const CNetAddr& ip, int64_t nOffsetSample)
{
    LOCK(cs_nTimeOffset);
    // Ignore duplicates
    static set<CNetAddr> setKnown;
    if (setKnown.size() == PASTEL_TIMEDATA_MAX_SAMPLES)
        return;
    if (!setKnown.insert(ip).second)
        return;

    // Add data
    static CMedianFilter<int64_t> vTimeOffsets(PASTEL_TIMEDATA_MAX_SAMPLES, 0);
    vTimeOffsets.input(nOffsetSample);
    LogFnPrintf("Added time data, samples %zu, offset %+" PRId64 " (%+" PRId64 " minutes)", vTimeOffsets.size(), nOffsetSample, nOffsetSample/60);

    // There is a known issue here (see issue #4521):
    //
    // - The structure vTimeOffsets contains up to 200 elements, after which
    // any new element added to it will not increase its size, replacing the
    // oldest element.
    //
    // - The condition to update nTimeOffset includes checking whether the
    // number of elements in vTimeOffsets is odd, which will never happen after
    // there are 200 elements.
    //
    // But in this case the 'bug' is protective against some attacks, and may
    // actually explain why we've never seen attacks which manipulate the
    // clock offset.
    //
    // So we should hold off on fixing this and clean it up as part of
    // a timing cleanup that strengthens it in a number of other ways.
    //
    if (vTimeOffsets.size() >= 5 && vTimeOffsets.size() % 2 == 1)
    {
        int64_t nMedian = vTimeOffsets.median();
        std::vector<int64_t> vSorted = vTimeOffsets.sorted();
        // Only let other nodes change our time by so much
        if (abs64(nMedian) < 70 * 60)
        {
            gl_nTimeOffset = nMedian;
        }
        else
        {
            gl_nTimeOffset = 0;

            static bool fDone;
            if (!fDone)
            {
                // If nobody has a time different than ours but within 5 minutes of ours, give a warning
                bool fMatch = false;
                for (const auto &nOffset : vSorted)
                {
                    if (nOffset != 0 && abs64(nOffset) < 5 * 60)
                        fMatch = true;
                }

                if (!fMatch)
                {
                    fDone = true;
                    string strMessage = translate("Warning: Please check that your computer's date and time are correct! If your clock is wrong Pastel will not work properly.");
                    strMiscWarning = strMessage;
                    LogFnPrintf("*** %s", strMessage);
                    uiInterface.ThreadSafeMessageBox(strMessage, "", CClientUIInterface::MSG_WARNING);
                }
            }
        }
        string s;
        if (fDebug)
        {
            for (const auto &n : vSorted)
                s += strprintf("%+d  ", n);
            s += "|  ";
        }
        s += strprintf("nTimeOffset = %+d  (%+d minutes)", gl_nTimeOffset, gl_nTimeOffset/60);
        LogFnPrintf(s);
    }
}
