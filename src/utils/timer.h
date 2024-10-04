#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chrono>

class CTimer
{
public:
    CTimer(const bool bAutoStart = false)
    {
        if (bAutoStart)
            start();
    }

    void start() noexcept
    {
        if (m_bStarted)
            return;
        m_StartTime = std::chrono::high_resolution_clock::now();
        m_StopTime = m_StartTime;
        m_bStarted = true;
    }

    void stop() noexcept
    {
        if (!m_bStarted)
            return;
        m_StopTime = std::chrono::high_resolution_clock::now();
        m_bStarted = false;
    }

    uint64_t elapsedMilliseconds() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(m_StopTime - m_StartTime).count();
    }

    uint64_t elapsedSeconds() const noexcept
    {
        return std::chrono::duration_cast<std::chrono::seconds>(m_StopTime - m_StartTime).count();
    }

private:
    bool m_bStarted = false;
    std::chrono::high_resolution_clock::time_point m_StartTime;
    std::chrono::high_resolution_clock::time_point m_StopTime;
};
