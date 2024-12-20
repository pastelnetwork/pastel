// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/pastel-config.h>
#endif

#include <cstdint>
#include <cstdio>

#include <unistd.h>
#ifndef WIN32
#include <signal.h>
#endif

#include <libsnark/common/profiling.hpp>
#include <openssl/opensslv.h>
#include <boost/interprocess/sync/file_lock.hpp>

#if ENABLE_ZMQ
#include <zmq/zmqnotificationinterface.h>
#endif

#if ENABLE_PROTON
#include <amqp/amqpnotificationinterface.h>
#endif
#include <librustzcash.h>

#include <config/port_config.h>
#include <utils/str_utils.h>
#include <utils/scheduler.h>
#include <utils/util.h>
#include <init.h>
#include <chain_options.h>
#include <crypto/common.h>
#include <addrman.h>
#include <amount.h>
#include <checkpoints.h>
#include <compat/sanity.h>
#include <consensus/upgrades.h>
#include <consensus/validation.h>
#include <httpserver.h>
#include <httprpc.h>
#include <key.h>
#include <accept_to_mempool.h>
#include <main.h>
#include <metrics.h>
#include <mining/miner.h>
#include <mining/mining-settings.h>
#include <net.h>
#include <rpc/server.h>
#include <rpc/register.h>
#include <script/standard.h>
#include <key_io.h>
#include <script/sigcache.h>
#include <txdb/txdb.h>
#include <torcontrol.h>
#include <ui_interface.h>
#include <utilmoneystr.h>
#include <validationinterface.h>
#include <experimental_features.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <mnode/mnode-controller.h>
#include <script_check.h>
#include <orphan-tx.h>
#include <netmsg/netconsts.h>
#include <netmsg/nodemanager.h>

using namespace std;

//MasterNode
CMasterNodeController masterNodeCtrl;

extern void ThreadSendAlert();

#ifdef ENABLE_WALLET
CWallet* pwalletMain = nullptr;
#endif
bool fFeeEstimatesInitialized = false;

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = nullptr;
#endif

#if ENABLE_PROTON
static AMQPNotificationInterface* pAMQPNotificationInterface = nullptr;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use file descriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
constexpr uint32_t MIN_CORE_FILEDESCRIPTORS = 0;
#else
constexpr uint32_t MIN_CORE_FILEDESCRIPTORS = 150;
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE         = 0,
    BF_EXPLICIT     = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST    = (1U << 2),
};

constexpr auto FEE_ESTIMATES_FILENAME = "fee_estimates.dat";
CClientUIInterface uiInterface; // Declared but not defined in ui_interface.h

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit().
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//

atomic_bool fRequestShutdown(false);
static condition_variable gl_cvShutdown;
static mutex gl_csShutdown;

void StartShutdown()
{
    unique_lock lock(gl_csShutdown);
    fRequestShutdown = true;
    LogFnPrintf("Shutdown requested");
	gl_cvShutdown.notify_all();
}

bool IsShutdownRequested()
{
    return fRequestShutdown;
}

void WaitForShutdown(CServiceThreadGroup& threadGroup, CScheduler &scheduler)
{
    unique_lock lock(gl_csShutdown);
    gl_cvShutdown.wait(lock, [] { return IsShutdownRequested(); });

    LogFnPrintf("Shutdown signal received, exiting...");
    Interrupt(threadGroup, scheduler);
}

/** Abort with a message */
bool AbortNode(const string& strMessage, const string& userMessage)
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? translate("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const string& strMessage, const string& userMessage)
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

class CCoinsViewErrorCatcher : public CCoinsViewBacked
{
public:
    CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoins(const uint256 &txid, CCoins &coins) const {
        try {
            return CCoinsViewBacked::GetCoins(txid, coins);
        } catch(const runtime_error& e) {
            uiInterface.ThreadSafeMessageBox(translate("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
            LogPrintf("Error reading from database: %s\n", e.what());
            // Starting the shutdown sequence and returning false to the caller would be
            // interpreted as 'entry not found' (as opposed to unable to read data), and
            // could lead to invalid interpretation. Just exit immediately, as we can't
            // continue anyway, and all writes should be atomic.
            abort();
        }
    }
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

class CLogRotationManager : public CStoppableServiceThread
{
public:
    CLogRotationManager() :
        CStoppableServiceThread("logrt")
    {}
    static constexpr auto LOG_ROTATION_INTERVAL = chrono::minutes(10);

    void execute() override
    {
        // check if debug log needs to be rotated every 10 minutes
        while (!shouldStop())
        {
            unique_lock lock(m_mutex);
            if (m_condVar.wait_for(lock, LOG_ROTATION_INTERVAL) == cv_status::timeout)
            {
                if (gl_LogMgr)
                    gl_LogMgr->ShrinkDebugLogFile();
            }
        }
    }
};

static unique_ptr<CCoinsViewDB> gl_pCoinsDbView;
static unique_ptr<CCoinsViewErrorCatcher> pCoinsCatcher;
static shared_ptr<CLogRotationManager> gl_LogRotationManager;

void Interrupt(CServiceThreadGroup& threadGroup, CScheduler &scheduler)
{
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    LogFnPrintf("Stopping Pastel threads...");
    threadGroup.stop_all();
    LogFnPrintf("Stopping scheduler threads...");
    scheduler.stop(false);
    if (gl_LogRotationManager)
        gl_LogRotationManager->stop();
}

void Shutdown(CServiceThreadGroup& threadGroup, CScheduler &scheduler)
{
    LogFnPrintf("In progress...");
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    RenameThread("psl-shutoff");
    mempool.AddTransactionsUpdated(1);

    if (gl_LogRotationManager)
        gl_LogRotationManager->waitForStop();

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(false);
#endif
#ifdef ENABLE_MINING
 #ifdef ENABLE_WALLET
    GenerateBitcoins(false, nullptr, Params());
 #else
    GenerateBitcoins(false, Params());
 #endif
#endif
    StopNode();
    LogFnPrintf("Waiting for Pastel threads to exit...");
    threadGroup.join_all();
    LogFnPrintf("...done");
    LogFnPrintf("Waiting for scheduler threads to exit...");
    scheduler.join_all();
    LogFnPrintf("...done");
    UnregisterNodeSignals(GetNodeSignals());

    if (fFeeEstimatesInitialized)
    {
        fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        FILE* fp = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        const errno_t err = fopen_s(&fp, est_path.string().c_str(), "wb");
#else
        fp = fopen(est_path.string().c_str(), "wb");
#endif
        CAutoFile est_fileout(fp, SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            mempool.WriteFeeEstimates(est_fileout);
        else
            LogFnPrintf("Failed to write fee estimates to %s", est_path.string());
        fFeeEstimatesInitialized = false;
    }

    {
        LOCK(cs_main);
        if (gl_pCoinsTip)
            FlushStateToDisk();
        gl_pCoinsTip.reset();
        pCoinsCatcher.reset();
        gl_pCoinsDbView.reset();
        gl_pBlockTreeDB.reset();
    }
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(true);
#endif

    masterNodeCtrl.ShutdownMasterNode();

#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        UnregisterValidationInterface(pzmqNotificationInterface);
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = nullptr;
    }
#endif

#if ENABLE_PROTON
    if (pAMQPNotificationInterface) {
        UnregisterValidationInterface(pAMQPNotificationInterface);
        delete pAMQPNotificationInterface;
        pAMQPNotificationInterface = nullptr;
    }
#endif

#ifndef WIN32
    try {
        fs::remove(GetPidFile());
    } catch (const fs::filesystem_error& e) {
        LogFnPrintf("Unable to remove pidfile: %s", e.what());
    }
#endif
    UnregisterAllValidationInterfaces();
#ifdef ENABLE_WALLET
    safe_delete_obj(pwalletMain);
#endif
    LogFnPrintf("done");
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int)
{
    StartShutdown();
}

void HandleSIGHUP(int)
{
    if (gl_LogMgr)
        gl_LogMgr->ScheduleReopenDebugLog();
}

bool static InitError(const string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

bool static Bind(const CService &addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && IsLimited(addr))
        return false;
    string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

void OnRPCStopped()
{
    cvBlockChange.notify_all();
    LogPrint("rpc", "RPC stopped.\n");
}

void OnRPCPreCommand(const CRPCCommand& cmd)
{
    // Observe safe mode
    string strWarning = GetWarnings("rpc");
    if (strWarning != "" && !GetBoolArg("-disablesafemode", false) &&
        !cmd.okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);
}

string HelpMessage(HelpMessageMode mode)
{
    const bool showDebug = GetBoolArg("-help-debug", false);

    // When adding new options to the categories, please keep and ensure alphabetical ordering.
    // Do not translate _(...) -help-debug options, many technical terms, and only a very small audience, so is unnecessary stress to translators

    string strUsage = HelpMessageGroup(translate("Options:"));
    strUsage += HelpMessageOpt("-?", translate("This help message"));
    strUsage += HelpMessageOpt("-alerts", strprintf(translate("Receive and display P2P network alerts (default: %u)"), DEFAULT_ALERTS));
    strUsage += HelpMessageOpt("-alertnotify=<cmd>", translate("Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)"));
    strUsage += HelpMessageOpt("-blocknotify=<cmd>", translate("Execute command when the best block changes (%s in cmd is replaced by block hash)"));
    strUsage += HelpMessageOpt("-checkblocks=<n>", strprintf(translate("How many blocks to check at startup (default: %u, 0 = all)"), DEFAULT_BLOCKDB_CHECKBLOCKS));
    strUsage += HelpMessageOpt("-checklevel=<n>", strprintf(translate("How thorough the block verification of -checkblocks is (0-4, default: %u)"), DEFAULT_BLOCKDB_CHECKLEVEL));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(translate("Specify configuration file (default: %s)"), "pastel.conf"));
    if (mode == HMM_BITCOIND) //-V547
    {
#if !defined(WIN32)
        strUsage += HelpMessageOpt("-daemon", translate("Run in the background as a daemon and accept commands"));
#endif
    }
    strUsage += HelpMessageOpt("-datadir=<dir>", translate("Specify data directory"));
    strUsage += HelpMessageOpt("-exportdir=<dir>", translate("Specify directory to be used when exporting data"));
    strUsage += HelpMessageOpt("-dbcache=<n>", strprintf(translate("Set database cache size in megabytes (%d to %d, default: %d)"), nMinDbCache, nMaxDbCache, nDefaultDbCache));
    strUsage += HelpMessageOpt("-loadblock=<file>", translate("Imports blocks from external blk000??.dat file") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-maxorphantx=<n>", strprintf(translate("Keep at most <n> unconnectable transactions in memory (default: %zu)"), DEFAULT_MAX_ORPHAN_TRANSACTIONS));
    strUsage += HelpMessageOpt("-par=<n>", strprintf(translate("Set the number of script verification threads (-%u to %zu, 0 = auto, <0 = leave that many cores free, default: %zu)"),
        GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS));
#ifndef WIN32
    strUsage += HelpMessageOpt("-pid=<file>", strprintf(translate("Specify pid file (default: %s)"), "pasteld.pid"));
#endif
    strUsage += HelpMessageOpt("-prune=<n>", strprintf(translate("Reduce storage requirements by pruning (deleting) old blocks. This mode disables wallet support and is incompatible with -txindex. "
            "Warning: Reverting this setting requires re-downloading the entire blockchain. "
            "(default: 0 = disable pruning blocks, >%u = target size in MiB to use for block files)"), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
    strUsage += HelpMessageOpt("-reindex", translate("Rebuild block chain index from current blk000??.dat files on startup"));
#if !defined(WIN32)
    strUsage += HelpMessageOpt("-sysperms", translate("Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)"));
#endif
    strUsage += HelpMessageOpt("-txindex", strprintf(translate("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)"), 0));
    strUsage += HelpMessageOpt("-rewindchain=<block_hash>", translate("Rewind chain to specified block hash"));
    strUsage += HelpMessageOpt("-repairticketdb", translate("Repair ticket database from the blockchain"));

    strUsage += HelpMessageGroup(translate("Connection options:"));
    strUsage += HelpMessageOpt("-addnode=<ip>", translate("Add a node to connect to and attempt to keep the connection open"));
    strUsage += HelpMessageOpt("-banscore=<n>", strprintf(translate("Threshold for disconnecting misbehaving peers (default: %u)"), 100));
    strUsage += HelpMessageOpt("-bantime=<n>", strprintf(translate("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), 86400));
    strUsage += HelpMessageOpt("-bind=<addr>", translate("Bind to given address and always listen on it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-connect=<ip>", translate("Connect only to the specified node(s)"));
    strUsage += HelpMessageOpt("-discover", translate("Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)"));
    strUsage += HelpMessageOpt("-dns", translate("Allow DNS lookups for -addnode, -seednode and -connect") + " " + translate("(default: 1)"));
    strUsage += HelpMessageOpt("-dnsseed", translate("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)"));
    strUsage += HelpMessageOpt("-externalip=<ip>", translate("Specify your own public address"));
    strUsage += HelpMessageOpt("-forcednsseed", strprintf(translate("Always query for peer addresses via DNS lookup (default: %u)"), 0));
    strUsage += HelpMessageOpt("-listen", translate("Accept connections from outside (default: 1 if no -proxy or -connect)"));
    strUsage += HelpMessageOpt("-listenonion", strprintf(translate("Automatically create Tor hidden service (default: %d)"), DEFAULT_LISTEN_ONION));
    strUsage += HelpMessageOpt("-maxconnections=<n>", strprintf(translate("Maintain at most <n> connections to peers (default: %u)"), DEFAULT_MAX_PEER_CONNECTIONS));
    strUsage += HelpMessageOpt("-fdsoftlimit=<n>", strprintf(translate("Set the file descriptor soft limit to <n> (default: %u)"), DEFAULT_FD_SOFT_LIMIT));
    strUsage += HelpMessageOpt("-maxreceivebuffer=<n>", strprintf(translate("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), 5000));
    strUsage += HelpMessageOpt("-maxsendbuffer=<n>", strprintf(translate("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), 1000));
    strUsage += HelpMessageOpt("-onion=<ip:port>", strprintf(translate("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"));
    strUsage += HelpMessageOpt("-onlynet=<net>", translate("Only connect to nodes in network <net> (ipv4, ipv6 or onion)"));
    strUsage += HelpMessageOpt("-permitbaremultisig", strprintf(translate("Relay non-P2SH multisig (default: %u)"), 1));
    strUsage += HelpMessageOpt("-port=<port>", strprintf(translate("Listen for connections on <port> (default: %u or testnet: %u or devnet: %u)"), MAINNET_DEFAULT_PORT, TESTNET_DEFAULT_PORT, DEVNET_DEFAULT_PORT));
    strUsage += HelpMessageOpt("-peerbloomfilters", strprintf(translate("Support filtering of blocks and transaction with Bloom filters (default: %u)"), 1));
    if (showDebug)
        strUsage += HelpMessageOpt("-enforcenodebloom", strprintf("Enforce minimum protocol version to limit use of Bloom filters (default: %u)", 0));
    strUsage += HelpMessageOpt("-proxy=<ip:port>", translate("Connect through SOCKS5 proxy"));
    strUsage += HelpMessageOpt("-proxyrandomize", strprintf(translate("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"), 1));
    strUsage += HelpMessageOpt("-seednode=<ip>", translate("Connect to a node to retrieve peer addresses, and disconnect"));
    strUsage += HelpMessageOpt("-timeout=<n>", strprintf(translate("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT));
    strUsage += HelpMessageOpt("-torcontrol=<ip>:<port>", strprintf(translate("Tor control port to use if onion listening enabled (default: %s)"), DEFAULT_TOR_CONTROL));
    strUsage += HelpMessageOpt("-torpassword=<pass>", translate("Tor control port password (default: empty)"));
    strUsage += HelpMessageOpt("-whitebind=<addr>", translate("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-whitelist=<netmask>", translate("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") +
        " " + translate("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"));

#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(translate("Wallet options:"));
    strUsage += HelpMessageOpt("-disablewallet", translate("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(translate("Set key pool size to <n> (default: %u)"), 100));
    if (showDebug)
        strUsage += HelpMessageOpt("-mintxfee=<amt>", strprintf("Fees (in %s/kB) smaller than this are considered zero fee for transaction creation (default: %s)",
            CURRENCY_UNIT, FormatMoney(CWallet::minTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-paytxfee=<amt>", strprintf(translate("Fee (in %s/kB) to add to transactions you send (default: %s)"),
        CURRENCY_UNIT, FormatMoney(payTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-rescan", translate("Rescan the block chain for missing wallet transactions") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", translate("Attempt to recover private keys from a corrupt wallet.dat") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions", strprintf(translate("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(translate("Spend unconfirmed change when sending transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(translate("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), DEFAULT_TX_CONFIRM_TARGET));
    strUsage += HelpMessageOpt("-txexpirydelta", strprintf(translate("Set the number of blocks after which a transaction that has not been mined will become invalid (min: %u, default: %u)"), TX_EXPIRING_SOON_THRESHOLD + 1, DEFAULT_TX_EXPIRY_DELTA));
    strUsage += HelpMessageOpt("-maxtxfee=<amt>", strprintf(translate("Maximum total fees (in %s) to use in a single wallet transaction; setting this too low may abort large transactions (default: %s)"),
        CURRENCY_UNIT, FormatMoney(maxTxFee)));
    strUsage += HelpMessageOpt("-upgradewallet", translate("Upgrade wallet to latest format") + " " + translate("on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", translate("Specify wallet file (within data directory)") + " " + strprintf(translate("(default: %s)"), "wallet.dat"));
    strUsage += HelpMessageOpt("-walletbroadcast", translate("Make the wallet broadcast transactions") + " " + strprintf(translate("(default: %u)"), true));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", translate("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    strUsage += HelpMessageOpt("-zapwallettxes=<mode>", translate("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
        " " + translate("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(translate("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", translate("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", translate("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", translate("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", translate("Enable publish raw transaction in <address>"));
#endif

#if ENABLE_PROTON
    strUsage += HelpMessageGroup(translate("AMQP 1.0 notification options:"));
    strUsage += HelpMessageOpt("-amqppubhashblock=<address>", translate("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-amqppubhashtx=<address>", translate("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-amqppubrawblock=<address>", translate("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-amqppubrawtx=<address>", translate("Enable publish raw transaction in <address>"));
#endif

    strUsage += HelpMessageGroup(translate("Debugging/Testing options:"));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-checkpoints", strprintf("Disable expensive verification for known chain history (default: %u)", 1));
        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)", 100));
        strUsage += HelpMessageOpt("-disablesafemode", strprintf("Disable safemode, override a real safe mode event (default: %u)", 0));
        strUsage += HelpMessageOpt("-testsafemode", strprintf("Force safe mode (default: %u)", 0));
        strUsage += HelpMessageOpt("-dropmessagestest=<n>", "Randomly drop 1 of every <n> network messages");
        strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", "Randomly fuzz 1 of every <n> network messages");
        strUsage += HelpMessageOpt("-flushwallet", strprintf("Run a thread to flush wallet periodically (default: %u)", 1));
        strUsage += HelpMessageOpt("-stopafterblockimport", strprintf("Stop running after importing blocks from disk (default: %u)", 0));
        strUsage += HelpMessageOpt("-nuparams=hexBranchId:activationHeight", "Use given activation height for specified network upgrade (regtest-only)");
    }
    string debugCategories = "addrman, alert, bench, coindb, compress, db, estimatefee, http, libevent, lock, mempool, mining, net, partitioncheck, pow, proxy, prune, "
                             "rand, reindex, rpc, selectcoins, tor, txdb, wallet, zmq, zrpc, zrpcunsafe (implies zrpc)"; // Don't translate these
    strUsage += HelpMessageOpt("-debug=<category>,...", strprintf(translate("Output debugging information (default: %u, supplying <category> is optional)"), 0) + ". " +
        translate("If <category> is not supplied or if <category> = 1, output all debugging information.") + " " + translate("<category> can be:") + " " + debugCategories + ".");
    strUsage += HelpMessageOpt("-experimentalfeatures", translate("Enable use of experimental features"));
    strUsage += HelpMessageOpt("-help-debug", translate("Show all debugging options (usage: --help -help-debug)"));
    strUsage += HelpMessageOpt("-logips", strprintf(translate("Include IP addresses in debug output (default: %u)"), 0));
    strUsage += HelpMessageOpt("-logtimestamps", strprintf(translate("Prepend debug output with timestamp (default: %u)"), 1));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-limitfreerelay=<n>", strprintf("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default: %u)", 15));
        strUsage += HelpMessageOpt("-relaypriority", strprintf("Require high priority for relaying free or low-fee transactions (default: %u)", 0));
        strUsage += HelpMessageOpt("-maxsigcachesize=<n>", strprintf("Limit size of signature cache to <n> MiB (default: %u)", DEFAULT_MAX_SIG_CACHE_SIZE));
        strUsage += HelpMessageOpt("-maxtipage=<n>", strprintf("Maximum tip age in seconds to consider node in initial block download (default: %u)", DEFAULT_MAX_TIP_AGE));
    }
    strUsage += HelpMessageOpt("-minrelaytxfee=<amt>", strprintf(translate("Fees (in %s/kB) smaller than this are considered zero fee for relaying (default: %s)"),
        CURRENCY_UNIT, FormatMoney(gl_ChainOptions.minRelayTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-printtoconsole=<n>", translate("Set print-to-console mode (0-debug.log file only (default), 1-print only to console, 2-print to both console and debug.log"));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-printpriority", strprintf("Log transaction priority and fee per kB when mining blocks (default: %u)", 0));
        strUsage += HelpMessageOpt("-privdb", strprintf("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)", 1));
        strUsage += HelpMessageOpt("-regtest", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
            "This is intended for regression testing tools and app development.");
    }
    strUsage += HelpMessageOpt("-shrinkdebugfile", translate("Shrink debug.log file on client startup (default: 1 when no -debug)"));
    strUsage += HelpMessageOpt("-testnet", translate("Use the test network"));
    strUsage += HelpMessageOpt("-devnet", translate("Use the devnet network"));

    strUsage += HelpMessageGroup(translate("Node relay options:"));
    strUsage += HelpMessageOpt("-datacarrier", strprintf(translate("Relay and mine data carrier transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-datacarriersize", strprintf(translate("Maximum size of data in data carrier transactions we relay and mine (default: %u)"), MAX_OP_RETURN_RELAY));

    strUsage += HelpMessageGroup(translate("Block creation options:"));
    strUsage += HelpMessageOpt("-blockminsize=<n>", strprintf(translate("Set minimum block size in bytes (default: %u)"), 0));
    strUsage += HelpMessageOpt("-blockmaxsize=<n>", strprintf(translate("Set maximum block size in bytes (default: %d)"), DEFAULT_BLOCK_MAX_SIZE));
    strUsage += HelpMessageOpt("-blockprioritysize=<n>", strprintf(translate("Set maximum size of high-priority/low-fee transactions in bytes (default: %d)"), DEFAULT_BLOCK_PRIORITY_SIZE));
    if (GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-blockversion=<n>", strprintf("Override block version to test forking scenarios (default: %d)", CBlock::CURRENT_VERSION));

#ifdef ENABLE_MINING
    strUsage += HelpMessageGroup(translate("Mining options:"));
    strUsage += HelpMessageOpt("-gen", strprintf(translate("Generate coins (default: %u)"), 0));
    strUsage += HelpMessageOpt("-genproclimit=<n>", strprintf(translate("Set the number of threads for coin generation if enabled (-1 = all cores, default: %d)"), 1));
    strUsage += HelpMessageOpt("-gensleepmsecs=<n>", strprintf(translate("Set the number of milliseconds to sleep for miner threads (default: %u)"), DEFAULT_MINER_SLEEP_MSECS));
    strUsage += HelpMessageOpt("-equihashsolver=<name>", translate("Specify the Equihash solver to be used if enabled (default: \"default\")"));
    strUsage += HelpMessageOpt("-mineraddress=<addr>", translate("Send mined coins to a specific single address"));
    strUsage += HelpMessageOpt("-minetolocalwallet", strprintf(
            translate("Require that mined blocks use a coinbase address in the local wallet (default: %u)"),
 #ifdef ENABLE_WALLET
            1
 #else
            0
 #endif
            ));
#endif

    strUsage += HelpMessageGroup(translate("RPC server options:"));
    strUsage += HelpMessageOpt("-server", translate("Accept command line and JSON-RPC commands"));
    strUsage += HelpMessageOpt("-rest", strprintf(translate("Accept public REST requests (default: %u)"), 0));
    strUsage += HelpMessageOpt("-rpcbind=<addr>", translate("Bind to given address to listen for JSON-RPC connections. Use [host]:port notation for IPv6. This option can be specified multiple times (default: bind to all interfaces)"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", translate("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", translate("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(translate("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u or devnet: %u)"), MAINNET_DEFAULT_RPC_PORT, TESTNET_DEFAULT_RPC_PORT, DEVNET_DEFAULT_RPC_PORT));
    strUsage += HelpMessageOpt("-rpcallowip=<ip>", translate("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times"));
    strUsage += HelpMessageOpt("-rpcthreads=<n>", strprintf(translate("Set the number of threads to service RPC calls (default: %d)"), DEFAULT_HTTP_THREADS));
    if (showDebug) {
        strUsage += HelpMessageOpt("-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE));
        strUsage += HelpMessageOpt("-rpcservertimeout=<n>", strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT));
    }

    // Disabled until we can lock notes and also tune performance of libsnark which by default uses multiple threads
    //strUsage += HelpMessageOpt("-rpcasyncthreads=<n>", strprintf(translate("Set the number of threads to service Async RPC calls (default: %d)"), 1));

    if (mode == HMM_BITCOIND) { //-V547
        strUsage += HelpMessageGroup(translate("Metrics Options (only if -daemon and -printtoconsole are not set):"));
        strUsage += HelpMessageOpt("-showmetrics", translate("Show metrics on stdout (default: 1 if running in a console, 0 otherwise)"));
        strUsage += HelpMessageOpt("-metricsui", translate("Set to 1 for a persistent metrics screen, 0 for sequential metrics output (default: 1 if running in a console, 0 otherwise)"));
        strUsage += HelpMessageOpt("-metricsrefreshtime", strprintf(translate("Number of seconds between metrics refreshes (default: %u if running in a console, %u otherwise)"), 1, 600));
    }

    strUsage += HelpMessageGroup(translate("Masternode options:"));
    strUsage += HelpMessageOpt("-enablemnsynccheck", translate("Enable automatic mn sync checks status and reset if no 10 SN received in the 30 minutes after initial block download done and then check every 30 minutes (default: 0)"));

    return strUsage;
}

static void BlockNotifyCallback(const uint256& hashNewTip)
{
    string strCmd = GetArg("-blocknotify", "");

    replaceAll(strCmd, "%s", hashNewTip.GetHex());
    thread t(runCommand, strCmd); // thread runs free
}

struct CImportingNow
{
    CImportingNow()
    {
        assert(fImporting == false);
        fImporting = true;
        LogPrint("net", "Block importing mode is ON\n");
    }

    ~CImportingNow()
    {
        assert(fImporting == true);
        fImporting = false;
        LogPrint("net", "Block importing mode is OFF\n");
    }
};


// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
void CleanupBlockRevFiles()
{
    map<string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    fs::path blocksdir = GetDataDir() / "blocks";
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator(); it++) {
        if (fs::is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8,4) == ".dat")
        {
            if (it->path().filename().string().substr(0,3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3,5)] = it->path();
            else if (it->path().filename().string().substr(0,3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    for (const auto &[blockIndex, blockFileName] : mapBlockFiles)
    {
        if (atoi(blockIndex) == nContigCounter)
        {
            nContigCounter++;
            continue;
        }
        remove(blockFileName);
    }
}

void ThreadImport(vector<fs::path> vImportFiles)
{
    RenameThread("psl-loadblk");
    const auto& chainparams = Params();
    // -reindex
    if (fReindex)
    {
        CImportingNow imp;
        int nFile = 0;
        while (true)
        {
            CDiskBlockPos pos(nFile, 0);
            if (!fs::exists(GetBlockPosFilename(pos, "blk")))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            LogFnPrintf("Reindexing block file blk%05u.dat...", (unsigned int)nFile);
            // file is autoclosed in LoadExternalBlockFile
            LoadExternalBlockFile(chainparams, file, &pos);
            nFile++;
        }
        gl_pBlockTreeDB->WriteReindexing(false);
        fReindex = false;
        LogFnPrintf("Reindexing finished");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        InitBlockIndex(chainparams);
    }

    // hardcoded $DATADIR/bootstrap.dat
    fs::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (fs::exists(pathBootstrap))
    {
        FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        const errno_t err = fopen_s(&file, pathBootstrap.string().c_str(), "rb");
#else
        file = fopen(pathBootstrap.string().c_str(), "rb");
#endif
        if (file)
        {
            CImportingNow imp;
            fs::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogFnPrintf("Importing bootstrap.dat...");
            // file is autoclosed in LoadExternalBlockFile
            LoadExternalBlockFile(chainparams, file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogFnPrintf("Warning: Could not open bootstrap file %s", pathBootstrap.string());
        }
    }

    // -loadblock=
    for (const auto& path : vImportFiles)
    {
        FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        const errno_t err = fopen_s(&file, path.string().c_str(), "rb");
#else
        file = fopen(path.string().c_str(), "rb");
#endif
        if (file)
        {
            CImportingNow imp;
            LogFnPrintf("Importing blocks file %s...", path.string());
            // file is autoclosed in LoadExternalBlockFile
            LoadExternalBlockFile(chainparams, file);
        } else {
            LogFnPrintf("Warning: Could not open blocks file %s", path.string());
        }
    }

    if (GetBoolArg("-stopafterblockimport", false))
    {
        LogFnPrintf("Stopping after block import");
        StartShutdown();
    }
}

/** Sanity checks
 *  Ensure that Bitcoin is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck(void)
{
    if(!ECC_InitSanityCheck())
    {
        InitError("Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }
#ifdef __GLIBC__
    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;
#endif

    return true;
}

static void ZC_LoadParams(
    const CChainParams& chainparams
)
{
    struct timeval tv_start, tv_end;
    float elapsed;

    fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    if (!(
        fs::exists(sapling_spend) &&
        fs::exists(sapling_output) &&
        fs::exists(sprout_groth16)
    )) {
        uiInterface.ThreadSafeMessageBox(strprintf(
            translate("Cannot find the Pastel network parameters in the following directory:\n"
              "%s\n"
              "Please run 'pastel-fetch-params' or './pcutil/fetch-params.sh' and then restart."),
                ZC_GetParamsDir()),
            "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return;
    }

    static_assert(
        sizeof(fs::path::value_type) == sizeof(codeunit),
        "librustzcash not configured correctly");
    auto sapling_spend_str = sapling_spend.native();
    auto sapling_output_str = sapling_output.native();
    auto sprout_groth16_str = sprout_groth16.native();

    LogPrintf("Loading Sapling (Spend) parameters from %s\n", sapling_spend.string().c_str());
    LogPrintf("Loading Sapling (Output) parameters from %s\n", sapling_output.string().c_str());
    LogPrintf("Loading Sapling (Sprout Groth16) parameters from %s\n", sprout_groth16.string().c_str());
    gettimeofday(&tv_start, 0);

    librustzcash_init_zksnark_params(
        reinterpret_cast<const codeunit*>(sapling_spend_str.c_str()),
        sapling_spend_str.length(),
        "8270785a1a0d0bc77196f000ee6d221c9c9894f55307bd9357c3f0105d31ca63991ab91324160d8f53e2bbd3c2633a6eb8bdf5205d822e7f3f73edac51b2b70c",
        reinterpret_cast<const codeunit*>(sapling_output_str.c_str()),
        sapling_output_str.length(),
        "657e3d38dbb5cb5e7dd2970e8b03d69b4787dd907285b5a7f0790dcc8072f60bf593b32cc2d1c030e00ff5ae64bf84c5c3beb84ddc841d48264b4a171744d028",
        reinterpret_cast<const codeunit*>(sprout_groth16_str.c_str()),
        sprout_groth16_str.length(),
        "e9b238411bd6c0ec4791e9d04245ec350c9c5744f5610dfcce4365d5ca49dfefd5054e371842b3f88fa1b9d7e8e075249b3ebabd167fa8b0f3161292d36c180a"
    );

    gettimeofday(&tv_end, 0);
    elapsed = float(tv_end.tv_sec-tv_start.tv_sec) + (tv_end.tv_usec-tv_start.tv_usec)/float(1000000);
    LogPrintf("Loaded Sapling parameters in %fs seconds.\n", elapsed);

    if (!gl_pOrphanTxManager)
        gl_pOrphanTxManager = make_unique<COrphanTxManager>();
}

bool AppInitServers()
{
    RPCServer::OnStopped(&OnRPCStopped);
    RPCServer::OnPreCommand(&OnRPCPreCommand);
    if (!InitHTTPServer())
        return false;
    if (!StartRPC())
        return false;
    if (!StartHTTPRPC())
        return false;
    if (GetBoolArg("-rest", false) && !StartREST())
        return false;
    if (!StartHTTPServer())
        return false;
    return true;
}

#ifdef WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    StartShutdown();
    return TRUE;
}
#endif // WIN32

/** Initialize pasteld.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(CServiceThreadGroup& threadGroup, CScheduler& scheduler)
{
    string strError;

    if (!gl_LogMgr)
        return InitError("Error: Log Manager is not initialized");

    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
        return InitError("Error: SetConsoleCtrlHandler failed");
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol) 
        setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking())
        return InitError("Error: Initializing networking failed");

    if (IsShutdownRequested())
		return false;

#ifndef WIN32
    if (GetBoolArg("-sysperms", false)) {
#ifdef ENABLE_WALLET
        if (!GetBoolArg("-disablewallet", false))
            return InitError("Error: -sysperms is not allowed in combination with enabled wallet functionality");
#endif
    } else {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, nullptr);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#endif

    set_new_handler(new_handler_terminate);

    // ********************************************************* Step 2: parameter interactions
    const CChainParams& chainparams = Params();
    KeyIO keyIO(chainparams);

    // initialize experimental features
    auto expFeaturesErrorMsg = InitExperimentalFeatures();
    if (expFeaturesErrorMsg)
        return InitError(expFeaturesErrorMsg.value());

    // Set this early so that parameter interactions go to console
    string error;
    if (!gl_LogMgr->SetPrintToConsoleMode(error))
        return InitError(error);
    fLogTimestamps = GetBoolArg("-logtimestamps", true);
    fLogIPs = GetBoolArg("-logips", false);

    LogPrintf("\n\n\n\n%s\n", string(120, '='));
    LogPrintf("Pastel version %s (%s), protocol version (%d)\n", FormatFullVersion(), CLIENT_DATE, PROTOCOL_VERSION);

    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (mapArgs.count("-bind")) {
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (mapArgs.count("-whitebind")) {
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (mapArgs.count("-proxy"))
    {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!GetBoolArg("-listen", DEFAULT_LISTEN))
    {
        // do not try to retrieve public IP when not listening (pointless)
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (mapArgs.count("-externalip"))
    {
        // if an explicit public IP is specified, do not try to find others
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    if (GetBoolArg("-salvagewallet", false))
    {
        // Rewrite just private keys: rescan to find transactions
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("%s: parameter interaction: -salvagewallet=1 -> setting -rescan=1\n", __func__);
    }

    // -zapwallettx implies a rescan
    if (GetBoolArg("-zapwallettxes", false))
    {
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("%s: parameter interaction: -zapwallettxes=<mode> -> setting -rescan=1\n", __func__);
    }

    // Make sure enough file descriptors are available
    int nBind = max((int)mapArgs.count("-bind") + (int)mapArgs.count("-whitebind"), 1);
    uint32_t nFdSoftLimit = static_cast<uint32_t>(GetArg("-fdsoftlimit", DEFAULT_FD_SOFT_LIMIT));
    gl_nMaxConnections = static_cast<uint32_t>(GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS));
    gl_nMaxConnections = min(gl_nMaxConnections, static_cast<uint32_t>(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS));
    const uint32_t nFdLimit = RaiseFileDescriptorLimit(max(nFdSoftLimit, gl_nMaxConnections + MIN_CORE_FILEDESCRIPTORS));
    LogPrintf("File descriptor limit: %u\n", nFdLimit);
    if (nFdLimit < MIN_CORE_FILEDESCRIPTORS)
        return InitError(translate("Not enough file descriptors available."));
    if (nFdLimit - MIN_CORE_FILEDESCRIPTORS < gl_nMaxConnections)
        gl_nMaxConnections = nFdLimit - MIN_CORE_FILEDESCRIPTORS;

    // if using block pruning, then disable txindex
    // also disable the wallet (for now, until SPV support is implemented in wallet)
    if (GetArg("-prune", 0))
    {
        if (GetBoolArg("-txindex", false))
            return InitError(translate("Prune mode is incompatible with -txindex."));
#ifdef ENABLE_WALLET
        if (!GetBoolArg("-disablewallet", false))
        {
            if (SoftSetBoolArg("-disablewallet", true))
                LogPrintf("%s : parameter interaction: -prune -> setting -disablewallet=1\n", __func__);
            else
                return InitError(translate("Can't run with a wallet in prune mode."));
        }
#endif
    }

    // ********************************************************* Step 3: parameter-to-internal-flags

    fDebug = !mapMultiArgs["-debug"].empty();
    // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
    const auto& categories = mapMultiArgs["-debug"];
    if (GetBoolArg("-nodebug", false) || find(categories.begin(), categories.end(), string("0")) != categories.end())
        fDebug = false;

    // Special case: if debug=zrpcunsafe, implies debug=zrpc, so add it to debug categories
    if (find(categories.begin(), categories.end(), string("zrpcunsafe")) != categories.end()) {
        if (find(categories.begin(), categories.end(), string("zrpc")) == categories.end()) {
            LogPrintf("%s: parameter interaction: setting -debug=zrpcunsafe -> -debug=zrpc\n", __func__);
            vector<string>& v = mapMultiArgs["-debug"];
            v.push_back("zrpc");
        }
    }

    // Check for -debugnet
    if (GetBoolArg("-debugnet", false))
        InitWarning(translate("Warning: Unsupported argument -debugnet ignored, use -debug=net."));
    // Check for -socks - as this is a privacy risk to continue, exit here
    if (mapArgs.count("-socks"))
        return InitError(translate("Error: Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported."));
    // Check for -tor - as this is a privacy risk to continue, exit here
    if (GetBoolArg("-tor", false))
        return InitError(translate("Error: Unsupported argument -tor found, use -onion."));

    if (GetBoolArg("-benchmark", false))
        InitWarning(translate("Warning: Unsupported argument -benchmark ignored, use -debug=bench."));

    // Checkmempool and checkblockindex default to true in regtest mode
    const int nCheckPool = static_cast<int>(GetArg("-checkmempool", chainparams.DefaultConsistencyChecks()) ? 1 : 0);
    int ratio = min<int>(max<int>(nCheckPool, 0), 1000000);
    if (ratio != 0) {
        mempool.setSanityCheck(1.0 / ratio);
    }
    fCheckBlockIndex = GetBoolArg("-checkblockindex", chainparams.DefaultConsistencyChecks());
    fCheckpointsEnabled = GetBoolArg("-checkpoints", true);

    if (IsShutdownRequested())
		return false;

    // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
    gl_ScriptCheckManager.SetThreadCount(GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS));

    fServer = GetBoolArg("-server", false);

    // block pruning; get the amount of disk space (in MB) to allot for block & undo files
    int64_t nSignedPruneTarget = GetArg("-prune", 0) * 1024 * 1024;
    if (nSignedPruneTarget < 0)
        return InitError(translate("Prune cannot be configured with a negative value."));
    nPruneTarget = (uint64_t) nSignedPruneTarget;
    if (nPruneTarget)
    {
        if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES)
            return InitError(strprintf(translate("Prune configured below the minimum of %d MB.  Please use a higher number."), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
        LogPrintf("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    RegisterAllCoreRPCCommands(tableRPC);
#ifdef ENABLE_WALLET
    bool fDisableWallet = GetBoolArg("-disablewallet", false);
    if (!fDisableWallet)
        RegisterWalletRPCCommands(tableRPC);
#endif

    nConnectTimeout = static_cast<int>(GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT));
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    // Fee-per-kilobyte amount considered the same as "free"
    // If you are mining, be careful setting this:
    // if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-patoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    if (mapArgs.count("-minrelaytxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-minrelaytxfee"], n) && n > 0)
            gl_ChainOptions.minRelayTxFee = CFeeRate(n);
        else
            return InitError(strprintf(translate("Invalid amount for -minrelaytxfee=<amount>: '%s'"), mapArgs["-minrelaytxfee"]));
    }

#ifdef ENABLE_WALLET
    if (mapArgs.count("-mintxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-mintxfee"], n) && n > 0)
            CWallet::minTxFee = CFeeRate(n);
        else
            return InitError(strprintf(translate("Invalid amount for -mintxfee=<amount>: '%s'"), mapArgs["-mintxfee"]));
    }
    if (mapArgs.count("-paytxfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-paytxfee"], nFeePerK))
            return InitError(strprintf(translate("Invalid amount for -paytxfee=<amount>: '%s'"), mapArgs["-paytxfee"]));
        if (nFeePerK > nHighTransactionFeeWarning)
            InitWarning(translate("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < gl_ChainOptions.minRelayTxFee)
        {
            return InitError(strprintf(translate("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                                       mapArgs["-paytxfee"], gl_ChainOptions.minRelayTxFee.ToString()));
        }
    }
    if (mapArgs.count("-maxtxfee"))
    {
        CAmount nMaxFee = 0;
        if (!ParseMoney(mapArgs["-maxtxfee"], nMaxFee))
            return InitError(strprintf(translate("Invalid amount for -maxtxfee=<amount>: '%s'"), mapArgs["-maptxfee"]));
        if (nMaxFee > nHighTransactionMaxFeeWarning)
            InitWarning(translate("Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee = nMaxFee;
        if (CFeeRate(maxTxFee, 1000) < gl_ChainOptions.minRelayTxFee)
        {
            return InitError(strprintf(translate("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                                       mapArgs["-maxtxfee"], gl_ChainOptions.minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = static_cast<unsigned int>(GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET));
    gl_ChainOptions.expiryDelta = static_cast<unsigned int>(GetArg("-txexpirydelta", DEFAULT_TX_EXPIRY_DELTA));
    uint32_t minExpiryDelta = TX_EXPIRING_SOON_THRESHOLD + 1;
    if (gl_ChainOptions.expiryDelta < minExpiryDelta)
        return InitError(strprintf(translate("Invalid value for -expiryDelta='%u' (must be least %u)"), gl_ChainOptions.expiryDelta, minExpiryDelta));
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", true);
    fSendFreeTransactions = GetBoolArg("-sendfreetransactions", false);

    string strWalletFile = GetArg("-wallet", "wallet.dat");
#endif // ENABLE_WALLET

    fIsBareMultisigStd = GetBoolArg("-permitbaremultisig", true);
    nMaxDatacarrierBytes = static_cast<unsigned int>(GetArg("-datacarriersize", nMaxDatacarrierBytes));

    fAlerts = GetBoolArg("-alerts", DEFAULT_ALERTS);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (GetBoolArg("-peerbloomfilters", true))
        nLocalServices |= NODE_BLOOM;

    nMaxTipAge = GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE);
    if (nMaxTipAge != DEFAULT_MAX_TIP_AGE)
        LogPrintf("Setting maximum tip age to %d seconds\n", nMaxTipAge);

    if (!mapMultiArgs["-nuparams"].empty()) {
        // Allow overriding network upgrade parameters for testing
        if (!chainparams.IsRegTest())
            return InitError("Network upgrade parameters may only be overridden on regtest.");
        v_strings vDeploymentParams;
        const v_strings& deployments = mapMultiArgs["-nuparams"];
        for (const auto &sDeployment : deployments)
        {
            str_split(vDeploymentParams, sDeployment, ':');
            if (vDeploymentParams.size() != 2) {
                return InitError("Network upgrade parameters malformed, expecting hexBranchId:activationHeight");
            }
            int nValue;
            if (!ParseInt32(vDeploymentParams[1], &nValue) || (nValue < 0))
                return InitError(strprintf("Invalid nActivationHeight (%s)", vDeploymentParams[1]));
            const uint32_t nActivationHeight = static_cast<uint32_t>(nValue);
            bool found = false;
            // Exclude Sprout from upgrades
            for (auto i = to_integral_type(Consensus::UpgradeIndex::BASE_SPROUT) + 1; i < to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES); ++i)
            {
                if (vDeploymentParams[0].compare(HexInt(NetworkUpgradeInfo[i].nBranchId)) == 0)
                {
                    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex(i), nActivationHeight);
                    found = true;
                    LogPrintf("Setting network upgrade activation parameters for %s to height=%u\n", vDeploymentParams[0], nActivationHeight);
                    break;
                }
            }
            if (!found) {
                return InitError(strprintf("Invalid network upgrade (%s)", vDeploymentParams[0]));
            }
        }
    }

    if (IsShutdownRequested())
		return false;

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    // Initialize libsodium
    if (init_and_check_sodium() == -1) {
        return false;
    }

    // Sanity check
    if (!InitSanityCheck())
        return InitError(translate("Initialization sanity check failed. Pastel is shutting down."));

    string strDataDir = GetDataDir().string();
#ifdef ENABLE_WALLET
    auto walletFilePath = fs::path(strWalletFile);
    // Wallet file must be a plain filename without a directory
    if (strWalletFile != walletFilePath.stem().string() + walletFilePath.extension().string())
        return InitError(strprintf(translate("Wallet %s resides outside data directory %s"), strWalletFile, strDataDir));
#endif
    // Make sure only a single pasteld process is using the data directory.
    fs::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    errno_t err = fopen_s(&file, pathLockFile.string().c_str(), "a");
#else
    file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
#endif
    if (file)
        fclose(file);

    try {
        static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
        if (!lock.try_lock())
            return InitError(strprintf(translate("Cannot obtain a lock on data directory %s. Pastel is probably already running."), strDataDir));
    } catch(const boost::interprocess::interprocess_exception& e) {
        return InitError(strprintf(translate("Cannot obtain a lock on data directory %s. Pastel is probably already running.") + " %s.", strDataDir, e.what()));
    }

#ifndef WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        gl_LogMgr->ShrinkDebugLogFile(true);

    gl_LogMgr->OpenDebugLogFile();
    if (!gl_LogRotationManager)
    {
		gl_LogRotationManager = make_unique<CLogRotationManager>();
		if (threadGroup.add_thread(error, gl_LogRotationManager, true) == INVALID_THREAD_OBJECT_ID)
			return InitError(translate("Failed to create log rotation thread. ") + error);
    }

    LogPrintf("Using OpenSSL version %s\n", OpenSSL_version(OPENSSL_VERSION_STRING));
#ifdef ENABLE_WALLET
    LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
#endif
    if (!fLogTimestamps)
        LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", strDataDir);
    LogPrintf("Using config file %s\n", GetConfigFile().string());
    LogPrintf("Using at most %u connections (%i file descriptors available)\n", gl_nMaxConnections, nFdLimit);
#ifdef ENABLE_TICKET_COMPRESS
    LogPrintf("Ticket compression is enabled\n");
#endif
    if (!categories.empty())
        LogPrintf("Using debug log categories: %s\n", str_join(categories, ", "));
    ostringstream strErrors;

    if (IsShutdownRequested())
		return false;

    gl_ScriptCheckManager.create_workers(threadGroup);

    // Start the lightweight task scheduler thread
    scheduler.add_workers(1);

    // Count uptime
    MarkStartTime();

    if (!chainparams.IsRegTest() &&
            GetBoolArg("-showmetrics", isatty(STDOUT_FILENO)) &&
            !gl_LogMgr->IsPrintToConsole() && !GetBoolArg("-daemon", false))
    {
        // Start the persistent metrics interface
        ConnectMetricsScreen();
        if (threadGroup.add_func_thread(strError, "metrics", ThreadShowMetricsScreen) == INVALID_THREAD_OBJECT_ID)
			return InitError(translate("Failed to create metrics thread. ") + strError);
    }

    // These must be disabled for now, they are buggy and we probably don't
    // want any of libsnark's profiling in production anyway.
    libsnark::inhibit_profiling_info = true;
    libsnark::inhibit_profiling_counters = true;

    if (IsShutdownRequested())
		return false;

    // Initialize Zcash circuit parameters
    uiInterface.InitMessage(translate("Initializing chain parameters..."));
    ZC_LoadParams(chainparams);

    if (IsShutdownRequested())
		return false;

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (fServer)
    {
        uiInterface.InitMessage.connect(SetRPCWarmupStatus);
        if (!AppInitServers())
            return InitError(translate("Unable to start HTTP server. See debug log for details."));
    }

    int64_t nStart;

    // ********************************************************* Step 5: verify wallet database integrity
#ifdef ENABLE_WALLET
    if (!fDisableWallet)
    {
        LogPrintf("Using wallet %s\n", strWalletFile);
        uiInterface.InitMessage(translate("Verifying wallet..."));

        string warningString;
        string errorString;

        if (!CWallet::Verify(strWalletFile, warningString, errorString))
            return false;

        if (!warningString.empty())
            InitWarning(warningString);
        if (!errorString.empty())
            return InitError(warningString);

    } // (!fDisableWallet)
#endif // ENABLE_WALLET
    // ********************************************************* Step 6: network initialization

    RegisterNodeSignals(GetNodeSignals());

    // sanitize comments per BIP-0014, format user agent and check total size
    v_strings uacomments;
    for (const auto &cmt : mapMultiArgs["-uacomment"])
    {
        auto sComment = SanitizeString(cmt, SAFE_CHARS_UA_COMMENT);
        if (cmt != sComment)
            return InitError(strprintf("User Agent comment (%s) contains unsafe characters.", cmt));
        uacomments.emplace_back(std::move(sComment));
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH)
    {
        return InitError(strprintf("Total length of network version string %i exceeds maximum of %i characters. Reduce the number and/or size of uacomments.",
            strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (mapArgs.count("-onlynet"))
    {
        set<enum Network> nets;
        for (const auto& snet : mapMultiArgs["-onlynet"])
        {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(translate("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++)
        {
            enum Network net = (enum Network)n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    if (mapArgs.count("-whitelist"))
    {
        set<string> vSubnets;
        for (const auto& subnetSpec : mapMultiArgs["-whitelist"])
        {
            // whitelist can be defined via file
            if (str_starts_with(subnetSpec, "@"))
            {
                string filename = subnetSpec.substr(1);
                trim(filename);
                fs::path filePath = GetDataDir() / filename;
                if (!fs::exists(filePath))
					return InitError(strprintf(translate("File '%s' with whitelist subnets does not exist"), filePath.string()));
                ifstream file(filePath.string());
				if (!file.is_open())
                    return InitError(strprintf(translate("File '%s' with whitelist subnets cannot be opened"), filePath.string()));
                LogFnPrintf("Loading whitelist subnets from file [%s]", filePath.string());
				string line;
				while (getline(file, line))
				{
					trim(line);
					if (line.empty() || line[0] == '#' || line[0] == ';')
						continue;
                    vSubnets.emplace(std::move(line));
				}
            }
            else
				vSubnets.emplace(subnetSpec);
        }
        LogFnPrintf("Processing %zu whitelist subnets", vSubnets.size());
        for (const auto& net : vSubnets)
        {
            CSubNet subnet(net);
            if (!subnet.IsValid())
                return InitError(strprintf(translate("Invalid netmask specified in -whitelist: '%s'"), net));
            CNode::AddWhitelistedRange(subnet);
        }
    }

    bool proxyRandomize = GetBoolArg("-proxyrandomize", true);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    string proxyArg = GetArg("-proxy", "");
    SetLimited(NET_TOR);
    if (proxyArg != "" && proxyArg != "0") {
        proxyType addrProxy = proxyType(CService(proxyArg, 9050), proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(translate("Invalid -proxy address: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_TOR, addrProxy);
        SetNameProxy(addrProxy);
        SetLimited(NET_TOR, false); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    string onionArg = GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetLimited(NET_TOR); // set onions as unreachable
        } else {
            proxyType addrOnion = proxyType(CService(onionArg, 9050), proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(translate("Invalid -onion address: '%s'"), onionArg));
            SetProxy(NET_TOR, addrOnion);
            SetLimited(NET_TOR, false);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", true);

    bool fBound = false;
    if (fListen)
    {
        if (mapArgs.count("-bind") || mapArgs.count("-whitebind"))
        {
            for (const auto& strBind : mapMultiArgs["-bind"])
            {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(translate("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            for (const auto& strBind : mapMultiArgs["-whitebind"])
            {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return InitError(strprintf(translate("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return InitError(strprintf(translate("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        }
        else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(translate("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (mapArgs.count("-externalip"))
    {
        for (const auto& strAddr : mapMultiArgs["-externalip"])
        {
            CService addrLocal(strAddr, GetListenPort(), fNameLookup);
            if (!addrLocal.IsValid())
                return InitError(strprintf(translate("Cannot resolve -externalip address: '%s'"), strAddr));
            AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LocalAddressType::MANUAL);
        }
    }

    for (const auto& strDest : mapMultiArgs["-seednode"])
        AddOneShot(strDest);

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::CreateWithArguments(mapArgs);

    if (pzmqNotificationInterface) {
        RegisterValidationInterface(pzmqNotificationInterface);
    }
#endif

#if ENABLE_PROTON
    pAMQPNotificationInterface = AMQPNotificationInterface::CreateWithArguments(mapArgs);

    if (pAMQPNotificationInterface) {

        // AMQP support is currently an experimental feature, so fail if user configured AMQP notifications
        // without enabling experimental features.
        if (!fExperimentalMode) {
            return InitError(translate("AMQP support requires -experimentalfeatures."));
        }

        RegisterValidationInterface(pAMQPNotificationInterface);
    }
#endif

    // ********************************************************* Step 7: load block chain
    fReindex = GetBoolArg("-reindex", false);
    if (fReindex)
        LogFnPrintf("Reindexing mode");

    // Upgrading to 0.8; hard-link the old blknnnn.dat files into /blocks/
    fs::path blocksDir = GetDataDir() / "blocks";
    if (!fs::exists(blocksDir))
    {
        fs::create_directories(blocksDir);
        bool bLinked = false;
        for (uint32_t i = 1; i < 10000; i++)
        {
            fs::path source = GetDataDir() / strprintf("blk%04u.dat", i);
            if (!fs::exists(source)) 
                break;
            fs::path dest = blocksDir / strprintf("blk%05u.dat", i-1);
            try {
                fs::create_hard_link(source, dest);
                LogPrintf("Hardlinked %s -> %s\n", source.string(), dest.string());
                bLinked = true;
            } catch (const fs::filesystem_error& e) {
                // Note: hardlink creation failing is not a disaster, it just means
                // blocks will get re-downloaded from peers.
                LogPrintf("Error hardlinking blk%04u.dat: %s\n", i, e.what());
                break;
            }
        }
        if (bLinked)
            fReindex = true;
    }

    // cache size calculations
    int64_t nTotalCache = (GetArg("-dbcache", nDefaultDbCache) << 20);
    nTotalCache = max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greated than nMaxDbcache
    int64_t nBlockTreeDBCache = nTotalCache / 8;
    if (nBlockTreeDBCache > (1 << 21) && !fTxIndex)
        nBlockTreeDBCache = (1 << 21); // block tree db cache shouldn't be larger than 2 MiB

    fTxIndex = GetBoolArg("-txindex", false);
    LogFnPrintf("(option) transaction index %s", fTxIndex ? "enabled" : "disabled");

    fInsightExplorer = GetBoolArg("-insightexplorer", false);
    LogFnPrintf("(option) insight explorer %s", fInsightExplorer ? "enabled" : "disabled");
    if (fInsightExplorer)
    {
        if (!fTxIndex)
            return InitError(translate("-insightexplorer requires -txindex."));

        // increase cache if additional indices are needed
        nBlockTreeDBCache = nTotalCache * 3 / 4;
    }
    SetInsightExplorer(fInsightExplorer);
    nTotalCache -= nBlockTreeDBCache;
    int64_t nCoinDBCache = min(nTotalCache / 2, (nTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    nTotalCache -= nCoinDBCache;
    nCoinCacheUsage = nTotalCache; // the rest goes to in-memory cache
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Using %.1fMiB for block index database\n", nBlockTreeDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for chain state database\n", nCoinDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for in-memory UTXO set\n", nCoinCacheUsage * (1.0 / 1024 / 1024));

    // connect Pastel Ticket txmempool tracker
    mempool.AddTxMemPoolTracker(CPastelTicketProcessor::GetTxMemPoolTracker());

    if (IsShutdownRequested())
		return false;

    bool bClearWitnessCaches = false;
    bool fLoaded = false;
    while (!fLoaded)
    {
        bool fReset = fReindex;
        string strLoadError;

        uiInterface.InitMessage(translate("Loading block index..."));

        nStart = GetTimeMillis();
        do {
            try {
                UnloadBlockIndex();
                gl_pCoinsTip.reset();
                gl_pCoinsDbView.reset();
                pCoinsCatcher.reset();
                gl_pBlockTreeDB.reset();

                gl_pBlockTreeDB = make_unique<CBlockTreeDB>(nBlockTreeDBCache, false, fReindex);
                gl_pCoinsDbView = make_unique<CCoinsViewDB>(nCoinDBCache, false, fReindex);
                pCoinsCatcher = make_unique<CCoinsViewErrorCatcher>(gl_pCoinsDbView.get());
                gl_pCoinsTip = make_unique<CCoinsViewCache>(pCoinsCatcher.get());

                if (fReindex)
                {
                    gl_pBlockTreeDB->WriteReindexing(true);
                    //If we're reindexing in prune mode, wipe away unusable block files and all undo data files
                    if (fPruneMode)
                        CleanupBlockRevFiles();
                }

                // Initialize the ticket database
                masterNodeCtrl.InitTicketDB();

                if (!LoadBlockIndex(strLoadError))
                {
                    if (IsShutdownRequested())
                        break;

                    strLoadError = translate("Error loading block database. ") + strLoadError;
                    break;
                }

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
                if (!mapBlockIndex.empty() && mapBlockIndex.count(chainparams.GetConsensus().hashGenesisBlock) == 0)
                    return InitError(translate("Incorrect or no genesis block found. Wrong datadir for network?"));

                // Initialize the block index (no-op if non-empty database was already loaded)
                if (!InitBlockIndex(chainparams))
                {
                    strLoadError = translate("Error initializing block database");
                    break;
                }

                // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
                // in the past, but is now trying to run unpruned.
                if (fHavePruned && !fPruneMode)
                {
                    strLoadError = translate("You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain");
                    break;
                }

                if (!fReindex)
                {
                    uiInterface.InitMessage(translate("Rewinding blocks if needed..."));
                    if (!RewindBlockIndex(chainparams, bClearWitnessCaches))
                    {
                        strLoadError = translate("Unable to rewind the database to a pre-upgrade state. You will need to redownload the blockchain");
                        break;
                    }
                }

                const uint32_t nBlockDBCheckBlocks = static_cast<uint32_t>(GetArg("-checkblocks", DEFAULT_BLOCKDB_CHECKBLOCKS));
                const uint32_t nBlockDBCheckLevel = static_cast<uint32_t>(GetArg("-checklevel", DEFAULT_BLOCKDB_CHECKLEVEL));

                uiInterface.InitMessage(strprintf(translate("Verifying last %u blocks..."), nBlockDBCheckBlocks));
                if (fHavePruned && nBlockDBCheckBlocks > MIN_BLOCKS_TO_KEEP)
                {
                    LogPrintf("Prune: pruned datadir may not have more than %u blocks; -checkblocks=%u may fail\n",
                        DEFAULT_BLOCKDB_CHECKBLOCKS, nBlockDBCheckBlocks);
                }
                if (!CVerifyDB().VerifyDB(chainparams, gl_pCoinsDbView.get(), nBlockDBCheckLevel, nBlockDBCheckBlocks))
                {
                    strLoadError = translate("Corrupted block database detected");
                    break;
                }
            } catch (const exception& e) {
                if (fDebug)
                    LogPrintf("%s\n", e.what());
                strLoadError = translate("Error opening block database");
                break;
            }

            fLoaded = true;
        } while(false);

        if (!fLoaded)
        {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeQuestion(
                    strLoadError + ".\n\n" + translate("Do you want to rebuild the block database now?"),
                    strLoadError + ".\nPlease restart with -reindex to recover.",
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    fRequestShutdown = false;
                } else {
                    LogPrintf("%s. Aborted block database rebuild. Exiting.\n", strLoadError);
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

    fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    FILE* fp = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    err = fopen_s(&fp, est_path.string().c_str(), "rb");
#else
    fp = fopen(est_path.string().c_str(), "rb");
#endif
    CAutoFile est_filein(fp, SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        mempool.ReadFeeEstimates(est_filein);
    fFeeEstimatesInitialized = true;


    // ********************************************************* Step 8: load wallet
#ifdef ENABLE_WALLET
    if (fDisableWallet)
    {
        pwalletMain = nullptr;
        LogPrintf("Wallet disabled!\n");
    } else {

        // needed to restore wallet transaction meta data after -zapwallettxes
        vector<CWalletTx> vWtx;

        if (GetBoolArg("-zapwallettxes", false)) {
            uiInterface.InitMessage(translate("Zapping all transactions from wallet..."));

            pwalletMain = new CWallet(strWalletFile);
            DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
            if (nZapWalletRet != DB_LOAD_OK) {
                uiInterface.InitMessage(translate("Error loading wallet.dat: Wallet corrupted"));
                return false;
            }

            delete pwalletMain;
            pwalletMain = nullptr;
        }

        uiInterface.InitMessage(translate("Loading wallet..."));

        nStart = GetTimeMillis();
        bool fFirstRun = true;
        pwalletMain = new CWallet(strWalletFile);
        DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
        if (nLoadWalletRet != DB_LOAD_OK)
        {
            if (nLoadWalletRet == DB_CORRUPT)
                strErrors << translate("Error loading wallet.dat: Wallet corrupted") << "\n";
            else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
            {
                string msg(translate("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                             " or address book entries might be missing or incorrect."));
                InitWarning(msg);
            }
            else if (nLoadWalletRet == DB_TOO_NEW)
                strErrors << translate("Error loading wallet.dat: Wallet requires newer version of Pastel") << "\n";
            else if (nLoadWalletRet == DB_NEED_REWRITE)
            {
                strErrors << translate("Wallet needed to be rewritten: restart Pastel to complete") << "\n";
                LogPrintf("%s", strErrors.str());
                return InitError(strErrors.str());
            }
            else
                strErrors << translate("Error loading wallet.dat") << "\n";
        }

        if (GetBoolArg("-upgradewallet", fFirstRun))
        {
            int nMaxVersion = static_cast<int>(GetArg("-upgradewallet", 0));
            if (nMaxVersion == 0) // the -upgradewallet without argument case
            {
                LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
                nMaxVersion = CLIENT_VERSION;
                pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
            }
            else
                LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
            if (nMaxVersion < pwalletMain->GetVersion())
                strErrors << translate("Cannot downgrade wallet") << "\n";
            pwalletMain->SetMaxVersion(nMaxVersion);
        }

        if (!pwalletMain->HaveHDSeed())
        {
            // generate a new HD seed
            pwalletMain->GenerateNewSeed();
        }

        if (fFirstRun)
        {
            // Create new keyUser and set as default key
            CPubKey newDefaultKey;
            if (pwalletMain->GetKeyFromPool(newDefaultKey)) {
                pwalletMain->SetDefaultKey(newDefaultKey);
                if (!pwalletMain->SetAddressBook(pwalletMain->vchDefaultKey.GetID(), "", "receive"))
                    strErrors << translate("Cannot write default address") << "\n";
            }

            pwalletMain->SetBestChain(chainActive.GetLocator());
        }

        LogPrintf("%s\n", strErrors.str());
        LogPrintf(" wallet loaded in %15dms\n", GetTimeMillis() - nStart);

        RegisterValidationInterface(pwalletMain);

        CBlockIndex *pindexRescan = chainActive.Tip();
        if (bClearWitnessCaches || GetBoolArg("-rescan", false))
        {
            pwalletMain->ClearNoteWitnessCache();
            pindexRescan = chainActive.Genesis();
        }
        else
        {
            CWalletDB walletdb(strWalletFile);
            CBlockLocator locator;
            if (walletdb.ReadBestBlock(locator))
                pindexRescan = FindForkInGlobalIndex(chainActive, locator);
            else
                pindexRescan = chainActive.Genesis();
        }
        if (chainActive.Tip() && chainActive.Tip() != pindexRescan)
        {
            uiInterface.InitMessage(translate("Rescanning..."));
            const int nBlocksToRescan = chainActive.Height() - pindexRescan->nHeight;
            LogPrintf("Rescanning last %i blocks (from block %i)...\n", nBlocksToRescan, pindexRescan->nHeight);
            nStart = GetTimeMillis();
            pwalletMain->ScanForWalletTransactions(pindexRescan, true);
            LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
            pwalletMain->SetBestChain(chainActive.GetLocator());
            CWalletDB::IncrementUpdateCounter();

            // Restore wallet transaction metadata after -zapwallettxes=1
            if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2")
            {
                CWalletDB walletdb(strWalletFile);

                for (const auto& wtxOld : vWtx)
                {
                    uint256 hash = wtxOld.GetHash();
                    auto mi = pwalletMain->mapWallet.find(hash);
                    if (mi != pwalletMain->mapWallet.end())
                    {
                        const CWalletTx* copyFrom = &wtxOld;
                        CWalletTx* copyTo = &mi->second;
                        copyTo->mapValue = copyFrom->mapValue;
                        copyTo->vOrderForm = copyFrom->vOrderForm;
                        copyTo->nTimeReceived = copyFrom->nTimeReceived;
                        copyTo->nTimeSmart = copyFrom->nTimeSmart;
                        copyTo->fFromMe = copyFrom->fFromMe;
                        copyTo->strFromAccount = copyFrom->strFromAccount;
                        copyTo->nOrderPos = copyFrom->nOrderPos;
                        copyTo->WriteToDisk(&walletdb);
                    }
                }
            }
        }
        pwalletMain->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", true));
    } // (!fDisableWallet)
#else // ENABLE_WALLET
    LogPrintf("No wallet support compiled in!\n");
#endif // !ENABLE_WALLET

#ifdef ENABLE_MINING
    if (!gl_MiningSettings.initialize(chainparams, strError))
        return InitError(strprintf(translate("Could not initialize PastelMiner settings. %s"), strError));
#endif // ENABLE_MINING

    // ********************************************************* Step 9: data directory maintenance

    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode)
    {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices &= ~NODE_NETWORK;
        if (!fReindex)
        {
            uiInterface.InitMessage(translate("Pruning blockstore..."));
            PruneAndFlush();
        }
    }

    // ********************************************************* Step 10: import blocks

    if (mapArgs.count("-blocknotify"))
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);

    uiInterface.InitMessage(translate("Activating best chain..."));
    // scan for better chains in the block chain database, that are not yet connected in the active best chain
    CValidationState state(TxOrigin::UNKNOWN);
    if (!ActivateBestChain(state, chainparams))
        strErrors << "Failed to connect best block";

    vector<fs::path> vImportFiles;
    if (mapArgs.count("-loadblock"))
    {
        for (const auto& strFile : mapMultiArgs["-loadblock"])
            vImportFiles.push_back(strFile);
    }
    // create a thread that loads blocks from disk, but not start it yet
    const size_t nBlockImportThreadId = threadGroup.add_func_thread(strError, "import-files", bind(&ThreadImport, vImportFiles), false);

    // ********************************************************* Step 11: start masternode
#ifdef ENABLE_WALLET
    if (!masterNodeCtrl.EnableMasterNode(strErrors, threadGroup, pwalletMain))
#else
    if (!masterNodeCtrl.EnableMasterNode(strErrors, threadGroup))
#endif
    {
       return InitError(strErrors.str());
    }
    uiInterface.InitMessage(translate("Importing blocks..."));
    if (!threadGroup.start_thread(strError, nBlockImportThreadId))
        return InitError(strError);

    if (!chainActive.Tip())
    {
        LogPrintf("Waiting for genesis block to be imported...\n");
        while (!fRequestShutdown && !chainActive.Tip())
            MilliSleep(10);
    }

    // ********************************************************* Step 12: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    //// debug print
    LogPrintf("mapBlockIndex.size() = %zu\n",   mapBlockIndex.size());
    LogPrintf("nBestHeight = %d\n",            chainActive.Height());
#ifdef ENABLE_WALLET
    LogPrintf("setKeyPool.size() = %zu\n",      pwalletMain ? pwalletMain->setKeyPool.size() : 0);
    LogPrintf("mapWallet.size() = %zu\n",       pwalletMain ? pwalletMain->mapWallet.size() : 0);
    LogPrintf("mapAddressBook.size() = %zu\n",  pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
#endif

    if (GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
    {
        if (threadGroup.add_thread(strError, make_shared<CTorControlThread>()) == INVALID_THREAD_OBJECT_ID)
			return InitError(translate("Failed to create torcontrol thread. ") + strError);
    }

    if (!StartNode(strError, threadGroup, scheduler))
        return InitError(translate(strError.c_str()));

    // Monitor the chain, and alert if we get blocks much quicker or slower than expected
    const auto& consensusParams = chainparams.GetConsensus();
    const int64_t nPowTargetSpacing = consensusParams.nPowTargetSpacing;
    CScheduler::Function f = bind(&PartitionCheck, consensusParams, fnIsInitialBlockDownload,
                                   ref(cs_main), cref(pindexBestHeader), nPowTargetSpacing);
    scheduler.scheduleEvery(f, nPowTargetSpacing);

#ifdef ENABLE_MINING
    // Generate coins in the background
 #ifdef ENABLE_WALLET
    if (pwalletMain || !gl_MiningSettings.getMinerAddress().empty())
        GenerateBitcoins(gl_MiningSettings.isLocalMiningEnabled(), pwalletMain, chainparams);
 #else
    GenerateBitcoins(gl_MiningSettings.isLocalMiningEnabled(), chainparams);
 #endif
#endif
    string sRewindChainBlockHash = GetArg("-rewindchain", "");
    if (!sRewindChainBlockHash.empty())
	{
        string sErrorMsg;
        if (!RewindChainToBlock(sErrorMsg, chainparams, sRewindChainBlockHash))
            return InitError(sErrorMsg);
	}
    if (mapArgs.count("-repairticketdb"))
    {
        LOCK(cs_main);
        masterNodeCtrl.masternodeTickets.RepairTicketDB(true);
    }
		
    // ********************************************************* Step 13: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(translate("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain)
    {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        // Run a thread to flush wallet periodically
        if (threadGroup.add_thread(strError, make_shared<CFlushWalletDBThread>(pwalletMain->strWalletFile)) == INVALID_THREAD_OBJECT_ID)
            return InitError(translate("Failed to create wallet flush thread. ") + strError);
    }
#endif

    // SENDALERT
    if (threadGroup.add_func_thread(strError, "sendalert", ThreadSendAlert) == INVALID_THREAD_OBJECT_ID)
		return InitError(translate("Failed to create sendalert thread. ") + strError);

    LogFnPrintf("Pastel initialization successful");
    return !fRequestShutdown;
}
