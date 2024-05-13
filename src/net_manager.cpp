// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chrono>
#include <net.h>
#include <net_manager.h>

using namespace std;

CNetManagerThread gl_NetMgr;

constexpr chrono::seconds ACTIVE_CHECK_PERIOD_SECS = 10s;
constexpr chrono::seconds INACTIVE_CHECK_PERIOD_SECS = 2s;
constexpr  int64_t INACTIVITY_CHECK_GRACE_PERIOD_SECS = 30;

CNetManagerThread::CNetManagerThread() : 
    CStoppableServiceThread("netmgr")
{
    m_bNetworkActive = true;
    m_nNetworkActiveStartTime = GetTime();
    m_nNetworkInactiveStartTime = 0;
}

void CNetManagerThread::NotifyNetworkConnected()
{
    m_bNetworkActive = true;
    m_nNetworkActiveStartTime = time(nullptr);
    LogFnPrintf("!!! <<< NETWORK CONNECTED >>> !!!");
}

void CNetManagerThread::NotifyNetworkDisconnected()
{
    LogFnPrintf("!!! <<< NETWORK IS UNREACHABLE >>> !!!");
    m_bNetworkActive = false;
    m_nNetworkInactiveStartTime = GetTime();
}

chrono::seconds CNetManagerThread::checkNetworkConnectivity()
{
    const bool bPrevNetworkState = m_bNetworkActive;
    const bool bHasActiveNetworkInterface = hasActiveNetworkInterface();
    if (!bHasActiveNetworkInterface)
    {
        if (bPrevNetworkState)
        {
            LogFnPrintf("No active network interfaces detected!!! Checking internet connectivity...");
            NotifyNetworkDisconnected();
        }
        return INACTIVE_CHECK_PERIOD_SECS;
    }
    const bool bHasInternetConnectivity = hasInternetConnectivity([this]() { return this->shouldStop(); });
    if (bHasInternetConnectivity)
    {
        if (!bPrevNetworkState)
            NotifyNetworkConnected();
        return ACTIVE_CHECK_PERIOD_SECS;
    }
    if (bPrevNetworkState)
        NotifyNetworkDisconnected();
    return INACTIVE_CHECK_PERIOD_SECS;
}

void CNetManagerThread::execute()
{
    chrono::seconds check_period = ACTIVE_CHECK_PERIOD_SECS;
    while (!shouldStop())
    {
        unique_lock lck(m_mutex);
        if (m_condVar.wait_for(lck, check_period) == cv_status::no_timeout)
            continue;
        check_period = checkNetworkConnectivity();
    }
}

bool CNetManagerThread::IsNetworkConnectedRecently() const noexcept
{
    if (!m_bNetworkActive || !m_nNetworkInactiveStartTime)
        return false;
    if (GetTime() - m_nNetworkActiveStartTime < INACTIVITY_CHECK_GRACE_PERIOD_SECS)
        return true;
    return false;
}

int64_t CNetManagerThread::GetNetworkInactivityTime(const int64_t nCurrentTime) const noexcept
{
    const int64_t nTime = nCurrentTime ? nCurrentTime : GetTime();
    return nTime - m_nNetworkInactiveStartTime;
}
