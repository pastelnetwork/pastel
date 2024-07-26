// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/str_utils.h>
#include <utils/util.h>
#include <utils/enum_util.h>
#include <utils/utilstrencodings.h>
#include <utils/sync.h>
#include <utils/utiltime.h>
#include <utils/random.h>
#include <httprpc.h>
#include <chainparams.h>
#include <httpserver.h>
#include <key_io.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <ui_interface.h>

using namespace std;

/** WWW-Authenticate to present with 401 Unauthorized response */
static const char* WWW_AUTH_HEADER_DATA = R"(Basic realm="jsonrpc")";

/** Simple one-shot callback timer to be used by the RPC mechanism to e.g.
 * re-lock the wallet.
 */
class HTTPRPCTimer : public RPCTimerBase
{
public:
    HTTPRPCTimer(struct event_base* eventBase, function<void(void)>& func, const int64_t millis) :
        ev(eventBase, false, func)
    {
        struct timeval tv;
        tv.tv_sec = static_cast<decltype(tv.tv_sec)>(millis / 1000);
        tv.tv_usec = (millis % 1000) * 1000;
        ev.trigger(&tv);
    }
private:
    HTTPEvent ev;
};

class HTTPRPCTimerInterface : public RPCTimerInterface
{
public:
    HTTPRPCTimerInterface(struct event_base* base) : 
        base(base)
    {}

    const char* Name() const noexcept override
    {
        return "HTTP";
    }
    RPCTimerBase* NewTimer(function<void(void)>& func, int64_t millis) override
    {
        return new HTTPRPCTimer(base, func, millis);
    }
private:
    struct event_base* base;
};


/* Pre-base64-encoded authentication token */
static string strRPCUserColonPass;
/* Stored RPC timer interface (for unregistration) */
static HTTPRPCTimerInterface* httpRPCTimerInterface = 0;

static void JSONErrorReply(HTTPRequest* req, const UniValue& objError, const UniValue& id)
{
    // Send error reply from json-rpc error object
    HTTPStatusCode status = HTTPStatusCode::INTERNAL_SERVER_ERROR;
    int code = find_value(objError, "code").get_int();

    if (code == RPC_INVALID_REQUEST)
        status = HTTPStatusCode::BAD_REQUEST;
    else if (code == RPC_METHOD_NOT_FOUND)
        status = HTTPStatusCode::NOT_FOUND;

    string strReply = JSONRPCReply(NullUniValue, objError, id);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(status, strReply);
}

static bool RPCAuthorized(const string& strAuth)
{
    if (strRPCUserColonPass.empty()) // Belt-and-suspenders measure if InitRPCAuthentication was not called
        return false;
    if (strAuth.substr(0, 6) != "Basic ")
        return false;
    string strUserPass64 = strAuth.substr(6);
    trim(strUserPass64);
    string strUserPass = DecodeBase64(strUserPass64);
    return TimingResistantEqual(strUserPass, strRPCUserColonPass);
}

static bool HTTPReq_JSONRPC(HTTPRequest* req, const string &)
{
    // JSONRPC handles only POST
    if (req->GetRequestMethod() != RequestMethod::POST)
    {
        req->WriteReply(HTTPStatusCode::BAD_METHOD, "JSONRPC server handles only POST requests");
        return false;
    }
    // Check authorization
    pair<bool, string> authHeader = req->GetHeader("authorization");
    if (!authHeader.first)
    {
        req->WriteHeader("WWW-Authenticate", WWW_AUTH_HEADER_DATA);
        req->WriteReply(HTTPStatusCode::UNAUTHORIZED);
        return false;
    }

    if (!RPCAuthorized(authHeader.second))
    {
        LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", req->GetPeerStr());

        /* Deter brute-forcing
           If this results in a DoS the user really
           shouldn't have their RPC port exposed. */
        MilliSleep(250);

        req->WriteHeader("WWW-Authenticate", WWW_AUTH_HEADER_DATA);
        req->WriteReply(HTTPStatusCode::UNAUTHORIZED);
        return false;
    }

    JSONRequest jreq;
    try {
        // Parse request
        UniValue valRequest;
        if (!valRequest.read(req->ReadBody()))
            throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");

        string strReply;
        // singleton request
        if (valRequest.isObject())
        {
            jreq.parse(valRequest);

            CSimpleTimer timer(true);
            string sMethod = jreq.method();
            UniValue result = tableRPC.execute(sMethod, jreq.params());
            if (sMethod != "getblocktemplate")
                LogPrint("rpc", "RPC method=%s (%s)\n", 
                    SanitizeString(sMethod), timer.elapsed_time_str());
            // Send reply
            strReply = JSONRPCReply(result, NullUniValue, jreq.id());

        // array of requests
        } else if (valRequest.isArray())
            strReply = JSONRPCExecBatch(valRequest.get_array());
        else
            throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");

        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTPStatusCode::OK, strReply);

    } catch (const UniValue& objError)
    {
        JSONErrorReply(req, objError, jreq.id());
        return false;
    } catch (const exception& e) {
        JSONErrorReply(req, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id());
        return false;
    }
    return true;
}

static bool InitRPCAuthentication()
{
    if (mapArgs["-rpcpassword"] == "")
    {
        LogPrintf("No rpcpassword set - using random cookie authentication\n");
        if (!GenerateAuthCookie(&strRPCUserColonPass)) {
            uiInterface.ThreadSafeMessageBox(
                translate("Error: A fatal internal error occurred, see debug.log for details"), // Same message as AbortNode
                "", CClientUIInterface::MSG_ERROR);
            return false;
        }
    } else {
        strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    }
    return true;
}

bool StartHTTPRPC()
{
    LogPrint("rpc", "Starting HTTP RPC server\n");
    if (!InitRPCAuthentication())
        return false;

    RegisterHTTPHandler("jsonrpc", "/", true, HTTPReq_JSONRPC);

    auto base = gl_HttpServer->GetEventBase();
    assert(base);
    httpRPCTimerInterface = new HTTPRPCTimerInterface(base);
    RPCRegisterTimerInterface(httpRPCTimerInterface);
    return true;
}

void InterruptHTTPRPC()
{
    LogPrint("rpc", "Interrupting HTTP RPC server\n");
}

void StopHTTPRPC()
{
    LogFnPrintf("Stopping HTTP RPC server");

    UnregisterHTTPHandlers("jsonrpc");
    if (httpRPCTimerInterface)
    {
        RPCUnregisterTimerInterface(httpRPCTimerInterface);
        delete httpRPCTimerInterface;
        httpRPCTimerInterface = 0;
    }
}
