// Copyright (c) 2016 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#ifdef WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#endif
#include <unistd.h>
#include <cinttypes>
#include <list>
#include <mutex>
#include <iostream>

#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <metrics.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <accept_to_mempool.h>
#include <main.h>
#include <ui_interface.h>
#include <utiltime.h>
#include <utilmoneystr.h>
#include <netmsg/nodemanager.h>

using namespace std;

void AtomicTimer::start()
{
    unique_lock<mutex> lock(mtx);
    if (threads < 1)
        start_time = GetTime();
    ++threads;
}

void AtomicTimer::stop()
{
    unique_lock<mutex> lock(mtx);
    // Ignore excess calls to stop()
    if (threads > 0)
    {
        --threads;
        if (threads < 1)
        {
            int64_t time_span = GetTime() - start_time;
            total_time += time_span;
        }
    }
}

bool AtomicTimer::running()
{
    unique_lock<mutex> lock(mtx);
    return threads > 0;
}

uint64_t AtomicTimer::threadCount()
{
    unique_lock<mutex> lock(mtx);
    return threads;
}

double AtomicTimer::rate(const AtomicCounter& count)
{
    unique_lock<mutex> lock(mtx);
    int64_t duration = total_time;
    if (threads > 0)
    {
        // Timer is running, so get the latest count
        duration += GetTime() - start_time;
    }
    return duration > 0 ? (double)count.get() / duration : 0;
}

static CCriticalSection cs_metrics;

static atomic_uint64_t nNodeStartTime;
static atomic_uint64_t nNextRefresh;
AtomicCounter transactionsValidated;
AtomicCounter ehSolverRuns;
AtomicCounter solutionTargetChecks;
static AtomicCounter minedBlocks;
AtomicTimer miningTimer;

static list<uint256> gl_TrackedBlocks;
static list<string> gl_MessageBox;
static string gl_sInitMessage;
static mutex gl_mtxTrackedBlocks;
static mutex gl_mtxMessageBox;
static mutex gl_mtxInitMessage;

static bool loaded = false;

extern int64_t GetNetworkHashPS(int lookup, int height);

void TrackMinedBlock(const uint256 &hash)
{
    LOCK(cs_metrics);
    minedBlocks.increment();
    {
        lock_guard<mutex> lock(gl_mtxTrackedBlocks);
        gl_TrackedBlocks.push_back(hash);
    }
}

void MarkStartTime()
{
    nNodeStartTime = GetTime();
}

int64_t GetUptime()
{
    return GetTime() - nNodeStartTime;
}

double GetLocalSolPS()
{
    return miningTimer.rate(solutionTargetChecks);
}

int EstimateNetHeightInner(int height, int64_t tipmediantime,
                           int heightLastCheckpoint, int64_t timeLastCheckpoint,
                           int64_t genesisTime, int64_t targetSpacing)
{
    // We average the target spacing with the observed spacing to the last
    // checkpoint (either from below or above depending on the current height),
    // and use that to estimate the current network height.
    const int medianHeight = height > CBlockIndex::nMedianTimeSpan ?
            height - (1 + ((CBlockIndex::nMedianTimeSpan - 1) / 2)) :
            height / 2;
    
    double checkpointSpacing = 0;
    if (medianHeight > heightLastCheckpoint)
        checkpointSpacing = (double (tipmediantime - timeLastCheckpoint)) / (medianHeight - heightLastCheckpoint);
    else if (heightLastCheckpoint != 0)
        checkpointSpacing = (double (timeLastCheckpoint - genesisTime)) / heightLastCheckpoint;
    const double averageSpacing = checkpointSpacing == 0? targetSpacing: (targetSpacing + checkpointSpacing) / 2;
    const int netheight = medianHeight + static_cast<int>((GetTime() - tipmediantime) / averageSpacing);
    // Round to nearest ten to reduce noise
    return ((netheight + 5) / 10) * 10;
}

int EstimateNetHeight(int height, int64_t tipmediantime, CChainParams chainParams)
{
    auto checkpointData = chainParams.Checkpoints();
    return EstimateNetHeightInner(
        height, tipmediantime,
        Checkpoints::GetTotalBlocksEstimate(checkpointData),
        checkpointData.nTimeLastCheckpoint,
        chainParams.GenesisBlock().nTime,
        chainParams.GetConsensus().nPowTargetSpacing);
}

void TriggerRefresh()
{
    nNextRefresh = GetTime();
    // Ensure that the refresh has started before we return
    MilliSleep(200);
}

static bool metrics_ThreadSafeMessageBox(const string& message,
                                      const string& caption,
                                      unsigned int style)
{
    // The SECURE flag has no effect in the metrics UI.
    style &= ~CClientUIInterface::SECURE;

    string strCaption;
    // Check for usage of predefined caption
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strCaption += translate("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strCaption += translate("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strCaption += translate("Information");
        break;
    default:
        strCaption += caption; // Use supplied caption (can be empty)
    }

    {
        lock_guard<mutex> lock(gl_mtxMessageBox);
        gl_MessageBox.push_back(strCaption + ": " + message);
        if (gl_MessageBox.size() > 5)
			gl_MessageBox.pop_front();
    }

    TriggerRefresh();
    return false;
}

static bool metrics_ThreadSafeQuestion(const string& /* ignored interactive message */, const string& message, const string& caption, unsigned int style)
{
    return metrics_ThreadSafeMessageBox(message, caption, style);
}

static void metrics_InitMessage(const string& sMessage)
{
    lock_guard<mutex> lock(gl_mtxInitMessage);
    gl_sInitMessage = sMessage;
}

void ConnectMetricsScreen()
{
    uiInterface.ThreadSafeMessageBox.disconnect_all_slots();
    uiInterface.ThreadSafeMessageBox.connect(metrics_ThreadSafeMessageBox);
    uiInterface.ThreadSafeQuestion.disconnect_all_slots();
    uiInterface.ThreadSafeQuestion.connect(metrics_ThreadSafeQuestion);
    uiInterface.InitMessage.disconnect_all_slots();
    uiInterface.InitMessage.connect(metrics_InitMessage);
}

int printStats(bool mining)
{
    // Number of lines that are always displayed
    int lines = 4;

    uint32_t nHeight = gl_nChainHeight;
    int64_t tipmediantime;
    size_t nNodeCount = gl_NodeManager.GetNodeCount();
    int64_t netsolps;
    {
        LOCK(cs_main);
        tipmediantime = chainActive.Tip()->GetMedianTimePast();
        netsolps = GetNetworkHashPS(120, -1);
    }
    auto localsolps = GetLocalSolPS();

    const auto& consensusParams = Params().GetConsensus();
    if (fnIsInitialBlockDownload(consensusParams))
    {
        int netheight = EstimateNetHeight(nHeight, tipmediantime, Params());
        int downloadPercent = netheight == 0? netheight: nHeight * 100 / netheight;
        cout << "     " << translate("Downloading blocks") << " | " << nHeight << " / ~" << netheight << " (" << downloadPercent << "%)" << endl;
    } else
        cout << "           " << translate("Block height") << " | " << nHeight << endl;
    
    cout << "            " << translate("Connections") << " | " << nNodeCount << endl;
    cout << "  " << translate("Network solution rate") << " | " << netsolps << " Sol/s" << endl;
    if (mining && miningTimer.running())
    {
        cout << "    " << translate("Local solution rate") << " | " << strprintf("%.4f Sol/s", localsolps) << endl;
        lines++;
    }
    cout << endl;

    return lines;
}

int printMiningStatus(bool mining)
{
#ifdef ENABLE_MINING
    // Number of lines that are always displayed
    int lines = 1;

    if (mining)
    {
        const auto nThreads = miningTimer.threadCount();
        const auto& consensusParams = Params().GetConsensus();
        if (nThreads > 0)
            cout << strprintf(translate("You are mining with the %s solver on %d threads."),
                                   GetArg("-equihashsolver", "default"), nThreads) << endl;
        else
        {
            bool fvNodesEmpty = gl_NodeManager.GetNodeCount() == 0;
            if (fvNodesEmpty)
                cout << translate("Mining is paused while waiting for connections.") << endl;
            else if (fnIsInitialBlockDownload(consensusParams))
                cout << translate("Mining is paused while downloading blocks.") << endl;
            else
                cout << translate("Mining is paused (a JoinSplit may be in progress).") << endl;
        }
        lines++;
    } else {
        cout << translate("You are currently not mining.") << endl;
        cout << translate("To enable mining, add 'gen=1' to your pastel.conf and restart.") << endl;
        lines += 2;
    }
    cout << endl;

    return lines;
#else // ENABLE_MINING
    return 0;
#endif // !ENABLE_MINING
}

int printMetrics(size_t cols, bool mining)
{
    // Number of lines that are always displayed
    int lines = 3;

    // Calculate uptime
    const int64_t uptime = GetUptime();
    const int days = static_cast<int>(uptime / (24 * 60 * 60));
    const int hours = static_cast<int>((uptime - (days * 24 * 60 * 60)) / (60 * 60));
    const int minutes = static_cast<int>((uptime - (((days * 24) + hours) * 60 * 60)) / 60);
    const int seconds = static_cast<int>(uptime - (((((days * 24) + hours) * 60) + minutes) * 60));

    // Display uptime
    string duration;
    if (days > 0)
        duration = strprintf(translate("%d days, %d hours, %d minutes, %d seconds"), days, hours, minutes, seconds);
    else if (hours > 0)
        duration = strprintf(translate("%d hours, %d minutes, %d seconds"), hours, minutes, seconds);
    else if (minutes > 0)
        duration = strprintf(translate("%d minutes, %d seconds"), minutes, seconds);
    else
        duration = strprintf(translate("%d seconds"), seconds);

    string strDuration = strprintf(translate("Since starting this node %s ago:"), duration);
    cout << strDuration << endl;
    lines += static_cast<int>(strDuration.size() / cols);

    const auto validatedCount = transactionsValidated.get();
    if (validatedCount > 1)
      cout << "- " << strprintf(translate("You have validated %d transactions!"), validatedCount) << endl;
    else if (validatedCount == 1)
      cout << "- " << translate("You have validated a transaction!") << endl;
    else
      cout << "- " << translate("You have validated no transactions.") << endl;

    if (mining && loaded)
    {
        cout << "- " << strprintf(translate("You have completed %d Equihash solver runs."), ehSolverRuns.get()) << endl;
        lines++;

        uint64_t mined = 0;
        uint64_t orphaned = 0;
        CAmount immature {0};
        CAmount mature {0};
        {
            LOCK2(cs_main, cs_metrics);
            lock_guard<mutex> lock(gl_mtxTrackedBlocks);
            const auto &consensusParams = Params().GetConsensus();
            auto tipHeight = chainActive.Height();

            // Update orphans and calculate subsidies
            for (auto it = gl_TrackedBlocks.begin(); it != gl_TrackedBlocks.end(); )
            {
                const auto &hash = *it;
                if (mapBlockIndex.count(hash) > 0 &&
                        chainActive.Contains(mapBlockIndex[hash]))
                {
					int height = mapBlockIndex[hash]->nHeight;
					CAmount subsidy = GetBlockSubsidy(height, consensusParams);
					if (max(0, COINBASE_MATURITY - (tipHeight - height)) > 0)
						immature += subsidy;
					else
						mature += subsidy;
					it++;
				} else
					it = gl_TrackedBlocks.erase(it);
            }

            mined = minedBlocks.get();
            orphaned = mined - gl_TrackedBlocks.size();
        }

        if (mined > 0)
        {
            string units = Params().CurrencyUnits();
            cout << "- " << strprintf(translate("You have mined %" PRIu64 " blocks!"), mined) << endl;
            cout << "  "
                      << strprintf(translate("Orphaned: %" PRIu64 " blocks, Immature: %u %s, Mature: %u %s"),
                                     orphaned,
                                     FormatMoney(immature), units,
                                     FormatMoney(mature), units)
                      << endl;
            lines += 2;
        }
    }
    cout << endl;

    return lines;
}

int printMessageBox(size_t cols)
{
    lock_guard<mutex> lock(gl_mtxMessageBox);
    if (gl_MessageBox.empty())
        return 0;

    int lines = static_cast<int>(2 + gl_MessageBox.size());
    cout << translate("Messages:") << endl;
    for (const auto& msg : gl_MessageBox)
    {
        auto sMsg = FormatParagraph(msg, cols, 2);
        cout << "- " << sMsg << endl;
        // Handle newlines and wrapped lines
        size_t i = 0;
        size_t j = 0;
        while (j < sMsg.size())
        {
            i = sMsg.find('\n', j);
            if (i == string::npos)
                i = sMsg.size();
            else // Newline
                lines++;
            j = i + 1;
        }
    }
    cout << endl;
    return lines;
}

int printInitMessage()
{
    if (loaded)
        return 0;

    string sMsg;
    {
        lock_guard<mutex> lock(gl_mtxInitMessage);
        sMsg = gl_sInitMessage;
    }
    cout << translate("Init message:") << " " << sMsg << endl;
    cout << endl;

    if (sMsg == translate("Done loading"))
        loaded = true;

    return 2;
}

#ifdef WIN32

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

bool enableVTMode()
{
    // Set output mode to handle virtual terminal sequences
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) {
        return false;
    }

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode)) {
        return false;
    }
    return true;
}
#endif

void SendVTSequence(const char *szVTcmd)
{
    if (!szVTcmd)
        return;
    string s;
#ifdef WIN32
    s = "\x1b[";
#else
    s = "\e[";
#endif
    s += szVTcmd;
    cout << s;
}

void ThreadShowMetricsScreen()
{
    // Make this thread recognizable as the metrics screen thread

    // Determine whether we should render a persistent UI or rolling metrics
    bool isTTY = isatty(STDOUT_FILENO);
    bool isScreen = GetBoolArg("-metricsui", isTTY);
    int64_t nRefresh = GetArg("-metricsrefreshtime", isTTY ? 1 : 600);

    if (isScreen) {
#ifdef WIN32
        enableVTMode();
#endif

        // Clear screen
        SendVTSequence("2J");

        // Print art
        // cout << METRICS_ART << endl;
        // cout << endl;

        // Thank you text
        cout << translate("Thank you for running a Pastel node!") << endl;
        cout << translate("You're helping to strengthen the network and contributing to a social good :)") << endl;

        // Privacy notice text
        cout << PrivacyInfo();
        cout << endl;
    }

    while (true)
    {
        // Number of lines that are always displayed
        int lines = 1;
        int cols = 80;

        // Get current window size
        if (isTTY) {
#ifdef WIN32
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi) != 0)
                cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
            struct winsize w;
            w.ws_col = 0;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1 && w.ws_col != 0)
                cols = w.ws_col;
#endif
        }

        // Erase below current position
        if (isScreen)
            SendVTSequence("J");

        // Miner status
#ifdef ENABLE_MINING
        bool mining = GetBoolArg("-gen", false);
#else
        bool mining = false;
#endif

        if (loaded)
        {
            lines += printStats(mining);
            lines += printMiningStatus(mining);
        }
        lines += printMetrics(cols, mining);
        lines += printMessageBox(cols);
        lines += printInitMessage();

        if (isScreen) {
            // Explain how to exit
            cout << "[";
#ifdef WIN32
            cout << translate("'pascal-cli.exe stop' to exit");
#else
            cout << translate("Press Ctrl+C to exit");
#endif
            cout << "] [" << translate("Set 'showmetrics=0' to hide") << "]" << endl;
        } else {
            // Print delineator
            cout << "----------------------------------------" << endl;
        }

        nNextRefresh = GetTime() + nRefresh;
        while (static_cast<uint64_t>(GetTime()) < nNextRefresh)
        {
            func_thread_interrupt_point();
            MilliSleep(200);
        }

        // Return to the top of the updating section
        if (isScreen)
            SendVTSequence(tfm::format("%dA", lines).c_str());
    }
}

void ClearMetrics()
{
    transactionsValidated.set(0);
    ehSolverRuns.set(0);
    solutionTargetChecks.set(0);
    minedBlocks.set(0);
    nNodeStartTime = 0;
    nNextRefresh = 0;
    loaded = false;
}
