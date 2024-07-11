#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <string>
#include <chrono>

int64_t GetTime() noexcept;
int64_t GetTimeMillis() noexcept;
int64_t GetTimeMicros() noexcept;
void SetMockTime(int64_t nMockTimeIn) noexcept;
void MilliSleep(int64_t n);

// decode UTC time from string in format "YYYY-MM-DD HH:MM:SS"
int64_t DecodeDumpTime(const std::string& str);
// encode UTC time to string in format "YYYY-MM-DD HH:MM:SS"
std::string EncodeDumpTime(const int64_t nTime);
// format UTC time to string with the given format
std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime);

class CSimpleTimer
{
public:
    CSimpleTimer(bool bAutoStart = false) noexcept;

    void start();
    void stop();
    void resume();
    bool is_started() const { return m_bRunning; }
    int64_t elapsed_time() const;
    std::string elapsed_time_str() const;

private:
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_endTime;
    bool m_bRunning;
    int64_t m_elapsedTime; // Elapsed time in milliseconds
};