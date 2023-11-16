// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdio.h>

#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include "support/events.h"
#include <univalue.h>

#include <utils/enum_util.h>
#include <utils/vector_types.h>
#include <utils/util.h>
#include <utils/utilstrencodings.h>
#include <utils/svc_thread.h>
#include <chainparamsbase.h>
#include <clientversion.h>
#include <rpc/client.h>
#include <rpc/protocol.h>
#include <rpc/rpc_consts.h>
#include <port_config.h>

constexpr int DEFAULT_HTTP_CLIENT_TIMEOUT = 900;
constexpr int CONTINUE_EXECUTION = -1;

using namespace std;

string HelpMessageCli()
{
    string strUsage;
    strUsage += HelpMessageGroup(translate("Options:"));
    strUsage += HelpMessageOpt("-?", translate("This help message"));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(translate("Specify configuration file (default: %s)"), "pastel.conf"));
    strUsage += HelpMessageOpt("-datadir=<dir>", translate("Specify data directory"));
    strUsage += HelpMessageOpt("-testnet", translate("Use the test network"));
    strUsage += HelpMessageOpt("-regtest", translate("Enter regression test mode, which uses a special chain in which blocks can be "
                                             "solved instantly. This is intended for regression testing tools and app development."));
    strUsage += HelpMessageOpt("-rpcconnect=<ip>", strprintf(translate("Send commands to node running on <ip> (default: %s)"), "127.0.0.1"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(translate("Connect to JSON-RPC on <port> (default: %u or testnet: %u)"), MAINNET_DEFAULT_RPC_PORT, TESTNET_DEFAULT_RPC_PORT));
    strUsage += HelpMessageOpt("-rpcwait", translate("Wait for RPC server to start"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", translate("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", translate("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcclienttimeout=<n>", strprintf(translate("Timeout in seconds during HTTP requests, or 0 for no timeout. (default: %d)"), DEFAULT_HTTP_CLIENT_TIMEOUT));
    strUsage += HelpMessageOpt("-stdin", translate("Read extra arguments from standard input, one per line until EOF/Ctrl-D (recommended for sensitive information such as passphrases)"));

    return strUsage;
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//

//
// Exception thrown on connection error.  This error is used to determine
// when to wait if -rpcwait is given.
//
class CConnectionFailed : public runtime_error
{
public:

    explicit inline CConnectionFailed(const string& msg) :
        runtime_error(msg)
    {}

};

//
// This function returns either one of EXIT_ codes when it's expected to stop the process or
// CONTINUE_EXECUTION when it's expected to continue further.
//
static int AppInitRPC(int argc, char* argv[])
{
    static_assert(CONTINUE_EXECUTION != EXIT_FAILURE,
                  "CONTINUE_EXECUTION should be different from EXIT_FAILURE");
    static_assert(CONTINUE_EXECUTION != EXIT_SUCCESS,
                  "CONTINUE_EXECUTION should be different from EXIT_SUCCESS");
    //
    // Parameters
    //
    ParseParameters(argc, argv);
    if (argc<2 || mapArgs.count("-?") || mapArgs.count("-h") || mapArgs.count("-help") || mapArgs.count("-version")) {
        string strUsage = translate("Pastel RPC client version") + " " + FormatFullVersion() + "\n" + PrivacyInfo();
        if (!mapArgs.count("-version")) {
            strUsage += "\n" + translate("Usage:") + "\n" +
                  "  pastel-cli [options] <command> [params]  " + translate("Send command to Pastel") + "\n" +
                  "  pastel-cli [options] help                " + translate("List commands") + "\n" +
                  "  pastel-cli [options] help <command>      " + translate("Get help for a command") + "\n";

            strUsage += "\n" + HelpMessageCli();
        } else {
            strUsage += LicenseInfo();
        }

        fprintf(stdout, "%s", strUsage.c_str());
        if (argc < 2) {
            fprintf(stderr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    if (!fs::is_directory(GetDataDir(false))) {
        fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", mapArgs["-datadir"].c_str());
        return EXIT_FAILURE;
    }
    try {
        ReadConfigFile(mapArgs, mapMultiArgs);
    } catch (const exception& e) {
        fprintf(stderr,"Error reading configuration file: %s\n", e.what());
        return EXIT_FAILURE;
    }
    // Check for -testnet or -regtest parameter (BaseParams() calls are only valid after this clause)
    if (!SelectBaseParamsFromCommandLine()) {
        fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
        return EXIT_FAILURE;
    }
    if (GetBoolArg("-rpcssl", false))
    {
        fprintf(stderr, "Error: SSL mode for RPC (-rpcssl) is no longer supported.\n");
        return EXIT_FAILURE;
    }
    return CONTINUE_EXECUTION;
}


/** Reply structure for request_done to fill in */
struct HTTPReply
{
    HTTPReply(): status(0), error(-1) {}

    int status;
    int error;
    string body;
};

const char *http_errorstring(int code)
{
    switch(code) {
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    case EVREQ_HTTP_TIMEOUT:
        return "timeout reached";
    case EVREQ_HTTP_EOF:
        return "EOF reached";
    case EVREQ_HTTP_INVALID_HEADER:
        return "error while reading header, or invalid header";
    case EVREQ_HTTP_BUFFER_ERROR:
        return "error encountered while reading or writing";
    case EVREQ_HTTP_REQUEST_CANCEL:
        return "request was canceled";
    case EVREQ_HTTP_DATA_TOO_LONG:
        return "response body is larger than allowed";
#endif
    default:
        return "unknown";
    }
}

static void http_request_done(struct evhttp_request *req, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply*>(ctx);

    if (!req)
    {
        /* If req is nullptr, it means an error occurred while connecting: the
         * error code will have been passed to http_error_cb.
         */
        reply->status = 0;
        return;
    }

    reply->status = evhttp_request_get_response_code(req);

    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    if (buf)
    {
        size_t size = evbuffer_get_length(buf);
        const char *data = (const char*)evbuffer_pullup(buf, size);
        if (data)
            reply->body = string(data, size);
        evbuffer_drain(buf, size);
    }
}

#if LIBEVENT_VERSION_NUMBER >= 0x02010300
static void http_error_cb(enum evhttp_request_error err, void *ctx)
{
    HTTPReply *reply = static_cast<HTTPReply*>(ctx);
    reply->error = err;
}
#endif

UniValue CallRPC(const string& strMethod, const UniValue& params)
{
    const string host = GetArg("-rpcconnect", "127.0.0.1");
    const int port = static_cast<int>(GetArg("-rpcport", BaseParams().RPCPort()));
    const int nHttpClientTimeout = static_cast<int>(GetArg("-rpcclienttimeout", DEFAULT_HTTP_CLIENT_TIMEOUT));

    // Obtain event base
    raii_event_base base = obtain_event_base();

    // Synchronously look up hostname
    raii_evhttp_connection evcon = obtain_evhttp_connection_base(base.get(), host, port);
    evhttp_connection_set_timeout(evcon.get(), nHttpClientTimeout);

    HTTPReply response;
    raii_evhttp_request req = obtain_evhttp_request(http_request_done, (void*)&response);
    if (!req)
        throw runtime_error("create http request failed");
#if LIBEVENT_VERSION_NUMBER >= 0x02010300
    evhttp_request_set_error_cb(req.get(), http_error_cb);
#endif

    // Get credentials
    string strRPCUserColonPass;
    if (mapArgs["-rpcpassword"] == "") {
        // Try fall back to cookie-based authentication if no password is provided
        if (!GetAuthCookie(&strRPCUserColonPass)) {
            throw runtime_error(strprintf(
                translate("Could not locate RPC credentials. No authentication cookie could be found,\n"
                  "and no rpcpassword is set in the configuration file (%s)."),
                    GetConfigFile().string().c_str()));

        }
    } else {
        strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    }

    struct evkeyvalq* output_headers = evhttp_request_get_output_headers(req.get());
    assert(output_headers);
    evhttp_add_header(output_headers, "Host", host.c_str());
    evhttp_add_header(output_headers, "Connection", "close");
    evhttp_add_header(output_headers, "Authorization", (string("Basic ") + EncodeBase64(strRPCUserColonPass)).c_str());

    // Attach request data
    string strRequest = JSONRPCRequest(strMethod, params, 1);
    struct evbuffer* output_buffer = evhttp_request_get_output_buffer(req.get());
    assert(output_buffer);
    evbuffer_add(output_buffer, strRequest.data(), strRequest.size());

    int r = evhttp_make_request(evcon.get(), req.get(), EVHTTP_REQ_POST, "/");
    req.release(); // ownership moved to evcon in above call
    if (r != 0) {
        throw CConnectionFailed("send http request failed");
    }

    event_base_dispatch(base.get());

    if (response.status == 0)
        throw CConnectionFailed(strprintf("couldn't connect to server: %s (code %d)\n(make sure server is running and you are connecting to the correct RPC port)", http_errorstring(response.error), response.error));
    else if (response.status == to_integral_type(HTTPStatusCode::UNAUTHORIZED))
        throw runtime_error("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (response.status >= 400 && 
             response.status != to_integral_type(HTTPStatusCode::BAD_REQUEST) && 
             response.status != to_integral_type(HTTPStatusCode::NOT_FOUND) && 
             response.status != to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR))
        throw runtime_error(strprintf("server returned HTTP error %d", response.status));
    else if (response.body.empty())
        throw runtime_error("no response from server");

    // Parse reply
    UniValue valReply(UniValue::VSTR);
    if (!valReply.read(response.body))
        throw runtime_error("couldn't parse reply from server");
    const UniValue& reply = valReply.get_obj();
    if (reply.empty())
        throw runtime_error("expected reply to have result, error and id properties");

    return reply;
}

int CommandLineRPC(int argc, char *argv[])
{
    string strPrint;
    int nRet = 0;
    try
    {
        // Skip switches
        while (argc > 1 && IsSwitchChar(argv[1][0]))
        {
            argc--;
            argv++;
        }
        auto args = vector<string>(&argv[1], &argv[argc]);
        if (GetBoolArg("-stdin", false))
        {
            // Read one arg per line from stdin and append
            string line;
            while (getline(cin,line))
                args.push_back(line);
        }
        if (args.size() < 1)
            throw runtime_error("too few parameters (need at least command)");
        const string strMethod = args[0];
        UniValue params = RPCConvertValues(strMethod, v_strings(args.begin()+1, args.end()));

        // Execute and handle connection failures with -rpcwait
        const bool fWait = GetBoolArg("-rpcwait", false);
        do
        {
            try
            {
                const UniValue reply = CallRPC(strMethod, params);

                // Parse reply
                const UniValue& result = find_value(reply, RPC_KEY_RESULT);
                const UniValue& error  = find_value(reply, "error");

                if (!error.isNull())
                {
                    // Error
                    int code = error["code"].get_int();
                    if (fWait && code == RPC_IN_WARMUP)
                        throw CConnectionFailed("server in warmup");
                    strPrint = "error: " + error.write();
                    nRet = abs(code);
                    if (error.isObject())
                    {
                        UniValue errCode = find_value(error, "code");
                        UniValue errMsg  = find_value(error, "message");
                        strPrint = errCode.isNull() ? "" : "error code: "+errCode.getValStr()+"\n";

                        if (errMsg.isStr())
                            strPrint += "error message:\n"+errMsg.get_str();
                    }
                }
                else
                {
                    // Result
                    if (result.isNull())
                        strPrint = "";
                    else if (result.isStr())
                        strPrint = result.get_str();
                    else
                        strPrint = result.write(2);
                }
                // Connection succeeded, no need to retry.
                break;
            }
            catch (const CConnectionFailed&)
            {
                if (fWait)
                    MilliSleep(1000);
                else
                    throw;
            }
        } while (fWait);
    }
    catch (const func_thread_interrupted&)
    {
        throw;
    }
    catch (const exception& e)
    {
        strPrint = string("error: ") + e.what();
        nRet = EXIT_FAILURE;
    }
    catch (...)
    {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
        throw;
    }

    if (strPrint != "")
        fprintf((nRet == 0 ? stdout : stderr), "%s\n", strPrint.c_str());
    return nRet;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();
    if (!SetupNetworking())
    {
        fprintf(stderr, "Error: Initializing networking failed\n");
        return EXIT_FAILURE;
    }

    try
    {
        int ret = AppInitRPC(argc, argv);
        if (ret != CONTINUE_EXECUTION)
            return ret;
    } catch (const exception& e) {
        PrintExceptionContinue(&e, "AppInitRPC()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInitRPC()");
        return EXIT_FAILURE;
    }

    int ret = EXIT_FAILURE;
    try
    {
        ret = CommandLineRPC(argc, argv);
    }
    catch (const exception& e) {
        PrintExceptionContinue(&e, "CommandLineRPC()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "CommandLineRPC()");
    }
    return ret;
}
