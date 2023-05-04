#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <atomic>
#include <svc_thread.h>

class CNetManagerThread : public CStoppableServiceThread
{
public:
    CNetManagerThread();

    void execute() override;

    bool IsNetworkConnected() const noexcept { return m_bNetworkActive; }
    bool IsNetworkConnectedRecently() const noexcept;
    int64_t GetNetworkInactivityTime(const int64_t nCurrentTime = 0) const noexcept;

private:
    std::atomic_bool m_bNetworkActive { true };
    std::atomic_int64_t m_nNetworkInactiveStartTime;
    std::atomic_int64_t m_nNetworkActiveStartTime;

    std::chrono::seconds checkNetworkConnectivity();
    void NotifyNetworkConnected();
    void NotifyNetworkDisconnected();
};

extern CNetManagerThread gl_NetMgr;
