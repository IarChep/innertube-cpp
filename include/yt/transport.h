// Pluggable HTTP transport (network backend) interface. C++17. See docs/backends.md.
//
// A backend moves ONE HTTP exchange: send the request (line + headers + body), deliver
// the response status + headers + *decoded* body, report how it ended. Policy is the
// library's, not the backend's: schemes, redirects (a backend must NOT follow them),
// body caps, timeouts, cancellation gating and thread marshalling all live above this
// interface, so no backend can weaken them. A backend is "supported" exactly when
// yt::conformance::runTransportConformance() reports no failures for it.
#ifndef YT_TRANSPORT_H
#define YT_TRANSPORT_H
#include <yt/log.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace yt {

struct HttpHeader { std::string name, value; };

struct HttpRequest {
    enum class Method { Get, Post } method = Method::Get;
    std::string url;                       // absolute http(s), byte-exact (never re-encode)
    std::vector<HttpHeader> headers;       // send verbatim
    std::string body;                      // Post only
};

enum class TransportError { None, Resolve, Connect, Tls, Reset, Protocol, Canceled, Other };
struct TransportStatus { TransportError error = TransportError::None; std::string message; };

// The library's per-request receiver. Calls may come on ANY thread, never concurrently
// for one request, always in order: onHead once, onBody 0..n, onDone exactly once
// (onDone alone when the exchange failed before a response arrived).
class HttpResponseSink {
public:
    virtual ~HttpResponseSink() = default;
    // Status + headers of THIS response (redirects are not followed by backends).
    virtual void onHead(int status, std::vector<HttpHeader> headers) = 0;
    // Decoded body bytes, in order. Return false to abort: the backend then stops
    // delivering body and calls onDone(Canceled).
    virtual bool onBody(const char *data, size_t size) = 0;
    virtual void onDone(TransportStatus status) = 0;
};

// Engine-thread integration for loop-driven backends (e.g. a curl multi handle):
// the engine's idle wait is delegated to the driver, which does its I/O there.
// Self-driven backends (own threads, OS callbacks) have no driver.
class LoopDriver {
public:
    virtual ~LoopDriver() = default;
    // Engine thread: block for I/O up to maxWaitMs or until wake(), then dispatch
    // whatever became ready. A wake() that happened before wait() was entered must
    // make that wait() return immediately.
    virtual void wait(int maxWaitMs) = 0;
    virtual void wake() = 0;               // any thread
};

// Created, used and destroyed on the engine thread.
class HttpTransport {
public:
    virtual ~HttpTransport() = default;    // cancels everything; no sink call after it returns
    virtual uint64_t start(HttpRequest request, std::shared_ptr<HttpResponseSink> sink) = 0;
    virtual void cancel(uint64_t id) = 0;  // best effort; the sink may still see onDone(Canceled)
    virtual LoopDriver *driver() { return nullptr; }
};

enum class IpResolve { Any, V4, V6 };

// Hints for the backend; a backend ignores what it cannot honour.
struct TransportConfig {
    std::string caBundlePath;              // empty = backend/OS default trust store
    IpResolve ipResolve = IpResolve::Any;
    std::string proxy;                     // empty = system setting (e.g. http_proxy env)
    bool noProxy = false;                  // true: always direct, ignoring proxy and system settings
    Logger log;
};

using TransportFactory = std::function<std::unique_ptr<HttpTransport>(const TransportConfig &)>;

// The build-time backend (INNERTUBE_HTTP_BACKEND); nullptr when built with "none".
std::unique_ptr<HttpTransport> makeDefaultTransport(const TransportConfig &config);

}
#endif
