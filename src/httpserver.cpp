// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <cinttypes>
#include <deque>
#include <thread>
#include <functional>

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

HTTPPathHandler::HTTPPathHandler(const string &sPrefix, const bool bExactMatch, const HTTPRequestHandler &handler) noexcept :
    m_sPrefix(sPrefix),
    m_bExactMatch(bExactMatch),
    m_handler(handler)
{}

bool HTTPPathHandler::IsMatch(const string &sPathPrefix, const bool bExactMatch) const noexcept
{
	if (m_bExactMatch == bExactMatch)
		return sPathPrefix == m_sPrefix;
	return sPathPrefix.find(m_sPrefix) == 0;
}

bool HTTPPathHandler::IsHandlerMatch(const string& sPathPrefix, const bool bExactMatch) const noexcept
{
	if (m_bExactMatch == bExactMatch)
		return sPathPrefix == m_sPrefix;
    return false;
}

/** Event dispatcher thread */
static void ThreadHTTP(struct event_base* base)
{
    RenameThread("psl-httplsnr");
    LogFnPrint("http", "Entering http event loop");
    event_base_dispatch(base);
    // Event loop will be interrupted by InterruptHTTPServer()
    LogFnPrint("http", "Exited http event loop");
}

void accept_connection_cb(struct evconnlistener* listener, evutil_socket_t client_socket, struct sockaddr* addr, int addrlen, void* arg)
{
    auto pWorkQueue = static_cast<WorkQueue<HTTPRequest>*>(arg);
    if (!pWorkQueue)
    {
		LogFnPrint("http", "No work queue available");
		evutil_closesocket(client_socket);
		return;
	}

    LogFnPrint("http", "Accepted connection, client socket %" PRId64, static_cast<int64_t>(client_socket));
    auto request = make_unique<HTTPRequest>(client_socket, addr, addrlen);
    auto [rejectedRequest, nQueueSize] = pWorkQueue->Enqueue(move(request));
    if (rejectedRequest)
	{
		LogFnPrintf("Work queue size %zu exceeded, rejecting request", nQueueSize);
    	rejectedRequest->WriteReply(HTTPStatusCode::INTERNAL_SERVER_ERROR, 
				strprintf("Work queue size exceeded (%zu)", nQueueSize));
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
	auto pWorkerContext = static_cast<WorkerContext<HTTPRequest>*>(arg);
    if (!pWorkerContext)
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
    auto pHttpRequest = pWorkerContext->ExtractHttpRequest(clientSocket);
	LogPrint("http", "Closing connection for %s (fd %" PRId64 ")\n", 
		pHttpRequest->GetPeer().ToString(), pHttpRequest->GetClientSocket());
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
    m_bInitialized(false),
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
        m_pWorkQueue = make_shared<WorkQueue<HTTPRequest>>(*this, m_nWorkQueueMaxSize);
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

bool CHTTPServer::Start()
{
    if (!m_bInitialized)
    {
		m_sInitError = "HTTP server not initialized";
		return false;
	}
    LogFnPrint("http", "Starting HTTP server");
    LogFnPrintf("HTTP: starting %zu worker threads", m_nRpcWorkerThreads);

    m_MainThread = thread(&ThreadHTTP, m_pMainEventBase);

    string error;
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
    return true;
}

void CHTTPServer::Interrupt()
{
    LogFnPrint("http", "Interrupting HTTP server");
    if (m_pMainEventBase)
    {
        // disable listeners
        for (auto listener : m_vListeners)
            evconnlistener_disable(listener);
        // Break the main event loop
        event_base_loopexit(m_pMainEventBase, nullptr);
	}
    m_WorkerThreadPool.stop_all();
    if (m_pWorkQueue)
        m_pWorkQueue->Interrupt();
}

void CHTTPServer::Stop()
{
    LogFnPrint("http", "Stopping HTTP server");
    LogFnPrint("http", "Waiting for HTTP worker threads to exit");
    m_WorkerThreadPool.join_all();
    m_pWorkQueue.reset();
    
    LogFnPrint("http", "Waiting for HTTP event thread to exit");
    if (m_MainThread.joinable())
        m_MainThread.join();
    if (m_pMainEventBase)
    {
        for (auto listener : m_vListeners)
			evconnlistener_free(listener);
		m_vListeners.clear();
		event_base_free(m_pMainEventBase);
		m_pMainEventBase = nullptr;
    }
    LogFnPrint("http", "Stopped HTTP server");
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
			m_sInitError = strprintf("Invalid address %s", address);
			return false;
		}

        if (!listener)
        {
			m_sInitError = strprintf("Binding RPC on address %s port %hu failed.", address, port);
			return false;
		}
        m_vListeners.push_back(listener);
        LogFnPrintf("HTTP RPC Server is listening on address %s port %hu", address, port);
    }
    return !m_vListeners.empty();
}

void CHTTPServer::RegisterHTTPHandler(const string& sPrefix, const bool bExactMatch, const HTTPRequestHandler &handler)
{
    LogFnPrint("http", "Registering HTTP handler for %s (exactmatch %d)", sPrefix, bExactMatch);
    m_vPathHandlers.emplace_back(sPrefix, bExactMatch, handler);
}

void CHTTPServer::UnregisterHTTPHandler(const string& sPrefix, const bool bExactMatch)
{
    auto it = m_vPathHandlers.begin();
    auto itEnd = m_vPathHandlers.end();
    for (; it != itEnd; ++it)
    {
        if (it->IsHandlerMatch(sPrefix, bExactMatch))
            break;
    }
    if (it != itEnd)
    {
        LogFnPrint("http", "Unregistering HTTP handler for %s (exactmatch %d)", sPrefix, bExactMatch);
        m_vPathHandlers.erase(it);
    }
}

bool CHTTPServer::FindHTTPHandler(const string& sURI, string &sPath, HTTPRequestHandler& handler) const noexcept
{
    bool bFoundMatch = false;
    for (const auto& pathHandler : m_vPathHandlers)
	{
		if (pathHandler.IsMatch(sURI, false))
		{
			handler = pathHandler.GetHandler();
            sPath = sURI.substr(pathHandler.GetPrefixSize());
			bFoundMatch = true;
			break;
		}
	}
    return bFoundMatch;
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
    if (!req)
	{
		LogFnPrint("http", "Invalid HTTP request");
		return;
	}
    auto pWorkerContext = static_cast<WorkerContext<HTTPRequest>*>(arg);
    if (!pWorkerContext)
    {
        LogFnPrint("http", "No worker context available");
        evhttp_send_error(req, to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Internal server error");
        return;
    }
    // get evhttp connection
    auto evcon = evhttp_request_get_connection(req);
    if (!evcon)
	{
		LogFnPrint("http", "No evhttp connection available");
		evhttp_send_error(req, to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Internal server error");
		return;
	}
    auto bev = evhttp_connection_get_bufferevent(evcon);
    if (!bev)
    {
        LogFnPrint("http", "No bufferevent available");
		evhttp_send_error(req, to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Internal server error");
		return;
	}
    const auto clientSocket = bufferevent_getfd(bev);
    if (clientSocket < 0)
	{
		LogFnPrint("http", "Invalid client socket");
        evhttp_send_error(req, to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Internal server error");
        return;
    }
    auto pHttpRequest = pWorkerContext->ExtractHttpRequest(clientSocket);
    if (!pHttpRequest)
    {
		LogFnPrint("http", "No HTTP request object available");
		evhttp_send_error(req, to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Internal server error");
		return;
	}
    const size_t nWorkerID = pWorkerContext->GetWorkerID();
    pHttpRequest->SetEvRequest(req);
    const CService &peer = pHttpRequest->GetPeer();
    const RequestMethod method = pHttpRequest->GetRequestMethod();
    const string sURI = pHttpRequest->GetURI();
    LogFnPrint("http", "Received a %s request for %s from %s (fd %" PRId64 ")",
        RequestMethodString(method), sURI, peer.ToString(), clientSocket);

    bool bUsesKeepAliveConnection = pHttpRequest->UsesKeepAliveConnection();
    // Early address-based allow check
    if (!bUsesKeepAliveConnection && !gl_HttpServer->IsClientAllowed(peer))
    {
        pHttpRequest->WriteReply(HTTPStatusCode::FORBIDDEN);
        return;
    }
    if (!bUsesKeepAliveConnection)
    {
        const auto &[bHeaderPresent, sHeaderValue] = pHttpRequest->GetHeader("Connection");
        if (bHeaderPresent && str_icmp(sHeaderValue, "keep-alive"))
        {
            pHttpRequest->SetUsesKeepAliveConnection();
            bUsesKeepAliveConnection = true;
            LogPrint("http", "[httpworker #%zu] Connection from %s (fd %" PRId64 ") is alive\n",
                nWorkerID, peer.ToString(), clientSocket);
            evhttp_connection_set_closecb(evcon, http_connection_close_cb, pWorkerContext);
        }
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
    (*pHttpRequest)();
    pHttpRequest->Cleanup();

    LogPrint("http", "[httpworker #%zu] Finished processing HTTP request (fd %" PRId64 ")\n", 
        nWorkerID, clientSocket);

    if (bUsesKeepAliveConnection)
        pWorkerContext->AddHttpRequest(clientSocket, move(pHttpRequest));
}

/** Callback to reject HTTP requests after shutdown. */
static void http_reject_request_cb(struct evhttp_request* req, void*)
{
    LogFnPrint("http", "Rejecting request while shutting down");
    evhttp_send_error(req, to_integral_type(HTTPStatusCode::SERVICE_UNAVAILABLE), "pasteld is shutting down");
}

template <typename WorkItem>
WorkerContext<WorkItem>::WorkerContext(const size_t nWorkerID) noexcept :
    m_base(nullptr),
    m_http(nullptr),
    m_bEvent(false),
    m_nWorkerID(nWorkerID),
    CServiceThread(strprintf("httpevloop%zu", nWorkerID).c_str())
{}

template <typename WorkItem>
WorkerContext<WorkItem>::~WorkerContext()
{
    waitForStop();
	DestroyEventLoop();
}

template <typename WorkItem>
bool WorkerContext<WorkItem>::Initialize(string &error)
{
    m_sLoopName = strprintf("HttpWorkerLoop #%zu", m_nWorkerID);
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

template <typename WorkItem>
void WorkerContext<WorkItem>::execute()
{
    while (event_base_got_exit(m_base) == 0)
    {
        event_base_loop(m_base, EVLOOP_NO_EXIT_ON_EMPTY);
        if (event_base_got_break(m_base))
            WaitForEvent();
    }
}

template <typename WorkItem>
void WorkerContext<WorkItem>::DestroyEventLoop()
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

template <typename WorkItem>
void WorkerContext<WorkItem>::stop() noexcept
{
    CServiceThread::stop();
    // Exit the event loop as soon as there are no active events.
    if (m_base)
        event_base_loopexit(m_base, nullptr);
    TriggerEvent();
}

template <typename WorkItem>
void WorkerContext<WorkItem>::TriggerEvent() noexcept
{
    m_bEvent = true;
    SIMPLE_LOCK(m_mutex);
    m_cond.notify_one();
}

template <typename WorkItem>
void WorkerContext<WorkItem>::CreateConnection(std::unique_ptr<WorkItem> &&pWorkItem)
{
    const evutil_socket_t clientSocket = pWorkItem->GetClientSocket();
    struct sockaddr addr;
    const socklen_t addrlen = pWorkItem->GetSockAddrParams(&addr);
    int nOne = 1;
    setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nOne), sizeof(nOne));

    AddHttpRequest(clientSocket, move(pWorkItem));

    event_base_loopbreak(m_base);
    evhttp_get_request(m_http, clientSocket, &addr, addrlen);
    TriggerEvent();
}

template <typename WorkItem>
void WorkerContext<WorkItem>::WaitForEvent()
{
    unique_lock<mutex> lock(m_mutex);
    m_cond.wait(lock, [this] { return m_bEvent.load(); });
    m_bEvent = false;
}


template <typename WorkItem>
void WorkerContext<WorkItem>::AddHttpRequest(const evutil_socket_t clientSocket, unique_ptr<WorkItem> &&request)
{
    SIMPLE_LOCK(m_RequestMapLock);
    m_RequestMap.emplace(clientSocket, std::move(request));
}

template <typename WorkItem>
unique_ptr<WorkItem> WorkerContext<WorkItem>::ExtractHttpRequest(const evutil_socket_t clientSocket)
{
	SIMPLE_LOCK(m_RequestMapLock);
	auto it = m_RequestMap.find(clientSocket);
	if (it != m_RequestMap.end())
	{
		auto request = std::move(it->second);
		m_RequestMap.erase(it);
		return request;
	}
	return nullptr;
}

template <typename WorkItem>
size_t WorkQueue<WorkItem>::size() const
{
    SIMPLE_LOCK(cs);
    return m_queue.size();
}

/**
* Enqueue a work item.
* 
* \param item Work item to enqueue
* \return tuple:
*      - unique_ptr<WorkItem> if the queue is full, the item is returned
*      - size_t current size of the queue
*/
template <typename WorkItem>
tuple<unique_ptr<WorkItem>, size_t> WorkQueue<WorkItem>::Enqueue(unique_ptr<WorkItem> &&item)
{
    SIMPLE_LOCK(cs);
    size_t nQueueSize = m_queue.size();
    if (nQueueSize >= m_nMaxQueueSize)
        return make_tuple(move(item), nQueueSize);
    m_queue.emplace_back(move(item));
    cond.notify_one();
    return make_tuple(nullptr, nQueueSize + 1);
}

template <typename WorkItem>
void WorkQueue<WorkItem>::worker(const size_t nWorkerID)
{
    LogFnPrintf("HTTP worker thread #%zu started", nWorkerID);
    shared_ptr<WorkerContext<WorkItem>> pWorkerContext;
    {
        SIMPLE_LOCK(cs);
        pWorkerContext = make_shared<WorkerContext<WorkItem>>(nWorkerID);
        m_WorkerContextMap.emplace(nWorkerID, pWorkerContext);
    }
	m_bRunning = true;

    string error;
    if (!pWorkerContext->Initialize(error))
    {
        LogFnPrintf("Failed to initialize http worker #%zu context. %s", 
            nWorkerID, error);
        return;
    }
    if (!pWorkerContext->start(error))
	{
		error = strprintf("Failed to start http worker #%zu event loop. %s", 
            nWorkerID, error);
		return;
	}


    auto http = pWorkerContext->GetHttp();
    evhttp_set_timeout(http, m_httpServer.GetRpcServerTimeout());
    evhttp_set_max_headers_size(http, DEFAULT_HTTP_MAX_HEADERS_SIZE);
    evhttp_set_max_body_size(http, MAX_DATA_SIZE);
    evhttp_set_gencb(http, http_request_cb, pWorkerContext.get());

    while (true)
    {
        unique_ptr<HTTPRequest> pHttpRequest;
        {
            unique_lock<mutex> lock(cs);
            cond.wait(lock, [this] { return !m_bRunning || !m_queue.empty(); });
            if (!m_bRunning && m_queue.empty())
                break;
            pHttpRequest = move(m_queue.front());
            m_queue.pop_front();
        }
        // Get the HTTPRequest from the work item
        if (!pHttpRequest)
        {
            LogPrintf("[httpworker #%zu] Invalid HTTP request\n", nWorkerID);
			continue;
        }
        LogPrint("http", "[httpworker #%zu] Processing HTTP request (fd %" PRId64 ")\n",
            nWorkerID, pHttpRequest->GetClientSocket());

        pWorkerContext->CreateConnection(move(pHttpRequest));
    }
    pWorkerContext->waitForStop();
}

HTTPRequest::HTTPRequest(evutil_socket_t clientSocket, struct sockaddr* addr, const socklen_t addrlen) noexcept: 
	m_clientSocket(clientSocket),
    req(nullptr),
    m_bReplySent(false),
    m_addrlen(0),
    m_bUsesKeepAliveConnection(false)
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

HTTPRequest::~HTTPRequest()
{
    if (req && !m_bReplySent)
    {
        // Keep track of whether reply was sent to avoid request leaks
        LogFnPrintf("Unhandled request");
        WriteReply(HTTPStatusCode::INTERNAL_SERVER_ERROR, "Unhandled request");
    }
}

pair<bool, string> HTTPRequest::GetHeader(const string& hdr) const noexcept
{
    const struct evkeyvalq* headers = evhttp_request_get_input_headers(req);
    assert(headers);
    const char* val = evhttp_find_header(headers, hdr.c_str());
    if (val)
        return make_pair(true, val);
    else
        return make_pair(false, "");
}

string HTTPRequest::ReadBody()
{
    struct evbuffer* buf = evhttp_request_get_input_buffer(req);
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
    struct evkeyvalq* headers = evhttp_request_get_output_headers(req);
    assert(headers);
    evhttp_add_header(headers, hdr.c_str(), value.c_str());
}

void HTTPRequest::WriteReply(HTTPStatusCode httpStatusCode, const string& strReply)
{
    assert(!m_bReplySent && req);

    // Send event to the worker thread to send reply message
    struct evbuffer* evbOutput = evhttp_request_get_output_buffer(req);
    assert(evbOutput);
    evbuffer_add(evbOutput, strReply.data(), strReply.size());

    const int nStatusCode = to_integral_type(httpStatusCode);
	evhttp_send_reply(req, nStatusCode, nullptr, evbOutput);
	LogPrint("http", "Sent reply to %s (fd %" PRId64 "): status %d, output size %zu\n", 
		GetPeer().ToString(), m_clientSocket, nStatusCode, strReply.size());
	m_bReplySent = true;
}

const CService &HTTPRequest::GetPeer() const noexcept
{
    return m_peer;
}

socklen_t HTTPRequest::GetSockAddrParams(struct sockaddr* addr) const noexcept
{
	if (addr && m_addrlen > 0)
	{
		memcpy(addr, &m_addr, m_addrlen);
		return m_addrlen;
	}
	return 0;
}

string HTTPRequest::GetURI()
{
    return evhttp_request_get_uri(req);
}

void HTTPRequest::Cleanup()
{
    m_sPath.clear();
    m_RequestHandler = nullptr;
    req = nullptr;
    m_bReplySent = false;
}

RequestMethod HTTPRequest::GetRequestMethod() const noexcept
{
    RequestMethod method = RequestMethod::UNKNOWN;
    switch (evhttp_request_get_command(req))
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

void HTTPRequest::operator()()
{
    if (m_RequestHandler)
        (*m_RequestHandler)(this, m_sPath);
}

void HTTPRequest::SetRequestHandler(const string &sPath, const HTTPRequestHandler& handler)
{
    m_sPath = sPath;
	m_RequestHandler = handler;
}

void RegisterHTTPHandler(const string &sPrefix, const bool bExactMatch, const HTTPRequestHandler &handler)
{
    gl_HttpServer->RegisterHTTPHandler(sPrefix, bExactMatch, handler);
}

void UnregisterHTTPHandler(const string &sPrefix, bool bExactMatch)
{
    gl_HttpServer->UnregisterHTTPHandler(sPrefix, bExactMatch);
}

