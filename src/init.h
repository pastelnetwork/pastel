#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>

#include <utils/svc_thread.h>

class CScheduler;
class CWallet;

extern CWallet* pwalletMain;

void StartShutdown();
bool ShutdownRequested();
/** Interrupt threads */
void Interrupt(CServiceThreadGroup& threadGroup, CScheduler &scheduler);
void Shutdown(CServiceThreadGroup& threadGroup, CScheduler &scheduler);
bool AppInit2(CServiceThreadGroup& threadGroup, CScheduler &scheduler);

/** The help message mode determines what help message to show */
enum HelpMessageMode {
    HMM_BITCOIND
};

/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage(HelpMessageMode mode);
