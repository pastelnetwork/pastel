// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <sstream>

#include <univalue.h>

#include <utils/str_utils.h>
#include <utils/sync.h>
#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <utils/random.h>
#include <chain_options.h>
#include <rpc/server.h>
#include <init.h>
#include <key_io.h>
#include <ui_interface.h>
#include <asyncrpcqueue.h>

#include <boost/signals2/signal.hpp>

using namespace RPCServer;
using namespace std;

static bool fRPCRunning = false;
static bool fRPCInWarmup = true;
static string rpcWarmupStatus("RPC server started");
static CCriticalSection cs_rpcWarmup;
/* Timer-creating functions */
static vector<RPCTimerInterface*> timerInterfaces;
// Map of name to timer.
static map<string, shared_ptr<RPCTimerBase> > deadlineTimers;

static struct CRPCSignals
{
    boost::signals2::signal<void ()> Started;
    boost::signals2::signal<void ()> Stopped;
    boost::signals2::signal<void (const CRPCCommand&)> PreCommand;
    boost::signals2::signal<void (const CRPCCommand&)> PostCommand;
} g_rpcSignals;

void RPCServer::OnStarted(function<void ()> slot)
{
    g_rpcSignals.Started.connect(slot);
}

void RPCServer::OnStopped(function<void ()> slot)
{
    g_rpcSignals.Stopped.connect(slot);
}

void RPCServer::OnPreCommand(function<void (const CRPCCommand&)> slot)
{
    g_rpcSignals.PreCommand.connect(slot);
}

void RPCServer::OnPostCommand(function<void (const CRPCCommand&)> slot)
{
    g_rpcSignals.PostCommand.connect(slot);
}

void RPCTypeCheck(const UniValue& params,
                  const list<UniValue::VType>& typesExpected,
                  bool fAllowNull)
{
    size_t i = 0;
    for (const auto &t : typesExpected)
    {
        if (params.size() <= i)
            break;

        const UniValue& v = params[i];
        if (!((v.type() == t) || (fAllowNull && (v.isNull()))))
        {
            string err = strprintf("Expected type %s, got %s",
                                   uvTypeName(t), uvTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
        i++;
    }
}

void RPCTypeCheckObj(const UniValue& o,
                  const map<string, UniValue::VType>& typesExpected,
                  bool fAllowNull)
{
    for (const auto &[sName, vType] : typesExpected)
    {
        const UniValue& v = find_value(o, sName);
        if (!fAllowNull && v.isNull())
            throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Missing %s", sName));

        if (!((v.type() == vType) || (fAllowNull && (v.isNull()))))
        {
            string err = strprintf("Expected type %s for %s, got %s",
                                   uvTypeName(vType), sName, uvTypeName(v.type()));
            throw JSONRPCError(RPC_TYPE_ERROR, err);
        }
    }
}

CAmount AmountFromValue(const UniValue& value)
{
    if (!value.isNum() && !value.isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");
    CAmount amount;
    if (!ParseFixedPoint(value.getValStr(), COIN_DECIMALS, &amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
    if (!MoneyRange(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount out of range");
    return amount;
}

UniValue ValueFromAmount(const CAmount& amount)
{
    const bool bSign = amount < 0;
    const int64_t n_abs = (bSign ? -amount : amount);
    const int64_t quotient = n_abs / COIN;
    const int64_t remainder = n_abs % COIN;
    return UniValue(UniValue::VNUM, strprintf("%s%d.%05d", bSign ? "-" : "", quotient, remainder));
}

uint256 ParseHashV(const UniValue& v, string strName)
{
    string strHex;
    if (v.isStr())
        strHex = v.get_str();
    if (!IsHex(strHex)) // Note: IsHex("") is false
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    uint256 result;
    result.SetHex(strHex);
    return result;
}

uint256 ParseHashO(const UniValue& o, string strKey)
{
    return ParseHashV(find_value(o, strKey), strKey);
}

v_uint8 ParseHexV(const UniValue& v, string strName)
{
    string strHex;
    if (v.isStr())
        strHex = v.get_str();
    if (!IsHex(strHex))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strName+" must be hexadecimal string (not '"+strHex+"')");
    return ParseHex(strHex);
}

v_uint8 ParseHexO(const UniValue& o, string strKey)
{
    return ParseHexV(find_value(o, strKey), strKey);
}

/**
 * Note: This interface may still be subject to change.
 */

string CRPCTable::help(const string& strCommand) const
{
    ostringstream strRet;
    string sCategory, strHelp;
    set<rpcfn_type> setDone;
    vector<pair<string, const CRPCCommand*> > vCommands;
    vCommands.reserve(mapCommands.size());

    // Build a sorted list of commands (category-command_name)
    for (const auto& [sCmdName, pcmd] : mapCommands)
        vCommands.emplace_back(pcmd->category + "-" + sCmdName, pcmd);
    sort(vCommands.begin(), vCommands.end());

    for (const auto &[_, pcmd] : vCommands)
    {
        const auto &strMethod = pcmd->name;
        // We already filter duplicates, but these deprecated screw up the sort order
        if (strMethod.find("label") != string::npos)
            continue;
        if ((!strCommand.empty() || pcmd->category == "hidden") && strMethod != strCommand)
            continue;
        try
        {
            UniValue params;
            rpcfn_type pfn = pcmd->actor;
            if (setDone.insert(pfn).second)
                (*pfn)(params, true);
        }
        catch (const exception& e)
        {
            // Help text is returned in an exception
            strHelp = string(e.what());
            if (strCommand.empty())
            {
                const size_t nNewlinePos = strHelp.find('\n');
                if (nNewlinePos != string::npos)
                    strHelp = strHelp.substr(0, nNewlinePos);

                if (!str_icmp(sCategory, pcmd->category))
                {
                    if (!sCategory.empty())
                        strRet << "\n";
                    sCategory = lowerstring_first_capital(pcmd->category);
                    strRet << "== " << sCategory << " ==\n";
                }
            }
            strRet << strHelp << "\n";
        }
    }
    if (strRet.str().empty())
        strRet << "help: unknown command: " << strCommand << "\n";

    string sHelp = strRet.str();
    if (!sHelp.empty())
        sHelp.pop_back();
    return sHelp;
}

UniValue help(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
R"(help ( "command" )

List all commands, or get help for a specified command.

Arguments:
1. "command"     (string, optional) The command to get help on

Result:
"text"     (string) The help text
)");

    string strCommand;
    if (params.size() > 0)
        strCommand = params[0].get_str();

    return tableRPC.help(strCommand);
}


UniValue stop(const UniValue& params, bool fHelp)
{
    // Accept the deprecated and ignored 'detach' boolean argument
    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(stop

Stop Pastel server.
)"
);
    // Event loop will exit after current HTTP requests have been handled, so
    // this reply will get back to the client.
    StartShutdown();
    return "Pastel server stopping";
}

/**
 * Call Table
 */
static const CRPCCommand vRPCCommands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    /* Overall control/query calls */
    { "control",            "help",                   &help,                   true  },
    { "control",            "stop",                   &stop,                   true  },
};

CRPCTable::CRPCTable()
{
    unsigned int vcidx;
    for (vcidx = 0; vcidx < (sizeof(vRPCCommands) / sizeof(vRPCCommands[0])); vcidx++)
    {
        const CRPCCommand *pcmd;

        pcmd = &vRPCCommands[vcidx];
        mapCommands[pcmd->name] = pcmd;
    }
}

const CRPCCommand *CRPCTable::operator[](const string &name) const noexcept
{
    auto it = mapCommands.find(name);
    if (it == mapCommands.cend())
        return nullptr;
    return it->second;
}

bool CRPCTable::appendCommand(const string& name, const CRPCCommand* pcmd)
{
    if (IsRPCRunning())
        return false;

    // don't allow overwriting for now
    auto it = mapCommands.find(name);
    if (it != mapCommands.cend())
        return false;

    mapCommands[name] = pcmd;
    return true;
}

bool StartRPC()
{
    LogFnPrint("rpc", "Starting RPC");
    fRPCRunning = true;
    g_rpcSignals.Started();

    // Launch one async rpc worker.  The ability to launch multiple workers is not recommended at present and thus the option is disabled.
    getAsyncRPCQueue()->addWorker();
/*
    int n = GetArg("-rpcasyncthreads", 1);
    if (n<1) {
        LogPrintf("ERROR: Invalid value %d for -rpcasyncthreads.  Must be at least 1.\n", n);
        strerr = strprintf(_("An error occurred while setting up the Async RPC threads, invalid parameter value of %d (must be at least 1)."), n);
        uiInterface.ThreadSafeMessageBox(strerr, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return;
    }
    for (int i = 0; i < n; i++)
        getAsyncRPCQueue()->addWorker();
*/
    return true;
}

void InterruptRPC()
{
    LogFnPrint("rpc", "Interrupting RPC");
    // Interrupt e.g. running longpolls
    fRPCRunning = false;
}

void StopRPC()
{
    LogFnPrint("rpc", "Stopping RPC");
    deadlineTimers.clear();
    g_rpcSignals.Stopped();

    // Tells async queue to cancel all operations and shutdown.
    LogFnPrintf("waiting for async rpc workers to stop");
    getAsyncRPCQueue()->closeAndWait();
}

bool IsRPCRunning()
{
    return fRPCRunning;
}

void SetRPCWarmupStatus(const string& newStatus)
{
    LOCK(cs_rpcWarmup);
    rpcWarmupStatus = newStatus;
}

void SetRPCWarmupFinished()
{
    LOCK(cs_rpcWarmup);
    assert(fRPCInWarmup);
    fRPCInWarmup = false;
}

bool RPCIsInWarmup(string *outStatus)
{
    LOCK(cs_rpcWarmup);
    if (outStatus)
        *outStatus = rpcWarmupStatus;
    return fRPCInWarmup;
}

void JSONRequest::parse(const UniValue& valRequest)
{
    // Parse request
    if (!valRequest.isObject())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid Request object");
    const UniValue& request = valRequest.get_obj();

    // Parse id now so errors from here on will have the id
    m_id = find_value(request, "id");

    // Parse method
    const UniValue valMethod = find_value(request, "method");
    if (valMethod.isNull())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Missing method");
    if (!valMethod.isStr())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Method must be a string");
    m_strMethod = valMethod.get_str();
    if (m_strMethod != "getblocktemplate")
        LogPrint("rpc", "ThreadRPCServer method=%s\n", SanitizeString(m_strMethod));

    // Parse params
    const UniValue valParams = find_value(request, "params");
    if (valParams.isArray())
        m_params = valParams.get_array();
    else if (valParams.isNull())
        m_params = UniValue(UniValue::VARR);
    else
        throw JSONRPCError(RPC_INVALID_REQUEST, "Params must be an array");
}

static UniValue JSONRPCExecOne(const UniValue& req)
{
    UniValue rpc_result(UniValue::VOBJ);

    JSONRequest jreq;
    try {
        jreq.parse(req);

        UniValue result = tableRPC.execute(jreq.method(), jreq.params());
        rpc_result = JSONRPCReplyObj(result, NullUniValue, jreq.id());
    }
    catch (const UniValue& objError)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue, objError, jreq.id());
    }
    catch (const exception& e)
    {
        rpc_result = JSONRPCReplyObj(NullUniValue,
                                     JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id());
    }

    return rpc_result;
}

string JSONRPCExecBatch(const UniValue& vReq)
{
    UniValue ret(UniValue::VARR);
    ret.reserve(vReq.size());
    for (size_t reqIdx = 0; reqIdx < vReq.size(); reqIdx++)
        ret.push_back(JSONRPCExecOne(vReq[reqIdx]));

    return ret.write() + "\n";
}

UniValue CRPCTable::execute(const string &strMethod, const UniValue &params) const
{
    // Return immediately if in warmup
    {
        LOCK(cs_rpcWarmup);
        if (fRPCInWarmup)
            throw JSONRPCError(RPC_IN_WARMUP, rpcWarmupStatus);
    }

    // Find method
    const CRPCCommand *pcmd = tableRPC[strMethod];
    if (!pcmd)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found");

    g_rpcSignals.PreCommand(*pcmd);

    try
    {
        // Execute
        return pcmd->actor(params, false);
    }
    catch (const exception& e)
    {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }

    g_rpcSignals.PostCommand(*pcmd);
}

string HelpExampleCli(const string& methodname, const string& args)
{
    return "> pastel-cli " + methodname + " " + args + "\n";
}

string HelpExampleRpc(const string& methodname, const string& args)
{
    return "> curl --user myusername --data-binary '{\"jsonrpc\": \"1.0\", \"id\":\"curltest\", "
        "\"method\": \"" + methodname + "\", \"params\": [" + args + "] }' -H 'content-type: text/plain;' http://127.0.0.1:9932/\n";
}

string experimentalDisabledHelpMsg(const string& rpc, const string& enableArg)
{
    return "\nWARNING: " + rpc + " is disabled.\n"
        "To enable it, restart pasteld with the -experimentalfeatures and\n"
        "-" + enableArg + " commandline options, or add these two lines\n"
        "to the pastel.conf file:\n\n"
        "experimentalfeatures=1\n"
        + enableArg + "=1\n";
}

string rpcDisabledHelpMsg(const string& rpc, const string& enableArg)
{
    return strprintf(R"(
WARNING: %s is disabled.
To enable it, restart pasteld with the -%s commandline options,
or add this line to the pastel.conf file:

%s=1
)", rpc, enableArg, enableArg);
}

string rpcDisabledInsightExplorerHelpMsg(const string& rpc)
{
    string sDisabledMsg;
    if (!fInsightExplorer)
        sDisabledMsg = rpcDisabledHelpMsg(rpc, "insightexplorer");
    return sDisabledMsg;
}

void rpcDisabledThrowMsg(const bool bFlagToCheck, const string& rpc)
{
    if (bFlagToCheck)
        return;

    throw JSONRPCError(RPC_MISC_ERROR, strprintf(ERRMSG_RPC_DISABLED, rpc, rpc));
}

void RPCRegisterTimerInterface(RPCTimerInterface *iface)
{
    timerInterfaces.push_back(iface);
}

void RPCUnregisterTimerInterface(RPCTimerInterface *iface)
{
    vector<RPCTimerInterface*>::iterator i = find(timerInterfaces.begin(), timerInterfaces.end(), iface);
    assert(i != timerInterfaces.end());
    timerInterfaces.erase(i);
}

void RPCRunLater(const string& name, function<void(void)> func, int64_t nSeconds)
{
    if (timerInterfaces.empty())
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No timer handler registered for RPC");
    deadlineTimers.erase(name);
    RPCTimerInterface* timerInterface = timerInterfaces[0];
    LogPrint("rpc", "queue run of timer %s in %i seconds (using %s)\n", name, nSeconds, timerInterface->Name());
    deadlineTimers.emplace(name, shared_ptr<RPCTimerBase>(timerInterface->NewTimer(func, nSeconds*1000)));
}

CRPCTable tableRPC;

// Return async rpc queue
shared_ptr<AsyncRPCQueue> getAsyncRPCQueue()
{
    return AsyncRPCQueue::sharedInstance();
}

