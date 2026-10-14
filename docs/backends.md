# Writing an HTTP backend

innertube-cpp does all its networking — youtubei POSTs, OAuth forms, RYD / suggest /
timedtext GETs, ranged googlevideo GETs — through one interface,
[`yt::HttpTransport`](../include/yt/transport.h). The default backend is libcurl
(`backends/curl/`). A port to a platform without libcurl, or one that would rather use
its native HTTP stack (WinHTTP, NSURLSession, Android, `emscripten_fetch`, …), needs one
new backend and nothing else.

## What a backend does, and what it must not do

A backend moves **one HTTP exchange**: send the request line + headers + body, deliver
the response status + headers + **decoded** body, report how it ended. Everything that
is *policy* stays in the library (`net::HttpClient`), so no backend can weaken it:

| Concern | Owner | How |
|---|---|---|
| Allowed schemes (http/https only — SSRF guard) | library | checked before `start()` and on every redirect hop |
| Redirects (bounded; only credential-free GETs follow — never with `Authorization`/`Cookie`, never for POSTs, whose bodies may carry OAuth secrets) | library | backends **must not** follow redirects; deliver the 3xx as is |
| Control characters in URLs / header values | library | requests carrying CR/LF/NUL are refused before `start()` |
| Body cap | library | `onBody()` returns `false` → the backend aborts |
| Timeouts | library | engine-loop timer → `cancel()` |
| Cancellation, late callbacks | library | per-request gate on the engine thread; late sink calls are dropped |
| Thread marshalling | library | sink calls from foreign threads are posted to the engine thread |
| TLS, trust store, proxy, IP family, HTTP/2, keep-alive, compression | backend | `TransportConfig` hints; must not send `Expect: 100-continue` |

## The contract

```cpp
class HttpTransport {
public:
    virtual ~HttpTransport();              // cancels everything; no sink call after it returns
    virtual uint64_t start(HttpRequest request, std::shared_ptr<HttpResponseSink> sink) = 0;
    virtual void cancel(uint64_t id) = 0;  // best effort; the sink may still see onDone(Canceled)
    virtual LoopDriver *driver() { return nullptr; }
};
```

- The transport is **created, used and destroyed on the engine thread** (the library
  calls your factory there), so thread-affine native handles never cross threads.
- `start()` must not call the sink synchronously.
- Sink calls may come from **any** thread, but never concurrently for one request, and
  always in order: `onHead` once, `onBody` 0..n times, `onDone` exactly once (`onDone`
  alone when no response arrived).
- `request.url` is byte-exact: never decode or re-encode it (signed googlevideo URLs).
- Deliver the **decoded** body (gunzip if you advertised `Accept-Encoding`).
- When `onBody()` returns `false`, stop delivering body and end with `onDone(Canceled)`.
- Map failures to `TransportError`: `Resolve`, `Connect`, `Tls`, `Reset` (incl. a
  truncated body), `Protocol`, `Canceled`, `Other`.

## Two integration styles

**Loop-driven** — your I/O can run inside the library's engine-thread wait. Return a
`LoopDriver` from `driver()`; the engine calls `wait(maxWaitMs)` whenever it is idle and
`wake()` (from any thread) when it has work. All your sink calls then happen on the
engine thread. Reference: `backends/curl/curltransport.cpp` (`curl_multi_poll` /
`curl_multi_wakeup`).

**Self-driven** — your API has its own threads or completion callbacks. Return no
driver and call the sink from wherever results arrive; the library marshals them.
Reference: `examples/threaded-transport/` (one blocking `curl_easy_perform` per request
on its own thread) — the template for WinHTTP, NSURLSession completion handlers or JNI.

## Plugging it in

At build time, compile your backend in as the default:

```sh
cmake -B build -DINNERTUBE_HTTP_BACKEND=path/to/my_transport.cpp \
               -DINNERTUBE_HTTP_BACKEND_LIBS="winhttp"
```

The file must define `std::unique_ptr<yt::HttpTransport> yt::makeDefaultTransport(const
yt::TransportConfig &)`. `-DINNERTUBE_HTTP_BACKEND=none` builds the library with no
backend (and no libcurl) — then every `Client` must be given a factory.

At run time, per client:

```cpp
yt::ClientOptions opts;
opts.transport = [](const yt::TransportConfig &c) { return std::make_unique<MyTransport>(c); };
```

## "Supported" means: the conformance suite passes

```cpp
#include "conformance/transport_conformance.h"   // target innertube_conformance
auto failures = yt::conformance::runTransportConformance(
    [](const yt::TransportConfig &c) { return std::make_unique<MyTransport>(c); });
// failures.empty() == supported; each entry is "<check>: <why>"
```

It runs against a scripted loopback HTTP server (plain HTTP, no TLS setup) — with
`TransportConfig::noProxy` set, which a backend must honour (go direct, ignoring
`http_proxy` & co.) — and checks:
GET/POST bodies, request + response headers, **no redirect following**, byte-exact URLs,
1 MiB streamed in order, `onBody → false` aborts, cancel mid-transfer, connection refused
→ `Connect`, unresolvable host → `Resolve`, truncated body → error, no
`Expect: 100-continue` (2 MiB POST), 32 concurrent requests, no overlapping or
out-of-order sink calls, no sink call after the destructor.

Checklist before you run it: redirects off · decoded bodies · `Expect` suppressed ·
`onBody → false` honoured · nothing after the destructor.

## Later: a builtin socket backend

For targets with neither libcurl nor a native HTTP API, a backend in the baileys-cpp
mould fits behind the same interface: a `sockets.h` free-function socket layer
(posix / win32 / your own `.cpp`), a minimal HTTP/1.1 client (Content-Length + chunked,
keep-alive, optional zlib inflate) and TLS via wolfSSL or mbedTLS. It is just another
`HttpTransport`; the core does not change.
