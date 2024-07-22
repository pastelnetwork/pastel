// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <deque>
#include <thread>
#include <functional>
#include <atomic>

#include <compat.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>
#include <event2/util.h>
#include <event2/http_compat.h>
#include <event2/listener.h>

#include <utils/vector_types.h>
#include <utils/enum_util.h>
#include <utils/util.h>
#include <utils/sync.h>
#include <extlibs/scope_guard.hpp>

#include <httpserver.h>
#include <chainparamsbase.h>
#include <compat.h>
#include <netbase.h>
#include <rpc/protocol.h> // For HTTP status code
#include <ui_interface.h>

#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>
#endif
#endif

using namespace std;

unique_ptr<CHTTPServer> gl_HttpServer;

/** HTTP request method as string - use for logging only */
static string RequestMethodString(const RequestMethod method)
{
    string sMethod;
    switch (method)
    {
    case RequestMethod::GET:
        sMethod = "GET";
        break;

    case RequestMethod::POST:
        sMethod = "POST";
        break;

    case RequestMethod::HEAD:
        sMethod = "HEAD";
        break;

    case RequestMethod::PUT:
        sMethod = "PUT";
        break;

    default:
        sMethod = "unknown";
    }
    return sMethod;
}

HTTPPathHandler::HTTPPathHandler(const std::string& sGroup, const string &sPrefix, 
        const bool bExactMatch, const HTTPRequestHandler &handler) noexcept :
    m_sGroup(sGroup),
    m_sPrefix(sPrefix),
    m_bExactMatch(bExactMatch),
    m_handler(handler)
{}

bool HTTPPathHandler::IsMatch(const string &sURI) const noexcept
{
	if (m_bExactMatch)
		return sURI == m_sPrefix;
	return sURI.find(m_sPrefix) == 0;
}

bool HTTPPathHandler::IsGroup(const string &sGroup) const noexcept
{
	return str_icmp(m_sGroup, sGroup);
}

void accept_error_cb(struct evconnlistener* listener, void* arg)
{
	auto pWorkQueue = static_cast<WorkQueue*>(arg);
	if (!pWorkQueue)
	{
		LogFnPrint("http", "No work queue available");
		return;
	}
	int nError = EVUTIL_SOCKET_ERROR();
	LogFnPrintf("Got an error %d (%s) on the listener", nError, evutil_socket_error_to_string(nError));
}

void accept_connection_cb(struct evconnlistener* listener, evutil_socket_t client_socket, struct sockaddr* addr, int addrlen, void* arg)
{
    auto pWorkQueue = static_cast<WorkQueue*>(arg);
    if (!pWorkQueue)
    {
		LogFnPrint("http", "No work queue available");
		evutil_closesocket(client_socket);
		return;
	}

    LogFnPrint("http", "Accepted connection (fd %" PRId64 ")", client_socket);
    auto pHttpConnection = make_unique<CHttpConnection>(client_socket, addr, addrlen);
    auto [rejectedConnection, nQueueSize] = pWorkQueue->EnqueueConnection(std::move(pHttpConnection));
    if (rejectedConnection)
	{
		LogFnPrintf("Work queue size %zu exceeded, rejecting request", nQueueSize);
        evutil_closesocket(client_socket);
	}
    else if (nQueueSize > 10)
    {
        static atomic_int64_t nLastLogTime(0);
        time_t nNow = time(nullptr);
        // log only every 60 seconds
        if (nNow - nLastLogTime.load() > 30)
        {
            nLastLogTime.store(nNow);
            LogFnPrintf("Work queue size %zu", nQueueSize);
        }
    }
}

/** Callback to close HTTP connection after request is processed. */
static void http_connection_close_cb(struct evhttp_connection* evcon, void* arg)
{
	auto pHttpWorkerContext = static_cast<CHttpWorkerContext*>(arg);
    if (!pHttpWorkerContext)
    {
        LogFnPrintf("ERROR ! No worker context available in the closing http connection");
        return;
    }
    auto bev = evhttp_connection_get_bufferevent(evcon);
    if (!bev)
    {
        LogFnPrintf("ERROR ! No bufferevent available in the closing http connection");
        return;
    }
    auto clientSocket = bufferevent_getfd(bev);
    pHttpWorkerContext->CloseHttpConnection(clientSocket, false);
}

/** libevent event log callback */
static void libevent_log_cb(int severity, const char *msg)
{
#ifndef EVENT_LOG_WARN
// EVENT_LOG_WARN was added in 2.0.19; but before then _EVENT_LOG_WARN existed.
# define EVENT_LOG_WARN _EVENT_LOG_WARN
#endif
    if (severity >= EVENT_LOG_WARN) // Log warn messages and higher without debug category
        LogPrintf("libevent: %s\n", msg);
    else
        LogPrint("libevent", "libevent: %s\n", msg);
}

static void httpevent_callback_fn(evutil_socket_t, short, void* data)
{
    // Static handler: simply call inner handler
    HTTPEvent *self = static_cast<HTTPEvent*>(data);
    if (self)
    {
        self->callHandler();
        if (self->DeleteWhenTriggered())
            delete self;
    }
}

HTTPEvent::HTTPEvent(struct event_base* base, bool bDeleteWhenTriggered, 
        const function<void(void)>& handler):
    m_bDeleteWhenTriggered(bDeleteWhenTriggered),
    handler(handler)
{
    ev = event_new(base, -1, 0, httpevent_callback_fn, this);
    assert(ev);
}

HTTPEvent::~HTTPEvent()
{
    event_free(ev);
}

void HTTPEvent::trigger(struct timeval* tv)
{
    if (!tv)
        event_active(ev, 0, 0); // immediately trigger event in main thread
    else
        evtimer_add(ev, tv); // trigger after timeval passed
}

CHTTPServer::CHTTPServer() noexcept :
    CServiceThread("httplsnr"),
    m_bInitialized(false),
    m_bShuttingDown(false),
    m_pMainEventBase(nullptr),
    m_nRpcWorkerThreads(DEFAULT_HTTP_WORKER_THREADS),
    m_nRpcServerTimeout(DEFAULT_HTTP_SERVER_TIMEOUT_SECS),
    m_nAcceptBackLog(DEFAULT_HTTP_SERVER_ACCEPT_BACKLOG)
{}

bool CHTTPServer::Initialize()
{
	if (m_bInitialized)
		return true;

    // read and validate HTTP server options
    const int64_t nRpcThreads = max<int64_t>(GetArg("-rpcthreads", DEFAULT_HTTP_WORKER_THREADS), 1);
    if ((nRpcThreads < 1) || (nRpcThreads > MAX_HTTP_THREADS))
    {
        m_sInitError = strprintf("Invalid number of RPC threads specified (must be between 1 and %d)", 
            MAX_HTTP_THREADS);
		return false;
    }
    m_nRpcWorkerThreads = static_cast<size_t>(nRpcThreads);

    const int64_t nRpcServerTimeout = GetArg("-rpcservertimeout", DEFAULT_HTTP_SERVER_TIMEOUT_SECS);
    if (nRpcServerTimeout > numeric_limits<int>::max())
    {
        m_sInitError = strprintf("'rpcservertimeout' parameter value [%" PRId64 "] is out of range (0..%d)", 
            nRpcServerTimeout, numeric_limits<int>::max());
        return false;
    }
    m_nRpcServerTimeout = static_cast<int>(nRpcServerTimeout);

    const int64_t nWorkQueueMaxSize = max<int64_t>(GetArg("-rpcworkqueue", DEFAULT_HTTP_WORKQUEUE_MAX_SIZE), MIN_HTTP_WORKQUEUE_MAX_SIZE);
    if (nWorkQueueMaxSize > numeric_limits<int>::max())
    {
        m_sInitError = strprintf("'-rpcworkqueue' parameter value [%" PRId64 "] is out of range (%d..%d)",
            MIN_HTTP_WORKQUEUE_MAX_SIZE, numeric_limits<int>::max());
        return false;
    }
    m_nWorkQueueMaxSize = static_cast<size_t>(nWorkQueueMaxSize);

    const int64_t nAcceptBackLog = GetArg("-rpcacceptbacklog", DEFAULT_HTTP_SERVER_ACCEPT_BACKLOG);
    if (nAcceptBackLog > numeric_limits<int>::max())
	{
		m_sInitError = strprintf("'-rpcacceptbacklog' parameter value [%" PRId64 "] is out of range (0..%d)",
			nAcceptBackLog, numeric_limits<int>::max());
		return false;
	}
    m_nAcceptBackLog = static_cast<int>(nAcceptBackLog);

    if (!InitHTTPAllowList())
        return false;

    if (GetBoolArg("-rpcssl", false))
    {
        uiInterface.ThreadSafeMessageBox(
            "SSL mode for RPC (-rpcssl) is no longer supported.",
            "", CClientUIInterface::MSG_ERROR);
        return false;
    }

    // Redirect libevent's logging to our own log
    event_set_log_callback(&libevent_log_cb);
#if LIBEVENT_VERSION_NUMBER >= 0x02010100
    // If -debug=libevent, set full libevent debugging.
    // Otherwise, disable all libevent debugging.
    if (LogAcceptCategory("libevent"))
        event_enable_debug_logging(EVENT_DBG_ALL);
    else
        event_enable_debug_logging(EVENT_DBG_NONE);
#endif
    event_set_fatal_callback([](int err) { LogPrintf("libevent: FATAL ERROR %d\n", err); });
#ifdef WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
    LogFnPrintf("Using libevent version %s", event_get_version());
    m_pMainEventBase = event_base_new();
    if (!m_pMainEventBase)
    {
        m_sInitError = "Couldn't create an event_base";
        return false;
    }

    LogFnPrintf("HTTP: creating work queue with max size %zu", m_nWorkQueueMaxSize);
    if (!m_pWorkQueue)
    {
        m_pWorkQueue = make_shared<WorkQueue>(*this, m_nWorkQueueMaxSize);
        if (!m_pWorkQueue)
		{
			m_sInitError = "Failed to create work queue";
			return false;
		}
    }

    if (!BindAddresses())
    {
        m_sInitError = strprintf("Unable to bind any endpoint for RPC server. %s", m_sInitError);
        return false;
    }

    LogFnPrint("http", "Initialized HTTP server");
	m_bInitialized = true;
	return m_bInitialized;
}

void CHTTPServer::execute()
{
    try
    {
        event_base_dispatch(m_pMainEventBase);
    } catch (const exception& e)
	{
		LogFnPrintf("exception in http listener event loop: %s", e.what());
	} catch (...)
	{
		LogFnPrintf("unknown exception in http listener event loop");
	}
    // Event loop will be interrupted by Interrupt() call
}

bool CHTTPServer::Start() noexcept
{
    try
    {
        if (!m_bInitialized)
        {
            m_sInitError = "HTTP server not initialized";
            return false;
        }
        LogFnPrintf("HTTP Server: starting %zu worker threads", m_nRpcWorkerThreads);

        string error;
        if (!start(error))
        {
            m_sInitError = strprintf("Failed to start HTTP Server listener thread. %s", error);
            return false;
        }

        for (size_t i = 0; i < m_nRpcWorkerThreads; i++)
        {
            const size_t nID = m_WorkerThreadPool.add_func_thread(error, strprintf("httpworker%zu", i + 1).c_str(),
                [this, i]()
            {
                if (m_pWorkQueue)
                    m_pWorkQueue->worker(i + 1);
            }, true);
            if (nID == INVALID_THREAD_OBJECT_ID)
            {
                m_sInitError = strprintf("Failed to create HTTP worker thread. %s", error);
                return false;
            }
        }
    } catch (const exception& e)
    {
        m_sInitError = strprintf("Exception starting HTTP server: %s", e.what());
		return false;
    } catch (...) {
		m_sInitError = "Unknown exception starting HTTP server";
		return false;
	}
    return true;
}

void CHTTPServer::Interrupt()
{
    LogFnPrintf("Stopping HTTP server");
    m_bShuttingDown = true;
    stop();
    if (m_pMainEventBase)
    {
        // disable all listeners
        for (auto listener : m_vListeners)
            evconnlistener_disable(listener);

        // add small delay to allow pending requests to finish without error
        // also to make sure stop() command sends back a response
        this_thread::sleep_for(chrono::milliseconds(200));

        // Break the main event loop
        event_base_loopexit(m_pMainEventBase, nullptr);
	}
    m_WorkerThreadPool.stop_all();
    if (m_pWorkQueue)
        m_pWorkQueue->Interrupt();
}

void CHTTPServer::Stop()
{
    LogFnPrintf("Stopping HTTP server");
    LogFnPrint("http", "Waiting for HTTP worker threads to exit");
    m_WorkerThreadPool.join_all();
    m_pWorkQueue.reset();
    
    LogFnPrint("http", "Waiting for HTTP event thread to exit");
    waitForStop();
    if (m_pMainEventBase)
    {
        for (auto listener : m_vListeners)
			evconnlistener_free(listener);
		m_vListeners.clear();
		event_base_free(m_pMainEventBase);
		m_pMainEventBase = nullptr;
    }
    LogFnPrintf("Stopped HTTP server");
}

/** Check if a network address is allowed to access the HTTP server */
bool CHTTPServer::IsClientAllowed(const CNetAddr& netaddr) const
{
    if (!netaddr.IsValid())
        return false;
    for (const auto& subnet : m_rpc_allow_subnets)
    {
        if (subnet.Match(netaddr))
            return true;
    }
    return false;
}

/** Initialize ACL list for HTTP server */
bool CHTTPServer::InitHTTPAllowList()
{
    m_rpc_allow_subnets.clear();
    m_rpc_allow_subnets.push_back(CSubNet("127.0.0.0/8")); // always allow IPv4 local subnet
    m_rpc_allow_subnets.push_back(CSubNet("::1"));         // always allow IPv6 localhost
    if (mapMultiArgs.count("-rpcallowip"))
    {
        const auto& vAllow = mapMultiArgs["-rpcallowip"];
        for (const auto &strAllow : vAllow)
        {
            CSubNet subnet(strAllow);
            if (!subnet.IsValid())
            {
                uiInterface.ThreadSafeMessageBox(
                    strprintf("Invalid -rpcallowip subnet specification: %s. Valid are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24).", strAllow),
                    "", CClientUIInterface::MSG_ERROR);
                return false;
            }
            m_rpc_allow_subnets.push_back(subnet);
        }
    }
    string strAllowed;
    for (const auto& subnet : m_rpc_allow_subnets)
        strAllowed += subnet.ToString() + " ";
    LogFnPrint("http", "Allowing HTTP connections from: %s", strAllowed);
    return true;
}

/** Bind HTTP server to specified addresses */
bool CHTTPServer::BindAddresses()
{
    const int64_t nPortParam = GetArg("-rpcport", BaseParams().RPCPort());
    vector<pair<string, uint16_t> > endpoints;

    if ((nPortParam > numeric_limits<uint16_t>::max()) || (nPortParam < 0))
    {
        m_sInitError = strprintf("'rpcport' parameter value [%" PRId64 "] is out of range (0..%hu)", 
            nPortParam, numeric_limits<uint16_t>::max());
        return false;
    }
    const uint16_t defaultPort = static_cast<uint16_t>(nPortParam);
    // Determine what addresses to bind to
    if (!mapArgs.count("-rpcallowip")) // Default to loopback if not allowing external IPs
    { 
        endpoints.push_back(make_pair("::1", defaultPort));
        endpoints.push_back(make_pair("127.0.0.1", defaultPort));
        if (mapArgs.count("-rpcbind"))
        {
            LogFnPrintf("WARNING: option -rpcbind was ignored because -rpcallowip was not specified, refusing to allow everyone to connect");
        }
    } else if (mapArgs.count("-rpcbind")) { // Specific bind address
        const v_strings& vbind = mapMultiArgs["-rpcbind"];
        for (const auto &sHostPort : vbind)
        {
            uint16_t port = defaultPort;
            string host, error;
            if (!SplitHostPort(error, sHostPort, port, host))
            {
                m_sInitError = strprintf("Invalid format for 'rpcbind' parameter. %s", error);
                return false;
            }
            endpoints.emplace_back(host, port);
        }
    }
    else // No specific bind address specified, bind to any
    { 
        endpoints.emplace_back("::", defaultPort);
        endpoints.emplace_back("0.0.0.0", defaultPort);
    }

    // Create listeners
    m_vListeners.clear();
    m_vListeners.reserve(endpoints.size());
    string sListenerInitError;
    for (const auto &[address, port] : endpoints)
    {
        LogFnPrint("http", "Binding RPC on address %s port %hu", address, port);
        evconnlistener* listener = nullptr;

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        // check for IPv4 address
        int nRet = inet_pton(sin.sin_family, address.c_str(), &sin.sin_addr);
        if (nRet == 1)
        {
            listener = evconnlistener_new_bind(m_pMainEventBase, accept_connection_cb, m_pWorkQueue.get(),
			    LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, m_nAcceptBackLog, (struct sockaddr*)&sin, sizeof(sin));
        }
        else if (nRet == 0)
        {
            struct sockaddr_in6 sin6;
            memset(&sin6, 0, sizeof(sin6));
            sin6.sin6_family = AF_INET6;
            sin6.sin6_port = htons(port);
            nRet = inet_pton(sin6.sin6_family, address.c_str(), &sin6.sin6_addr);
            if (nRet == 1)
			{
				listener = evconnlistener_new_bind(m_pMainEventBase, accept_connection_cb, m_pWorkQueue.get(),
					LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, m_nAcceptBackLog, (struct sockaddr*)&sin6, sizeof(sin6));
			}
        }
        if (nRet != 1)
		{
            str_append_field(sListenerInitError, strprintf("Invalid address %s", address).c_str(), ". ");
			continue;
		}

        if (!listener)
        {
			str_append_field(sListenerInitError, strprintf("Binding RPC on address %s port %hu failed. %s",
                address, port, SAFE_SZ(evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()))).c_str(), ". ");
			continue;
		}
        evconnlistener_set_error_cb(listener, accept_error_cb);
        m_vListeners.push_back(listener);
        LogFnPrintf("HTTP RPC Server is listening on address %s port %hu", address, port);
    }
    if (m_vListeners.empty())
    {
		m_sInitError = strprintf("Failed to bind any endpoint for RPC server. %s", sListenerInitError);
		return false;
	}
    return !m_vListeners.empty();
}

void CHTTPServer::RegisterHTTPHandler(const string& sHandlerGroup, const string& sPrefix, const bool bExactMatch, const HTTPRequestHandler &handler)
{
    LogFnPrint("http", "[%s] registering HTTP handler for %s (exactmatch %d)", 
        sHandlerGroup, sPrefix, bExactMatch);
    m_vPathHandlers.emplace_back(sHandlerGroup, sPrefix, bExactMatch, handler);
}

void CHTTPServer::UnregisterHTTPHandlers(const string& sHandlerGroup)
{
    LogFnPrint("http", "Unregistering %s HTTP handlers", sHandlerGroup);
    auto it = m_vPathHandlers.begin();
    while (it != m_vPathHandlers.end())
	{
        if (!it->IsGroup(sHandlerGroup))
            ++it;

        it = m_vPathHandlers.erase(it);
	}
}

bool CHTTPServer::FindHTTPHandler(const string& sURI, string &sPath, HTTPRequestHandler& handler) const noexcept
{
    bool bFoundMatch = false;
    for (const auto& pathHandler : m_vPathHandlers)
	{
		if (pathHandler.IsMatch(sURI))
		{
			handler = pathHandler.GetHandler();
            sPath = sURI.substr(pathHandler.GetPrefixSize());
			bFoundMatch = true;
			break;
		}
	}
    return bFoundMatch;
}

void ReplyInternalServerError(struct evhttp_request* req, const char *szErrorDesc)
{
    if (!req)
        return;
    LogFnPrintf("HTTP Internal server error, status %d. %s", 
        to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), SAFE_SZ(szErrorDesc));
	evhttp_send_error(req, to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Internal server error");
}

/**
 * HTTP request callback.
 * This is called when a new HTTP request is received.
 * Two possible scenarios:
 *  1) connection was just accepted in the main listener thread,
 * passed via queue to worker thread and it called evhttp_get_request
 * to setup the request object.
 *  2) connection has keep-alive header and worker event base loop just received 
 * new http request over the existing connection.
 * 
 * \param req - eventlib HTTP request object
 * \param arg - pointer to the WorkQueue<HTTPRequest>
 */
static void http_request_cb(struct evhttp_request* req, void* arg)
{
    try
    {
        if (!req)
        {
            LogFnPrint("http", "Invalid HTTP request");
            return;
        }
        auto pHttpWorkerContext = static_cast<CHttpWorkerContext*>(arg);
        if (!pHttpWorkerContext)
        {
            ReplyInternalServerError(req, "No worker context available");
            return;
        }
        // get evhttp connection
        auto evcon = evhttp_request_get_connection(req);
        if (!evcon)
        {
            ReplyInternalServerError(req, "No evhttp connection available");
            return;
        }
        auto bev = evhttp_connection_get_bufferevent(evcon);
        if (!bev)
        {
            ReplyInternalServerError(req, "No bufferevent available");
            return;
        }
        const auto clientSocket = bufferevent_getfd(bev);
        if (clientSocket < 0)
        {
            ReplyInternalServerError(req, "Invalid client socket");
            return;
        }
        auto pHttpConnection = pHttpWorkerContext->GetHttpConnection(clientSocket);
        if (!pHttpConnection)
        {
            ReplyInternalServerError(req, strprintf("No HTTP connection object available for fd %" PRId64, clientSocket).c_str());
            return;
        }
        const size_t nWorkerID = pHttpWorkerContext->GetWorkerID();

        // create local HTTPRequest object
        auto pHttpRequest = make_unique<HTTPRequest>(req, pHttpConnection);
        if (!pHttpConnection->ValidateClientConnection(pHttpWorkerContext, pHttpRequest, evcon))
        {
            pHttpWorkerContext->CloseHttpConnection(clientSocket, true);
            return;
        }

        const CService& peer = pHttpConnection->GetPeer();
        const RequestMethod method = pHttpRequest->GetRequestMethod();
        const string sURI = pHttpRequest->GetURI();
        LogPrint("http", "[httpworker #%zu] Received a %s request for %s from %s (fd %" PRId64 ")\n",
            nWorkerID, RequestMethodString(method), sURI, peer.ToString(), clientSocket);

        if (sURI.size() > MAX_URI_LENGTH)
        {
            pHttpRequest->WriteReply(HTTPStatusCode::URI_TOO_LONG);
			return;
        }

        // Early reject unknown HTTP methods
        if (method == RequestMethod::UNKNOWN)
        {
            pHttpRequest->WriteReply(HTTPStatusCode::BAD_METHOD);
            return;
        }
        // Find registered handler by URI prefix, dispatch to worker thread
        string sPath;
        HTTPRequestHandler fnRequestHandler;
        if (!gl_HttpServer->FindHTTPHandler(sURI, sPath, fnRequestHandler))
        {
            LogFnPrint("http", "No handler found for '%s'", sURI);
            pHttpRequest->WriteReply(HTTPStatusCode::NOT_FOUND);
        }
        pHttpRequest->SetRequestHandler(sPath, fnRequestHandler);

        // Process the request
        pHttpRequest->execute();
        pHttpRequest->Cleanup();

        LogPrint("http", "[httpworker #%zu] Finished processing HTTP request (fd %" PRId64 ")\n",
            nWorkerID, clientSocket);
    } catch (const exception& e)
	{
		LogFnPrintf("Exception in HTTP request callback: %s", e.what());
        ReplyInternalServerError(req, e.what());
    } catch (...)
    {
        LogFnPrintf("Unknown exception in HTTP request callback");
        ReplyInternalServerError(req, "Unknown exception");
    }
}

/** Callback to reject HTTP requests after shutdown. */
static void http_reject_request_cb(struct evhttp_request* req, void*)
{
    LogFnPrint("http", "Rejecting request while shutting down");
    evhttp_send_error(req, to_integral_type(HTTPStatusCode::SERVICE_UNAVAILABLE), "pasteld is shutting down");
}

CHttpWorkerContext::CHttpWorkerContext(const size_t nWorkerID) noexcept :
    m_base(nullptr),
    m_http(nullptr),
    m_bEvent(false),
    m_bInEventLoop(false),
    m_nWorkerID(nWorkerID),
    CServiceThread(strprintf("httpevloop%zu", nWorkerID).c_str())
{}

CHttpWorkerContext::~CHttpWorkerContext()
{
    waitForStop();
	DestroyEventLoop();
}

bool CHttpWorkerContext::Initialize(string &error)
{
    m_sLoopName = strprintf("http-evloop #%zu", m_nWorkerID);
    m_base = event_base_new();
    if (!m_base)
    {
        error = strprintf("Couldn't create an event_base in %s", m_sLoopName);
        return false;
    }
    LogPrint("http", "[%s] created event loop\n", m_sLoopName);
    // Guard the event_base to ensure it is freed on error
    auto guard_event_base = sg::make_scope_guard([&]() noexcept 
    {
        if (m_base)
        {
            event_base_free(m_base);
            m_base = nullptr;
        }
    });
    // Create a new evhttp object to handle requests
	m_http = evhttp_new(m_base);
    if (!m_http)
    {
        error = strprintf("Couldn't create HTTP server in %s", m_sLoopName);
        return false;
    }
    guard_event_base.dismiss();
    LogPrint("http", "[%s] created HTTP server\n", m_sLoopName);

	return true;
}

/**
 * Main event loop for the worker thread.
 */
void CHttpWorkerContext::execute()
{
    while (event_base_got_exit(m_base) == 0)
    {
        m_bInEventLoop = true;
        m_LoopCond.notify_one();

        event_base_loop(m_base, EVLOOP_NO_EXIT_ON_EMPTY);

        if (event_base_got_break(m_base))
        {
            m_bInEventLoop = false;
            m_LoopCond.notify_one();

            WaitForEvent();
        }
    }
    m_bInEventLoop = false;
    m_LoopCond.notify_one();
    LogPrint("http", "[%s] event loop exiting\n", m_sLoopName);
}

/**
 * Destroy the event loop and free resources.
 */
void CHttpWorkerContext::DestroyEventLoop()
{
    if (m_http)
    {
        evhttp_free(m_http);
        m_http = nullptr;
        LogPrint("http", "[%s] destroyed HTTP server\n", m_sLoopName);
    }
    if (m_base)
    {
        event_base_free(m_base);
        m_base = nullptr;
        LogPrint("http", "[%s] destroyed event loop\n", m_sLoopName);
    }
}

void CHttpWorkerContext::stop() noexcept
{
    CServiceThread::stop();
    // Exit the event loop as soon as there are no active events.
    if (m_base)
        event_base_loopexit(m_base, nullptr);
    TriggerEvent();
}

void CHttpWorkerContext::TriggerEvent() noexcept
{
    m_bEvent = true;
    SIMPLE_LOCK(m_mutex);
    m_cond.notify_one();
}

/**
 * Add new HTTP connection to the worker context.
 * 
 * \param pHttpConnection - new HTTP connection object
 */
void CHttpWorkerContext::AddHttpConnection(http_connection_t &&pHttpConnection)
{
    const evutil_socket_t clientSocket = pHttpConnection->GetClientSocket();
    struct sockaddr addr;
    const socklen_t addrlen = pHttpConnection->GetSockAddrParams(&addr);
    int nOne = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nOne), sizeof(nOne));

    // add new http connection to the map - can be accessed in a request callback
    {
        SIMPLE_LOCK(m_ConnectionMapLock);
        m_ConnectionMap.emplace(clientSocket, std::move(pHttpConnection));
        LogPrint("http", "[%s] Added HTTP connection for %s (fd %" PRId64 "), total %zy\n",
			m_sLoopName, m_ConnectionMap[clientSocket]->GetPeer().ToString(), 
            clientSocket, m_ConnectionMap.size());
    }

    // break worker event loop to process new http connection
    event_base_loopbreak(m_base);
    {
        unique_lock<mutex> lock(m_LoopMutex);
        m_LoopCond.wait(lock, [this] { return m_bInEventLoop.load(); });
    }
    evhttp_get_request(m_http, clientSocket, &addr, addrlen);

    // notify the worker event loop that it can enter the event loop again
    TriggerEvent();
}

/**
 * Close HTTP connection and remove it from the worker context.
 * 
 * \param clientSocket
 * \param bCloseSocket
 */
void CHttpWorkerContext::CloseHttpConnection(const evutil_socket_t clientSocket, const bool bCloseSocket)
{
    SIMPLE_LOCK(m_ConnectionMapLock);
	auto it = m_ConnectionMap.find(clientSocket);
    if (it != m_ConnectionMap.end())
    {
        LogPrint("http", "[%s] Closing HTTP connection for %s (fd %" PRId64 "), left %zu\n",
			m_sLoopName, it->second->GetPeer().ToString(), clientSocket,
            m_ConnectionMap.size() - 1);

        m_ConnectionMap.erase(it);

        // close connection socket
        if (bCloseSocket)
        {
            evutil_closesocket(clientSocket);
            LogPrint("http", "[%s] Socket closed (fd %" PRId64 ")\n", 
                m_sLoopName, clientSocket);
        }
    }
}

http_connection_t CHttpWorkerContext::GetHttpConnection(const evutil_socket_t clientSocket)
{
    SIMPLE_LOCK(m_ConnectionMapLock);
	auto it = m_ConnectionMap.find(clientSocket);
	if (it != m_ConnectionMap.end())
		return it->second;
	return nullptr;
}

void CHttpWorkerContext::WaitForEvent()
{
    unique_lock<mutex> lock(m_mutex);
    m_cond.wait(lock, [this] { return m_bEvent.load(); });
    m_bEvent = false;
}

size_t WorkQueue::size() const
{
    SIMPLE_LOCK(cs);
    return m_queue.size();
}

/**
* Enqueue a new http connection.
* 
* \param connection new http connection to enqueue
* \return tuple:
*      - unique_ptr<CHttpConnection> if the queue is full, the original connection object is returned, otherwise nullptr
*      - size_t current size of the queue
*/
tuple<unique_ptr<CHttpConnection>, size_t> WorkQueue::EnqueueConnection(unique_ptr<CHttpConnection> &&connection)
{
    SIMPLE_LOCK(cs);
    size_t nQueueSize = m_queue.size();
    if (nQueueSize >= m_nMaxQueueSize)
        return make_tuple(std::move(connection), nQueueSize);
    m_queue.emplace_back(std::move(connection));
    cond.notify_one();
    return make_tuple(nullptr, nQueueSize + 1);
}

void WorkQueue::worker(const size_t nWorkerID)
{
    LogFnPrintf("HTTP worker thread #%zu started", nWorkerID);
    http_worker_context_t pHttpWorkerContext;
    {
        SIMPLE_LOCK(cs);
        pHttpWorkerContext = make_shared<CHttpWorkerContext>(nWorkerID);
        m_WorkerContextMap.emplace(nWorkerID, pHttpWorkerContext);
    }
	m_bRunning = true;

    string error;
    if (!pHttpWorkerContext->Initialize(error))
    {
        LogFnPrintf("Failed to initialize http worker #%zu context. %s", 
            nWorkerID, error);
        return;
    }
    if (!pHttpWorkerContext->start(error))
	{
		error = strprintf("Failed to start http worker #%zu event loop. %s", 
            nWorkerID, error);
		return;
	}

    auto http = pHttpWorkerContext->GetHttp();
    evhttp_set_timeout(http, m_httpServer.GetRpcServerTimeout());
    evhttp_set_max_headers_size(http, DEFAULT_HTTP_MAX_HEADERS_SIZE);
    evhttp_set_max_body_size(http, MAX_DATA_SIZE);
    evhttp_set_gencb(http, http_request_cb, pHttpWorkerContext.get());

    try
    {
        while (true)
        {
            unique_ptr<CHttpConnection> pHttpConnection;
            {
                unique_lock<mutex> lock(cs);
                cond.wait(lock, [this] { return !m_bRunning || !m_queue.empty(); });
                if (!m_bRunning && m_queue.empty())
                    break;
                pHttpConnection = std::move(m_queue.front());
                m_queue.pop_front();
            }
            if (!pHttpConnection)
            {
                LogPrintf("[httpworker #%zu] Invalid HTTP connection\n", nWorkerID);
                continue;
            }
            LogPrint("http", "[httpworker #%zu] Processing new HTTP connection (fd %" PRId64 ")\n",
                nWorkerID, pHttpConnection->GetClientSocket());

            pHttpWorkerContext->AddHttpConnection(std::move(pHttpConnection));
        }
    } catch (const exception& e)
	{
		LogPrintf("Exception in http worker thread #%zu: %s\n", nWorkerID, e.what());
    } catch (...)
    {
        LogPrintf("Unknown exception in http worker thread #%zu\n", nWorkerID);
    }
    pHttpWorkerContext->waitForStop();
}

CHttpConnection::CHttpConnection(evutil_socket_t clientSocket, const struct sockaddr* addr, socklen_t addrlen) noexcept :
    m_clientSocket(clientSocket),
    m_addrlen(addrlen)
{
    memset(&m_addr, 0, sizeof(m_addr));
    if (addr)
    {
        m_peer.SetSockAddr(addr);
        if (sizeof(m_addr) <= addrlen)
        {
            memcpy(&m_addr, addr, addrlen);
            m_addrlen = addrlen;
        }
    }
}

CHttpConnection::~CHttpConnection()
{
    LogFnPrint("http", "HTTP connection closed for %s (fd %" PRId64 ")", 
        m_peer.ToString(), m_clientSocket);
}

bool CHttpConnection::ValidateClientConnection(CHttpWorkerContext* pHttpWorkerContext, 
    const unique_ptr<HTTPRequest> &pHttpRequest, struct evhttp_connection *evcon)
{
    if (m_bClientValidated)
        return m_bClientIsAllowed;
    m_bClientIsAllowed = gl_HttpServer->IsClientAllowed(m_peer);
    m_bClientValidated = true;
    const size_t nWorkerID = pHttpWorkerContext->GetWorkerID();
    if (!m_bClientIsAllowed)
    {
        LogPrint("http", "[httpworker #%zu] Rejecting connection from %s (fd %" PRId64 ")\n",
            nWorkerID, m_peer.ToString(), m_clientSocket);
        pHttpRequest->WriteReply(HTTPStatusCode::FORBIDDEN);
        return false;
    }
    const auto& [bHeaderPresent, sHeaderValue] = pHttpRequest->GetHeader("Connection");
    m_bUsesKeepAliveConnection = bHeaderPresent && str_icmp(sHeaderValue, "keep-alive");

    LogPrint("http", "[httpworker #%zu] HTTP connection from %s (fd %" PRId64 ") is allowed%s\n",
        nWorkerID, m_peer.ToString(), m_clientSocket, m_bUsesKeepAliveConnection ? ", keep-alive" : "");

    evhttp_connection_set_closecb(evcon, http_connection_close_cb, pHttpWorkerContext);
    return true;
}

const CService &CHttpConnection::GetPeer() const noexcept
{
    return m_peer;
}

socklen_t CHttpConnection::GetSockAddrParams(struct sockaddr* addr) const noexcept
{
	if (addr && m_addrlen > 0)
	{
		memcpy(addr, &m_addr, m_addrlen);
		return m_addrlen;
	}
	return 0;
}

HTTPRequest::HTTPRequest(evhttp_request* req, http_connection_t pHttpConnection) noexcept :
    m_req(req),
    m_pHttpConnection(pHttpConnection),
    m_bReplySent(false)
{}

HTTPRequest::~HTTPRequest()
{
    if (m_req && !m_bReplySent)
    {
        // Keep track of whether reply was sent to avoid request leaks
        LogFnPrintf("Unhandled request");
        WriteReply(HTTPStatusCode::INTERNAL_SERVER_ERROR, "Unhandled request");
    }
}

pair<bool, string> HTTPRequest::GetHeader(const string& hdr) const noexcept
{
    const struct evkeyvalq* headers = evhttp_request_get_input_headers(m_req);
    assert(headers);
    const char* val = evhttp_find_header(headers, hdr.c_str());
    if (val)
        return make_pair(true, val);
    else
        return make_pair(false, "");
}

string HTTPRequest::ReadBody()
{
    struct evbuffer* buf = evhttp_request_get_input_buffer(m_req);
    if (!buf)
        return "";
    size_t size = evbuffer_get_length(buf);
    /** Trivial implementation: if this is ever a performance bottleneck,
     * internal copying can be avoided in multi-segment buffers by using
     * evbuffer_peek and an awkward loop. Though in that case, it'd be even
     * better to not copy into an intermediate string but use a stream
     * abstraction to consume the evbuffer on the fly in the parsing algorithm.
     */
    const char* data = (const char*)evbuffer_pullup(buf, size);
    if (!data) // returns nullptr in case of empty buffer
        return "";
    string rv(data, size);
    evbuffer_drain(buf, size);
    return rv;
}

void HTTPRequest::WriteHeader(const string& hdr, const string& value)
{
    struct evkeyvalq* headers = evhttp_request_get_output_headers(m_req);
    assert(headers);
    evhttp_add_header(headers, hdr.c_str(), value.c_str());
}

void HTTPRequest::WriteReply(HTTPStatusCode httpStatusCode, const string& strReply)
{
    assert(!m_bReplySent && m_req);

    // Send event to the worker thread to send reply message
    struct evbuffer* evbOutput = evhttp_request_get_output_buffer(m_req);
    assert(evbOutput);
    evbuffer_add(evbOutput, strReply.data(), strReply.size());

    const int nStatusCode = to_integral_type(httpStatusCode);
	evhttp_send_reply(m_req, nStatusCode, nullptr, evbOutput);
    if (m_pHttpConnection)
	    LogPrint("http", "Sent reply to %s (fd %" PRId64 "): status %d, output size %zu\n", 
		    GetPeerStr(), m_pHttpConnection->GetClientSocket(), nStatusCode, strReply.size());
	m_bReplySent = true;
}

const string HTTPRequest::GetPeerStr() const noexcept
{
   	return m_pHttpConnection ? m_pHttpConnection->GetPeer().ToString() : string();
}

string HTTPRequest::GetURI()
{
    return evhttp_request_get_uri(m_req);
}

void HTTPRequest::Cleanup()
{
    m_sPath.clear();
    m_RequestHandler = nullptr;
    m_req = nullptr;
    m_bReplySent = false;
}

RequestMethod HTTPRequest::GetRequestMethod() const noexcept
{
    RequestMethod method = RequestMethod::UNKNOWN;
    switch (evhttp_request_get_command(m_req))
    {
        case EVHTTP_REQ_GET:
            method = RequestMethod::GET;
            break;

        case EVHTTP_REQ_POST:
            method = RequestMethod::POST;
            break;

        case EVHTTP_REQ_HEAD:
            method = RequestMethod::HEAD;
            break;

        case EVHTTP_REQ_PUT:
            method = RequestMethod::PUT;
            break;

        default:
            break;
    }
    return method;
}

void HTTPRequest::execute()
{
    if (!m_RequestHandler)
    {
        evutil_socket_t clientSocket = m_pHttpConnection ? m_pHttpConnection->GetClientSocket() : -1;
        if (clientSocket < 0)
			LogPrint("http", "No request handler available\n");
		else
            LogPrint("http", "No request handler available (fd %" PRId64 ")\n", clientSocket);
        return;
    }
    try
    {
        (*m_RequestHandler)(this, m_sPath);
    } catch (const exception& e)
	{
		LogPrintf("Exception in HTTP request handler: %s\n", e.what());
		WriteReply(HTTPStatusCode::INTERNAL_SERVER_ERROR, "Internal server error");
	} catch (...)
	{
		LogPrint("http", "Unknown exception in HTTP request handler\n");
		WriteReply(HTTPStatusCode::INTERNAL_SERVER_ERROR, "Internal server error");
	}
}

void HTTPRequest::SetRequestHandler(const string &sPath, const HTTPRequestHandler& handler)
{
    m_sPath = sPath;
	m_RequestHandler = handler;
}

void RegisterHTTPHandler(const string &sHandlerGroup, const string &sPrefix, const bool bExactMatch, const HTTPRequestHandler &handler)
{
    if (gl_HttpServer)
        gl_HttpServer->RegisterHTTPHandler(sHandlerGroup, sPrefix, bExactMatch, handler);
}

void UnregisterHTTPHandlers(const string &sHandlerGroup)
{
    if (gl_HttpServer)
        gl_HttpServer->UnregisterHTTPHandlers(sHandlerGroup);
}

