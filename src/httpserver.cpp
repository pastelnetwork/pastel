// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <signal.h>
#include <stdio.h>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <cinttypes>
#include <deque>
#include <thread>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>
#include <event2/util.h>

#include <httpserver.h>
#include <chainparamsbase.h>
#include <compat.h>
#include <util.h>
#include <netbase.h>
#include <rpc/protocol.h> // For HTTP status code
#include <sync.h>
#include <ui_interface.h>
#include <vector_types.h>
#include <enum_util.h>

#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>
#endif
#endif

using namespace std;

/** HTTP request work item */
class HTTPWorkItem : public HTTPClosure
{
public:
    HTTPWorkItem(HTTPRequest* req, const string &path, const HTTPRequestHandler& func):
        req(req), path(path), func(func)
    {
    }
    void operator()()
    {
        func(req.get(), path);
    }

    unique_ptr<HTTPRequest> req;

private:
    string path;
    HTTPRequestHandler func;
};

/** Simple work queue for distributing work over multiple threads.
 * Work items are simply callable objects.
 */
template <typename WorkItem>
class WorkQueue
{
private:
    /** Mutex protects entire object */
    CWaitableCriticalSection cs;
    CConditionVariable cond;
    /* XXX in C++11 we can use unique_ptr here and avoid manual cleanup */
    deque<WorkItem*> queue;
    bool running;
    size_t maxDepth;
    int numThreads;

    /** RAII object to keep track of number of running worker threads */
    class ThreadCounter
    {
    public:
        WorkQueue &wq;
        ThreadCounter(WorkQueue &w): wq(w)
        {
            unique_lock<mutex> lock(wq.cs);
            wq.numThreads += 1;
        }
        ~ThreadCounter()
        {
            unique_lock<mutex> lock(wq.cs);
            wq.numThreads -= 1;
            wq.cond.notify_all();
        }
    };

public:
    WorkQueue(size_t maxDepth) : running(true),
                                 maxDepth(maxDepth),
                                 numThreads(0)
    {
    }
    /*( Precondition: worker threads have all stopped
     * (call WaitExit)
     */
    ~WorkQueue()
    {
        while (!queue.empty())
        {
            delete queue.front();
            queue.pop_front();
        }
    }
    /** Enqueue a work item */
    bool Enqueue(WorkItem* item)
    {
        unique_lock<mutex> lock(cs);
        if (queue.size() >= maxDepth) {
            return false;
        }
        queue.push_back(item);
        cond.notify_one();
        return true;
    }
    /** Thread function */
    void Run()
    {
        ThreadCounter count(*this);
        while (running)
        {
            WorkItem* i = 0;
            {
                unique_lock<mutex> lock(cs);
                while (running && queue.empty())
                    cond.wait(lock);
                if (!running)
                    break;
                i = queue.front();
                queue.pop_front();
            }
            (*i)();
            delete i;
        }
    }
    /** Interrupt and exit loops */
    void Interrupt()
    {
        unique_lock<mutex> lock(cs);
        running = false;
        cond.notify_all();
    }
    /** Wait for worker threads to exit */
    void WaitExit()
    {
        unique_lock<mutex> lock(cs);
        while (numThreads > 0)
            cond.wait(lock);
    }

    /** Return current depth of queue */
    size_t Depth()
    {
        unique_lock<mutex> lock(cs);
        return queue.size();
    }
};

struct HTTPPathHandler
{
    HTTPPathHandler(): prefix {}, exactMatch{false}, handler{} {}
    HTTPPathHandler(string prefix, bool exactMatch, HTTPRequestHandler handler):
        prefix(prefix), exactMatch(exactMatch), handler(handler)
    {
    }
    string prefix;
    bool exactMatch;
    HTTPRequestHandler handler;
};

/** HTTP module state */

//! libevent event loop
static struct event_base* eventBase = 0;
//! HTTP server
struct evhttp* eventHTTP = 0;
//! List of subnets to allow RPC connections from
static vector<CSubNet> rpc_allow_subnets;
//! Work queue for handling longer requests off the event loop thread
static WorkQueue<HTTPClosure>* workQueue = 0;
//! Handlers for (sub)paths
vector<HTTPPathHandler> pathHandlers;
//! Bound listening sockets
vector<evhttp_bound_socket *> boundSockets;

/** Check if a network address is allowed to access the HTTP server */
static bool ClientAllowed(const CNetAddr& netaddr)
{
    if (!netaddr.IsValid())
        return false;
    for (const auto& subnet : rpc_allow_subnets)
        if (subnet.Match(netaddr))
            return true;
    return false;
}

/** Initialize ACL list for HTTP server */
static bool InitHTTPAllowList()
{
    rpc_allow_subnets.clear();
    rpc_allow_subnets.push_back(CSubNet("127.0.0.0/8")); // always allow IPv4 local subnet
    rpc_allow_subnets.push_back(CSubNet("::1"));         // always allow IPv6 localhost
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
            rpc_allow_subnets.push_back(subnet);
        }
    }
    string strAllowed;
    for (const auto& subnet : rpc_allow_subnets)
        strAllowed += subnet.ToString() + " ";
    LogFnPrint("http", "Allowing HTTP connections from: %s", strAllowed);
    return true;
}

/** HTTP request method as string - use for logging only */
static string RequestMethodString(HTTPRequest::RequestMethod m)
{
    switch (m) {
    case HTTPRequest::GET:
        return "GET";
        break;
    case HTTPRequest::POST:
        return "POST";
        break;
    case HTTPRequest::HEAD:
        return "HEAD";
        break;
    case HTTPRequest::PUT:
        return "PUT";
        break;
    default:
        return "unknown";
    }
}

/** HTTP request callback */
static void http_request_cb(struct evhttp_request* req, void* arg)
{
    unique_ptr<HTTPRequest> hreq(new HTTPRequest(req));

    LogFnPrint("http", "Received a %s request for %s from %s",
             RequestMethodString(hreq->GetRequestMethod()), hreq->GetURI(), hreq->GetPeer().ToString());

    // Early address-based allow check
    if (!ClientAllowed(hreq->GetPeer()))
    {
        hreq->WriteReply(to_integral_type(HTTPStatusCode::FORBIDDEN));
        return;
    }

    // Early reject unknown HTTP methods
    if (hreq->GetRequestMethod() == HTTPRequest::UNKNOWN)
    {
        hreq->WriteReply(to_integral_type(HTTPStatusCode::BAD_METHOD));
        return;
    }

    // Find registered handler for prefix
    string strURI = hreq->GetURI();
    string path;
    auto i = pathHandlers.begin();
    auto iend = pathHandlers.end();
    for (; i != iend; ++i)
    {
        bool match = false;
        if (i->exactMatch)
            match = (strURI == i->prefix);
        else
            match = (strURI.substr(0, i->prefix.size()) == i->prefix);
        if (match)
        {
            path = strURI.substr(i->prefix.size());
            break;
        }
    }

    // Dispatch to worker thread
    if (i != iend)
    {
        unique_ptr<HTTPWorkItem> item(new HTTPWorkItem(hreq.release(), path, i->handler));
        assert(workQueue);
        if (workQueue->Enqueue(item.get()))
            item.release(); /* if true, queue took ownership */
        else
            item->req->WriteReply(to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Work queue depth exceeded");
    } else {
        hreq->WriteReply(to_integral_type(HTTPStatusCode::NOT_FOUND));
    }
}

/** Callback to reject HTTP requests after shutdown. */
static void http_reject_request_cb(struct evhttp_request* req, void*)
{
    LogFnPrint("http", "Rejecting request while shutting down");
    evhttp_send_error(req, to_integral_type(HTTPStatusCode::SERVICE_UNAVAILABLE), nullptr);
}

/** Event dispatcher thread */
static void ThreadHTTP(struct event_base* base, struct evhttp* http)
{
    RenameThread("psl-http");
    LogFnPrint("http", "Entering http event loop");
    event_base_dispatch(base);
    // Event loop will be interrupted by InterruptHTTPServer()
    LogFnPrint("http", "Exited http event loop");
}

/** Bind HTTP server to specified addresses */
static bool HTTPBindAddresses(string &error, struct evhttp* http)
{
    const int64_t nPortParam = GetArg("-rpcport", BaseParams().RPCPort());
    vector<pair<string, uint16_t> > endpoints;

    if ((nPortParam > numeric_limits<uint16_t>::max()) || (nPortParam < 0))
    {
        error = strprintf("'rpcport' parameter value [%" PRId64 "] is out of range (0..%hu)", nPortParam, numeric_limits<uint16_t>::max());
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
            string host;
            if (!SplitHostPort(error, sHostPort, port, host))
            {
                error = strprintf("Invalid format for 'rpcbind' parameter. %s", error);
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

    // Bind addresses
    for (const auto &[address, port] : endpoints)
    {
        LogFnPrint("http", "Binding RPC on address %s port %hu", address, port);
        auto bind_handle = evhttp_bind_socket_with_handle(http, address.empty() ? nullptr : address.c_str(), port);
        if (bind_handle)
            boundSockets.push_back(bind_handle);
        else
            LogFnPrintf("Binding RPC on address %s port %hu failed.", address, port);
    }
    return !boundSockets.empty();
}

/** Simple wrapper to set thread name and run work queue */
static void HTTPWorkQueueRun(WorkQueue<HTTPClosure>* queue)
{
    RenameThread("psl-httpworker");
    queue->Run();
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

bool InitHTTPServer()
{
    string error;
    struct evhttp* http = nullptr;
    struct event_base* base = nullptr;

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
#ifdef WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif

    base = event_base_new(); // XXX RAII
    if (!base)
    {
        LogFnPrintf("Couldn't create an event_base: exiting");
        return false;
    }

    /* Create a new evhttp object to handle requests. */
    http = evhttp_new(base); // XXX RAII
    if (!http)
    {
        LogFnPrintf("couldn't create evhttp. Exiting.");
        event_base_free(base);
        return false;
    }

    const int64_t nRpcServerTimeout = GetArg("-rpcservertimeout", DEFAULT_HTTP_SERVER_TIMEOUT);
    if (nRpcServerTimeout > numeric_limits<int>::max())
    {
        LogFnPrintf("'rpcservertimeout' parameter value [%" PRId64 "] is out of range (0..%d)", nRpcServerTimeout, numeric_limits<int>::max());
        evhttp_free(http);
        event_base_free(base);
        return false;
    }
    evhttp_set_timeout(http, static_cast<int>(nRpcServerTimeout));
    evhttp_set_max_body_size(http, MAX_DATA_SIZE);
    evhttp_set_gencb(http, http_request_cb, nullptr);

    if (!HTTPBindAddresses(error, http))
    {
        LogPrintf("Unable to bind any endpoint for RPC server. %s\n", error);
        evhttp_free(http);
        event_base_free(base);
        return false;
    }

    LogFnPrint("http", "Initialized HTTP server");
    int workQueueDepth = max((long)GetArg("-rpcworkqueue", DEFAULT_HTTP_WORKQUEUE), 1L);
    LogFnPrintf("HTTP: creating work queue of depth %d", workQueueDepth);

    workQueue = new WorkQueue<HTTPClosure>(workQueueDepth);
    eventBase = base;
    eventHTTP = http;
    return true;
}

thread threadHTTP;

bool StartHTTPServer()
{
    LogFnPrint("http", "Starting HTTP server");
    int rpcThreads = max((long)GetArg("-rpcthreads", DEFAULT_HTTP_THREADS), 1L);
    LogFnPrintf("HTTP: starting %d worker threads", rpcThreads);
    threadHTTP = thread(bind(&ThreadHTTP, eventBase, eventHTTP));

    for (int i = 0; i < rpcThreads; i++)
    {
        thread rpc_worker(HTTPWorkQueueRun, workQueue);
        rpc_worker.detach();
    }
    return true;
}

void InterruptHTTPServer()
{
    LogFnPrint("http", "Interrupting HTTP server");
    if (eventHTTP)
    {
        // Unlisten sockets
        for (auto socket : boundSockets)
            evhttp_del_accept_socket(eventHTTP, socket);
        // Reject requests on current connections
        evhttp_set_gencb(eventHTTP, http_reject_request_cb, nullptr);
    }
    if (workQueue)
        workQueue->Interrupt();
}

void StopHTTPServer()
{
    LogFnPrint("http", "Stopping HTTP server");
    if (workQueue)
    {
        LogFnPrint("http", "Waiting for HTTP worker threads to exit");
        workQueue->WaitExit();
        delete workQueue;
    }
    if (eventBase) {
        LogFnPrint("http", "Waiting for HTTP event thread to exit");
        // Exit the event loop as soon as there are no active events.
        event_base_loopexit(eventBase, nullptr);
        // Give event loop a few seconds to exit (to send back last RPC responses), then break it
        // Before this was solved with event_base_loopexit, but that didn't work as expected in
        // at least libevent 2.0.21 and always introduced a delay. In libevent
        // master that appears to be solved, so in the future that solution
        // could be used again (if desirable).
        // (see discussion in https://github.com/bitcoin/bitcoin/pull/6990)
//        if (!threadHTTP.try_join_for(chrono::milliseconds(2000))) {
//            LogPrintf("HTTP event loop did not exit within allotted time, sending loopbreak\n");
//            event_base_loopbreak(eventBase);
        threadHTTP.join();
//        }
    }
    if (eventHTTP)
    {
        evhttp_free(eventHTTP);
        eventHTTP = 0;
    }
    if (eventBase)
    {
        event_base_free(eventBase);
        eventBase = 0;
    }
    LogFnPrint("http", "Stopped HTTP server");
}

struct event_base* EventBase()
{
    return eventBase;
}

static void httpevent_callback_fn(evutil_socket_t, short, void* data)
{
    // Static handler: simply call inner handler
    HTTPEvent *self = ((HTTPEvent*)data);
    self->handler();
    if (self->deleteWhenTriggered)
        delete self;
}

HTTPEvent::HTTPEvent(struct event_base* base, bool deleteWhenTriggered, const function<void(void)>& handler):
    deleteWhenTriggered(deleteWhenTriggered), handler(handler)
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

HTTPRequest::HTTPRequest(struct evhttp_request* req) : req(req),
                                                       replySent(false)
{}

HTTPRequest::~HTTPRequest()
{
    if (!replySent)
    {
        // Keep track of whether reply was sent to avoid request leaks
        LogFnPrintf("Unhandled request");
        WriteReply(to_integral_type(HTTPStatusCode::INTERNAL_SERVER_ERROR), "Unhandled request");
    }
    // evhttpd cleans up the request, as long as a reply was sent.
}

pair<bool, string> HTTPRequest::GetHeader(const string& hdr)
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

/** Closure sent to main thread to request a reply to be sent to
 * a HTTP request.
 * Replies must be sent in the main loop in the main http thread,
 * this cannot be done from worker threads.
 */
void HTTPRequest::WriteReply(int nStatus, const string& strReply)
{
    assert(!replySent && req);
    // Send event to main http thread to send reply message
    struct evbuffer* evb = evhttp_request_get_output_buffer(req);
    assert(evb);
    evbuffer_add(evb, strReply.data(), strReply.size());
    HTTPEvent* ev = new HTTPEvent(eventBase, true,
        boost::bind(evhttp_send_reply, req, nStatus, (const char*)nullptr, (struct evbuffer *)nullptr));
    ev->trigger(0);
    replySent = true;
    req = 0; // transferred back to main thread
} //-V773 : ev will be release by the callback function httpevent_callback_fn later, so this PVS warning is a false warning

CService HTTPRequest::GetPeer()
{
    evhttp_connection* con = evhttp_request_get_connection(req);
    CService peer;
    if (con)
    {
        // evhttp retains ownership over returned address string
        const char* address = "";
        uint16_t port = 0;
        evhttp_connection_get_peer(con, (char**)&address, &port);
        peer = CService(address, port);
    }
    return peer;
}

string HTTPRequest::GetURI()
{
    return evhttp_request_get_uri(req);
}

HTTPRequest::RequestMethod HTTPRequest::GetRequestMethod()
{
    switch (evhttp_request_get_command(req))
    {
        case EVHTTP_REQ_GET:
            return GET;
            break;
        case EVHTTP_REQ_POST:
            return POST;
            break;
        case EVHTTP_REQ_HEAD:
            return HEAD;
            break;
        case EVHTTP_REQ_PUT:
            return PUT;
            break;
        default:
            return UNKNOWN;
            break;
    }
}

void RegisterHTTPHandler(const string &prefix, bool exactMatch, const HTTPRequestHandler &handler)
{
    LogFnPrint("http", "Registering HTTP handler for %s (exactmatch %d)", prefix, exactMatch);
    pathHandlers.push_back(HTTPPathHandler(prefix, exactMatch, handler));
}

void UnregisterHTTPHandler(const string &prefix, bool exactMatch)
{
    auto i = pathHandlers.begin();
    auto iend = pathHandlers.end();
    for (; i != iend; ++i)
    {
        if (i->prefix == prefix && i->exactMatch == exactMatch)
            break;
    }
    if (i != iend)
    {
        LogFnPrint("http", "Unregistering HTTP handler for %s (exactmatch %d)", prefix, exactMatch);
        pathHandlers.erase(i);
    }
}

