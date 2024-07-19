#pragma once
// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <functional>
#include <vector>
#include <deque>

#include <event2/event.h>
#include <event2/http.h>

#include <utils/sync.h>
#include <utils/svc_thread.h>
#include <rpc/protocol.h>
#include <netbase.h>

// default number of threads for the HTTP server
constexpr int DEFAULT_HTTP_WORKER_THREADS = 4;
// maximum number of threads for the HTTP server
constexpr size_t MAX_HTTP_THREADS = 64;
// default number of workqueue items for the HTTP server
constexpr int DEFAULT_HTTP_WORKQUEUE_MAX_SIZE = 4096;
// minimum number of workqueue items for the HTTP server
constexpr int MIN_HTTP_WORKQUEUE_MAX_SIZE = 16;
// maximum size of the HTTP headers
constexpr int DEFAULT_HTTP_MAX_HEADERS_SIZE = 8192;
// default timeout for the HTTP server
constexpr int DEFAULT_HTTP_SERVER_TIMEOUT_SECS = 900;
// default backlog for the HTTP server (use system default)
constexpr int DEFAULT_HTTP_SERVER_ACCEPT_BACKLOG = -1;
// maximum size of the HTTP URI
constexpr size_t MAX_URI_LENGTH = 1024;

enum class RequestMethod
{
    UNKNOWN,
    GET,
    POST,
    HEAD,
    PUT
};

class HTTPRequest;
class CHttpWorkerContext;
class CHttpConnection;

using http_connection_t = std::shared_ptr<CHttpConnection>;
using http_worker_context_t = std::shared_ptr<CHttpWorkerContext>;

/** Handler for requests to a certain HTTP path */
typedef std::function<void(HTTPRequest* req, const std::string &)> HTTPRequestHandler;
/** Register handler for prefix.
 * If multiple handlers match a prefix, the first-registered one will
 * be invoked.
 */
void RegisterHTTPHandler(const std::string &sHandlerGroup, const std::string &prefix, 
    bool exactMatch, const HTTPRequestHandler &handler);
/** Unregister handler by handler group name */
void UnregisterHTTPHandlers(const std::string &sHandlerGroup);

class HTTPRequest
{
public:
    HTTPRequest(evhttp_request* req, http_connection_t pHttpConnection) noexcept;
    ~HTTPRequest();

    void execute();
    void Cleanup();

    void SetRequestHandler(const std::string& sPath, const HTTPRequestHandler& handler);

    std::string GetURI(); // Get requested URI.

    // Get request method.
    virtual RequestMethod GetRequestMethod() const noexcept;

    /**
     * Get the request header specified by hdr, or an empty string.
     * Return a pair (isPresent, string).
     */
    virtual std::pair<bool, std::string> GetHeader(const std::string& hdr) const noexcept;

    /**
     * Read request body.
     *
     * @note As this consumes the underlying buffer, call this only once.
     * Repeated calls will return an empty string.
     */
    std::string ReadBody();

    /**
     * Write output header.
     *
     * @note call this before calling WriteErrorReply or WriteReply.
     */
    virtual void WriteHeader(const std::string& hdr, const std::string& value);

    /**
     * Write HTTP reply.
     * nStatus is the HTTP status code to send.
     * strReply is the body of the reply. Keep it empty to send a standard message.
     *
     * @note Can be called only once. As this will give the request back to the
     * main thread, do not call any other HTTPRequest methods after calling this.
     */
    virtual void WriteReply(HTTPStatusCode httpStatusCode, const std::string& strReply = "");
    virtual const std::string GetPeerStr() const noexcept;

protected:
    bool m_bReplySent; // true if reply has been sent

private:
    std::string m_sPath; // HTTP URI path
    std::optional<HTTPRequestHandler> m_RequestHandler; // handler for the request

    struct evhttp_request* m_req; // libevent request
    http_connection_t m_pHttpConnection; // HTTP connection
};

class CHttpConnection
{
public:
    CHttpConnection(evutil_socket_t clientSocket, const struct sockaddr* addr, const socklen_t addrlen) noexcept;
    ~CHttpConnection();

    evutil_socket_t GetClientSocket() const noexcept { return m_clientSocket; }
    struct sockaddr* GetAddr() { return &m_addr; }
    socklen_t GetAddrlen() { return m_addrlen; }
    socklen_t GetSockAddrParams(struct sockaddr* addr) const noexcept;
    bool ValidateClientConnection(CHttpWorkerContext* pHttpWorkerContext,
        const std::unique_ptr<HTTPRequest> &pHttpRequest, struct evhttp_connection *evcon);

    bool UsesKeepAliveConnection() const noexcept { return m_bUsesKeepAliveConnection; }
    // Get CService (address:ip) for the origin of the http connection.
    const CService &GetPeer() const noexcept;

private:
    evutil_socket_t m_clientSocket;
    struct sockaddr m_addr;
    socklen_t m_addrlen;
    CService m_peer;
    bool m_bUsesKeepAliveConnection;
    std::atomic_bool m_bClientIsAllowed;
    std::atomic_bool m_bClientValidated;
};

/**
 * HTTP worker context.
 * It runs its own event loop and processes HTTP requests.
 */
class CHttpWorkerContext : public CServiceThread 
{
public:
    CHttpWorkerContext(const size_t nWorkerID) noexcept;
    ~CHttpWorkerContext();

    bool Initialize(std::string& error);
    void execute() override;
    void stop() noexcept override;

    struct event_base* GetEventBase() { return m_base; }
    evhttp* GetHttp() { return m_http; }
    size_t GetWorkerID() const noexcept { return m_nWorkerID; }

    void AddHttpConnection(http_connection_t &&pConnection);
    void CloseHttpConnection(const evutil_socket_t clientSocket, const bool bCloseSocket);
    http_connection_t GetHttpConnection(const evutil_socket_t clientSocket);

    void TriggerEvent() noexcept;
    void WaitForEvent();

private:
    size_t m_nWorkerID;

    // map to keep HTTP connection objects
    std::unordered_map<evutil_socket_t, http_connection_t> m_ConnectionMap;
    CWaitableCriticalSection m_ConnectionMapLock;

    struct event_base* m_base; // libevent event loop
    evhttp* m_http;      // HTTP server
    std::string m_sLoopName; // name of the event loop
    std::atomic_bool m_bEvent;

    CWaitableCriticalSection m_mutex;
    CConditionVariable m_cond;

    void DestroyEventLoop();
};

/** Event class. This can be used either as a cross-thread trigger or as a timer.
 */
class HTTPEvent
{
public:
    /** Create a new event.
     * deleteWhenTriggered deletes this event object after the event is triggered (and the handler called)
     * handler is the handler to call when the event is triggered.
     */
    HTTPEvent(struct event_base* base, const bool bDeleteWhenTriggered, const std::function<void(void)>& handler);
    ~HTTPEvent();

    /** Trigger the event. If tv is 0, trigger it immediately. Otherwise trigger it after
     * the given time has elapsed.
     */
    void trigger(struct timeval* tv);
    bool DeleteWhenTriggered() const noexcept { return m_bDeleteWhenTriggered; }
    void callHandler() { handler(); }

private:
    bool m_bDeleteWhenTriggered;
    std::function<void(void)> handler;

private:
    struct event* ev;
};

class CHTTPServer;

/** Simple work queue for distributing work over multiple threads.
 * Work items are simply callable objects.
 */
class WorkQueue
{
public:
    explicit WorkQueue(CHTTPServer &httpServer, const size_t nMaxQueueSize) : 
        m_httpServer(httpServer),
        m_nMaxQueueSize(nMaxQueueSize),
        m_bRunning(false)
    {
        if (nMaxQueueSize == 0)
            throw std::invalid_argument("Max queue size must be greater than 0");
    }

    // Enqueue a new connection
    std::tuple<std::unique_ptr<CHttpConnection>, size_t> EnqueueConnection(std::unique_ptr<CHttpConnection>&& connection);

    /** http worker job */
    void worker(const size_t nWorkerID);

    /** Interrupt and exit loops */
    void Interrupt()
    {
        std::unique_lock lock(cs);
        m_bRunning = false;
        cond.notify_all();
    }

    /** Return current size of queue */
    size_t size() const;

private:
    /** Mutex protects entire object */
    mutable CWaitableCriticalSection cs;
    CConditionVariable cond;
    std::atomic_bool m_bRunning;
    std::deque<std::unique_ptr<CHttpConnection>> m_queue;
    std::unordered_map<size_t, http_worker_context_t> m_WorkerContextMap;
    size_t m_nMaxQueueSize;
    CHTTPServer &m_httpServer;
};

struct HTTPPathHandler
{
    HTTPPathHandler() noexcept = delete;
    HTTPPathHandler(const std::string& sGroup, const std::string& sPrefix, 
        const bool bExactMatch, const HTTPRequestHandler &handler) noexcept;

    bool IsMatch(const std::string& sPathPrefix) const noexcept;
    bool IsGroup(const std::string& sGroup) const noexcept;
    HTTPRequestHandler GetHandler() const noexcept { return m_handler; }
    size_t GetPrefixSize() const noexcept { return m_sPrefix.size(); }

private:
    std::string m_sGroup;
    std::string m_sPrefix;
    bool m_bExactMatch;
    HTTPRequestHandler m_handler;
};

class CHTTPServer final : public CServiceThread
{
public:
    CHTTPServer() noexcept;

    /** Initialize HTTP server.
     * Call this before RegisterHTTPHandler or EventBase().
     */
    bool Initialize();
    /** Start HTTP server.
     * This is separate from InitHTTPServer to give users race-condition-free time
     * to register their handlers between InitHTTPServer and StartHTTPServer.
     */
    bool Start() noexcept;
    void Stop(); // Stop HTTP server 
    void execute() override;
    void Interrupt(); // Interrupt HTTP server threads
    void RegisterHTTPHandler(const std::string& sHandlerGroup, const std::string &sPrefix, const bool bExactMatch, const HTTPRequestHandler &handler);
    void UnregisterHTTPHandlers(const std::string& sHandlerGroup);
    bool FindHTTPHandler(const std::string& sURI, std::string &sPath, HTTPRequestHandler& handler) const noexcept;

    bool IsShuttingDown() const noexcept { return m_bShuttingDown; }
    std::string GetInitError() const noexcept { return m_sInitError; }
    /** Return evhttp event base. This can be used by submodules to
    * queue timers or custom events. */
    struct event_base* GetEventBase() { return m_pMainEventBase; }
    bool IsClientAllowed(const CNetAddr& netaddr) const;

    int GetRpcServerTimeout() const noexcept { return m_nRpcServerTimeout; }
    size_t GetWorkQueueMaxSize() const noexcept { return m_nWorkQueueMaxSize; }
    size_t GetRpcWorkerThreads() const noexcept { return m_nRpcWorkerThreads; }

private:
    bool m_bInitialized;
    bool m_bShuttingDown;
    std::string m_sInitError;
    std::thread m_MainThread;
    struct event_base* m_pMainEventBase;
    std::vector<evconnlistener*> m_vListeners;
    // Work queue for handling longer requests off the event loop thread
    std::shared_ptr<WorkQueue> m_pWorkQueue;
    CServiceThreadGroup m_WorkerThreadPool;

    // list of subnets to allow RPC connections from
    std::vector<CSubNet> m_rpc_allow_subnets;
    size_t m_nRpcWorkerThreads;
    int m_nRpcServerTimeout;
    size_t m_nWorkQueueMaxSize;
    int m_nAcceptBackLog;
    // handlers for (sub)paths
    std::vector<HTTPPathHandler> m_vPathHandlers;

    bool InitHTTPAllowList();
    bool BindAddresses();
};

extern std::unique_ptr<CHTTPServer> gl_HttpServer;

