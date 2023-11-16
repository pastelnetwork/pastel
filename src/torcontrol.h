#pragma once
// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#include <utils/scheduler.h>

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

class CTorControlThread : public CServiceThread
{
public:
    CTorControlThread() : 
        CServiceThread("torcontrol"),
        m_eventBase(nullptr)
    {}

    ~CTorControlThread() override;

    void stop() override;
    void execute() override;

private:
    struct event_base* m_eventBase;
};