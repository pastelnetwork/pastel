// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdio>

#include <clientversion.h>
#include <extlibs/scope_guard.hpp>
#include <utils/scheduler.h>
#include <utils/util.h>
#include <rpc/server.h>
#include <init.h>
#include <main.h>
#include <noui.h>
#include <httpserver.h>
#include <httprpc.h>
#ifdef __linux__
#include <execinfo.h>
#include <dlfcn.h>
#endif

using namespace std;

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Bitcoin (https://www.bitcoin.org/),
 * which enables instant payments to anyone, anywhere in the world. Bitcoin uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static bool fDaemon;

void WaitForShutdown(CServiceThreadGroup& threadGroup, CScheduler &scheduler)
{
    bool fShutdown = ShutdownRequested();
    // Tell the main threads to shutdown.
    while (!fShutdown)
    {
        MilliSleep(200);
        fShutdown = ShutdownRequested();
    }
    Interrupt(threadGroup, scheduler);
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[])
{
    CServiceThreadGroup threadGroup;
    CScheduler scheduler("scheduler");

    bool fRet = false;

    //
    // Parameters
    //
    ParseParameters(argc, argv);

    // Process help and version before taking care about datadir
    if (mapArgs.count("-?") || mapArgs.count("-h") ||  mapArgs.count("-help") || mapArgs.count("-version"))
    {
        string strUsage = translate("Pastel Daemon") + " " + translate("version") + " " + FormatFullVersion() + "\n" + PrivacyInfo();

        if (mapArgs.count("-version"))
        {
            strUsage += LicenseInfo();
        }
        else
        {
            strUsage += "\n" + translate("Usage:") + "\n" +
                  "  pasteld [options]                     " + translate("Start Pastel Daemon") + "\n";

            strUsage += "\n" + HelpMessage(HMM_BITCOIND);
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return true;
    }

    try
    {
        if (!fs::is_directory(GetDataDir(false)))
        {
            fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
            return false;
        }
        try
        {
            ReadConfigFile(mapArgs, mapMultiArgs);
        } catch (const missing_pastel_conf& ) {
            fprintf(stderr,
                (translate("Before starting pasteld, you need to create a configuration file:\n"
                   "%s\n"
                   "It can be completely empty! That indicates you are happy with the default\n"
                   "configuration of pasteld. But requiring a configuration file to start ensures\n"
                   "that pasteld won't accidentally compromise your privacy if there was a default\n"
                   "option you needed to change.\n"
                   "\n"
                   "You can look at the example configuration file for suggestions of default\n"
                   "options that you may want to change. It should be in one of these locations,\n"
                   "depending on how you installed Pastel:\n") +
                 translate("- Source code:  %s\n"
                   "- .deb package: %s\n")).c_str(),
                GetConfigFile().string().c_str(),
                "contrib/debian/examples/pastel.conf",
                "/usr/share/doc/pastel/examples/pastel.conf");
            return false;
        } catch (const exception& e) {
            fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            return false;
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        if (!SelectParamsFromCommandLine()) {
            fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
            return false;
        }

        // Command-line RPC
        bool fCommandLine = false;
        for (int i = 1; i < argc; i++)
        {
            if (!IsSwitchChar(argv[i][0]) && !str_istarts_with(argv[i], "pastel:"))
                fCommandLine = true;
        }

        if (fCommandLine)
        {
            fprintf(stderr, "Error: There is no RPC client functionality in pasteld. Use the pastel-cli utility instead.\n");
            exit(EXIT_FAILURE);
        }
#ifndef WIN32
        fDaemon = GetBoolArg("-daemon", false);
        if (fDaemon)
        {
            fprintf(stdout, "Pastel server starting\n");

            // Daemonize
            pid_t pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
                return false;
            }
            if (pid > 0) // Parent process, pid is child process id
            {
                return true;
            }
            // Child process falls through to rest of initialization

            pid_t sid = setsid();
            if (sid < 0)
                fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
        }
#endif
        SoftSetBoolArg("-server", true);

        fRet = AppInit2(threadGroup, scheduler);
    }
    catch (const exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
    }

    if (!fRet)
        Interrupt(threadGroup, scheduler);
    else
        WaitForShutdown(threadGroup, scheduler);
    Shutdown(threadGroup, scheduler);

    return fRet;
}

#ifdef __linux__
void print_callstack()
{
    const int max_frames = 64;
    void* addrlist[max_frames];

    int addrlen = backtrace(addrlist, sizeof(addrlist) / sizeof(void*));

    if (addrlen == 0)
    {
        LogFnPrintf("No stack trace found");
        return;
    }

    char** symbol_list = backtrace_symbols(addrlist, addrlen);
    auto guard = sg::make_scope_guard([&]() noexcept 
    {
        free(symbol_list);
    });

    LogFnPrintf("Stack trace:");
    for (int i = 1; i < addrlen; i++)
    {
        LogFnPrintf("%s", symbol_list[i]);
        Dl_info info;
        if (dladdr(addrlist[i], &info) && info.dli_fbase)
        {
            void* offset = (void*)((uintptr_t)addrlist[i] - (uintptr_t)info.dli_fbase);
            char syscom[256];
            snprintf(syscom, sizeof(syscom), "addr2line -pfC -e %s %p", info.dli_fname, offset);
            system(syscom);
        }
    }
}
#else
void print_callstack()
{
    return;
}
#endif

void pasteld_terminate()
{
    LogFnPrintf("pasteld_terminate():");
    print_callstack();
    exit(1);
}

int main(int argc, char* argv[])
{
    SetupEnvironment();
    set_terminate(pasteld_terminate);

    // Connect bitcoind signal handlers
    noui_connect();

    bool res = false;
    try {
        res = AppInit(argc, argv);
    }
    catch (...) {
        LogFnPrintf("main() exception catch:");
        print_callstack();
    }
    return (res? EXIT_SUCCESS : EXIT_FAILURE);
}
