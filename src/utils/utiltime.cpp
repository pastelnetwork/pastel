// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include <utils/utiltime.h>

using namespace std;
using namespace chrono;

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
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int64_t GetTimeMicros() noexcept
{
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

void MilliSleep(int64_t n)
{
    this_thread::sleep_for(milliseconds(n));
}

string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    // Convert the time to a time_point
    system_clock::time_point tp = system_clock::from_time_t(nTime);

    // Create a time_t from the time_point for use with std::put_time
    time_t time = system_clock::to_time_t(tp);
    tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    // Format the time into a string
    stringstream ss;
    ss.imbue(locale::classic());
    ss << put_time(&tm, pszFormat);
    return ss.str();    
}

int64_t DecodeDumpTime(const string &str)
{
    constexpr auto timeFormat = "%Y-%m-%dT%H:%M:%SZ";

    tm tm = {};
    istringstream iss(str);
    iss.imbue(locale::classic()); 

    iss >> get_time(&tm, timeFormat);
    if (iss.fail())
		return 0;

    return static_cast<int64_t>(mktime(&tm));
}

string EncodeDumpTime(const int64_t nTime)
{
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

CSimpleTimer::CSimpleTimer(bool bAutoStart) noexcept : 
    m_startTime(),
    m_endTime(),
    m_bRunning(false),
    m_elapsedTime(0)
{
    if (bAutoStart)
		start();
}

void CSimpleTimer::start()
{
    m_startTime = high_resolution_clock::now();
    m_bRunning = true;
    m_elapsedTime = 0;
}

void CSimpleTimer::stop()
{
    if (!m_bRunning)
        return;

    m_endTime = high_resolution_clock::now();
    m_bRunning = false;
    m_elapsedTime += duration_cast<milliseconds>(m_endTime - m_startTime).count();
}

void CSimpleTimer::resume()
{
    if (m_bRunning)
        return;

    m_startTime = high_resolution_clock::now();
    m_bRunning = true;
}

int64_t CSimpleTimer::elapsed_time() const
{
    if (m_bRunning)
    {
        auto currentTime = high_resolution_clock::now();
        return m_elapsedTime + duration_cast<milliseconds>(currentTime - m_startTime).count();
    }
    return m_elapsedTime;
}

string CSimpleTimer::elapsed_time_str() const
{
    auto ms = elapsed_time();
    auto hours = ms / (1000 * 60 * 60);
    ms %= (1000 * 60 * 60);
    auto minutes = ms / (1000 * 60);
    ms %= (1000 * 60);
    auto seconds = ms / 1000;
    ms %= 1000;

    ostringstream oss;
    oss << setw(2) << setfill('0') << hours << ":"
        << setw(2) << setfill('0') << minutes << ":"
        << setw(2) << setfill('0') << seconds << "."
        << setw(3) << setfill('0') << ms;
    return oss.str();
}