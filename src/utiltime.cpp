// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "utiltime.h"

#include <chrono>
#include <thread>
#include <boost/date_time/posix_time/posix_time.hpp>

using namespace std;

static int64_t nMockTime = 0;  //! For unit testing

int64_t GetTime() noexcept
{
    if (nMockTime)
        return nMockTime;
    return time(nullptr);
}

void SetMockTime(const int64_t nMockTimeIn) noexcept
{
    nMockTime = nMockTimeIn;
}

int64_t GetTimeMillis() noexcept
{
    return chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();
}

int64_t GetTimeMicros() noexcept
{
    return chrono::duration_cast<chrono::microseconds>(
            chrono::system_clock::now().time_since_epoch()).count();
}

void MilliSleep(int64_t n)
{
    this_thread::sleep_for(chrono::milliseconds(n));
}

string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    // locale takes ownership of the pointer
    locale loc(locale::classic(), new boost::posix_time::time_facet(pszFormat));
    stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}
