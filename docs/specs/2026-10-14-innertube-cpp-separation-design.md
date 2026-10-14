# innertube-cpp: separating meetube-core into a Qt-free product library (analysis + design)

**Status:** design — awaiting review. Implementation plan:
[../plans/2026-10-14-innertube-cpp-separation.md](../plans/2026-10-14-innertube-cpp-separation.md).

**Goal:** turn `meetube-core` (today: a Qt 4.7.4-bound STATIC lib shaped around one QML app)
into **innertube-cpp** — a standalone C++ YouTube InnerTube client library with **zero Qt
dependencies**, usable from any modern C++ project on any libcurl-capable target (Linux, Android
NDK, macOS/iOS, Windows, embedded ARM incl. the N9). MeeTube becomes its first *consumer*,
through a thin Qt bridge, with the QML contract frozen (zero `.qml` edits).

---

## 1. Analysis of the current core

`src/core/` = **11 551 LOC** in 9 directories. Every directory was read in full; the coupling
verdicts below are per directory.

| Dir | LOC | Qt coupling today | What it really is | Verdict |
|---|---|---|---|---|
| `types/` | 76 | `QString`/`QList`/`qint64` fields + `Q_DECLARE_METATYPE` | value types | **lib** (std types); Qt types survive only in the bridge as row types |
| `parsers/` | 2336 | **thin**: the engine is already Glaze + a Qt-free single-pass scanner (`json/jsonscan.h`, `json/tokenscan.h`, `json/ytjson.h` have zero Qt); outputs are `QString`/`QList`; `QRegExp` ×3 in `rendererinternal.h` (digits filter, `h:mm:ss` match, 4-digit year) | the product's crown jewel | **lib** — mechanical port |
| `requests/` | 213 | `QString` params only | POST body builders (Glaze) | **lib** — mechanical |
| `core/` | 1434 | `job.h`/`status.h`: none. `chains.*`: `QString`/`QList`/`QMap`/`QUrl::toPercentEncoding`. `http.*`: `QObject`, QNAM, `QTimer`, `QHash`, `QCryptographicHash`(md5), `QDateTime`, thread-affinity asserts. `debuglog.*`: `QDebug`, `qgetenv("MEETUBE_DEBUG")` | request layer + transport + logging | chains/http logic → **lib**; QNAM/QTimer plumbing → **replaced** |
| `threading/` | 142 | `QThread` + `QEvent` posted closures | the one cross-thread seam | worker side → **lib** (own loop thread); GUI-side dispatcher → **bridge** |
| `net/` | 594 | `QNetworkReply`/`QNetworkAccessManager` subclasses, `QSocketNotifier`, `QTimer` | libcurl driven by a Qt event loop | **bridge** (still needed for QML `Image` thumbnails); the lib gets its own Qt-free curl loop |
| `innertube/` | 2752 | heavy: `QObject`/`Q_PROPERTY`/`Q_INVOKABLE`/`QVariantList`, a GUI singleton, `QPointer`, `QTimer::singleShot` (OAuth poll) | **mixed**: QML view-models (VideoDetails optimistic like/subscribe, StreamSet catalog maps, API tree caching) + real client logic (OAuth device flow, client/context config, session, settings persistence) | split: client logic → **lib**; view-models → **bridge**; `SettingsStore` (app prefs: orientation, quality) → **app** |
| `models/` | 1116 | `QAbstractListModel` | QML list models | **bridge** in full |
| `media/` | 2908 | `fmp4demux`/`bufferplanner`: container types only. `bytesource`/`hlssource`/`subtitletrack`: QNAM + `QXmlStreamReader`. `mediapump`/`streamplayer`/`ipipeline`/`ipolicy`: QObject signal machines encoding **GStreamer 0.10 appsrc + Harmattan resource-policy + OMAP overlay colour-key** semantics | playback | Qt-free building blocks (demuxer, planner, HLS master parse, captions parse, ranged source) → **optional `innertube::media`**; the player state machine → **stays in MeeTube** |

### 1.1 What is already right (keep it)

- **Glaze + single-pass scanner parsers** — no DOM, Qt-free core; the port is type-level only.
- **Pure-C++ chains** over the `IHttp` seam with `std::function` continuations and an advisory
  `JobToken` — the request layer is *already* the library's shape, just with Qt string types.
- **The JobToken delivery gate** (`invokeGui([job]{ if (!live(job)) return; … })`) — the
  race-free cancellation protocol; it becomes the library's public cancellation contract.
- **Transport policy**: request coalescing, per-endpoint TTL cache, memoized per-client
  context/headers, single envelope scan, visitorData capture — all logic, no Qt in essence.
- **libcurl hardening** (`CurlNetworkReply`): http/https-only (SSRF), no redirect-follow when
  `Authorization`/`Cookie` present, bounded redirects, 32 MB body cap, `Expect:` suppression,
  `PIPEWAIT`, IPv4 forcing (N9 networks). All of it must survive the port verbatim.
- `FakeHttp` test seam and the 9-suite harness, the fuzzers and fixture payloads.

### 1.2 What ties the core to *this one app* (beyond Qt)

1. **Singleton reach-around**: models/details call `Innertube::instance()` in `apiRef()` and
   `signedIn()` (35 sites). A library cannot have a process singleton; the client is an object.
2. **App-named environment**: `MEETUBE_DEBUG`, `MEETUBE_NET_DEBUG`, `MEETUBE_PLAYER_DEBUG`,
   `MEETUBE_COLORKEY`, `MEETUBE_PREBUFFER_FRAMES` are read inside core.
3. **Compile-time config**: `MEETUBE_CA_BUNDLE` compile def; `BUILD_N9` branches in core's CMake;
   `CACERT_PEM`/`CURL_A`/`WOLFSSL_A` CACHE INTERNAL vars from the app's `deps/`.
4. **File I/O policy**: `SettingsStore` hard-codes `~/.config/MeeTube/settings.json` and mixes
   client state (refresh token, visitorData) with app prefs (player orientation, default quality).
5. **QML-shaped contracts**: `Status` ints frozen for `Status.js`; `QVariantList` catalogs
   (`navEntries`/`searchTypes`/`feedSections`) with English UI labels; `QObject*` returns to dodge
   the QML GC.
6. **Device semantics in core**: colour-key overlay (`playbackmode.h`), GL-context-loss rebuild,
   Nokia DSP AAC "rebuild instead of flush-seek", resource policy.
7. **Errors are strings**: `Outcome<T>{ok, QString error}` — a product needs machine-readable codes.
8. **Build**: one CMake tree, not standalone-configurable; no install/export; tests on QtTest.

### 1.3 Latent defects found during the analysis (fix as part of the port)

- **OAuth access-token expiry is never handled.** `AccountManager` mints a bearer once
  (`restore()` at startup / sign-in) and never reads `expires_in`; Google access tokens live ~1 h.
  A session longer than that sends a dead bearer on every authed TV call until restart. The
  library refreshes proactively (expiry − 60 s) and once on HTTP 401.
- **`Session` direct-write hazard** (`http.h` comment): direct `session()` writes without
  `clearCache()` serve stale memoized context. The library has no public mutable session — only
  `setLocale()`, which invalidates internally.
- **Captions via XML (`QXmlStreamReader`, forced `fmt=srv1`)** — timedtext also serves
  `fmt=json3`, which Glaze reads; the XML dependency disappears instead of being re-implemented.

---

## 2. Decisions (defaults — each is overridable at review)

| # | Decision | Default | Why |
|---|---|---|---|
| D1 | Product name / target / namespace | **innertube-cpp**, CMake `innertube::innertube`, namespace `yt` (already used) | `yt::` is in place; `CT::` (cuteTube heritage) retires into the bridge |
| D2 | License | **unchanged GPLv3** — flagged for the owner | GPLv3 forces every consumer app to be GPL. For "any project" adoption **LGPL-3.0 or MPL-2.0** is the usual choice. All `src/core` headers carry © IarChep, so the owner can relicense; *verify no file still contains verbatim cuteTube2 code before going permissive* |
| D3 | Player scope | `StreamPlayer`/`MediaPump`/`IPipeline`/`IPolicy` **stay in MeeTube**; Qt-free building blocks ship as optional **`innertube::media`** | every modern target has its own player (ExoPlayer, AVPlayer, mpv, GStreamer 1.x); the N9 state machine encodes 0.10/DSP/overlay quirks nobody else needs |
| D4 | Language level | **public headers C++17**, implementation **C++23** (Glaze v7 needs it, PRIVATE) | maximum consumer reach; Glaze never leaks into a public header |
| D5 | Threading | library **owns one engine thread** (task queue + timers); its idle wait is **pluggable** (`LoopDriver`): a loop-driven backend (curl multi) does its I/O inside that wait, a self-driven backend (own threads / OS callbacks) completes from anywhere and is marshalled onto the engine thread; optional *manual drive* (`ownThread=false` + `runOnce()`) for tests/embedding | identical to today's worker model; no Qt/libuv/asio dependency; works for both curl and native HTTP stacks |
| D6 | Callback delivery | consumer-supplied **`Executor`**; empty = run on engine thread | Qt bridge passes a GUI-thread poster; plain C++ consumers pick inline or their own queue |
| D7 | Strings | **UTF-8 `std::string`** everywhere | removes every `QString::fromUtf8` on the hot path; QString conversion happens once, in the bridge, **on the engine thread** (same cost/placement as today) |
| D8 | Persistence | **none in the lib**: `SessionState` in via options, out via `onStateChanged` | the consumer owns storage; no hard-coded paths |
| D9 | Tests | **doctest** (single header, vendored submodule) for the lib; QtTest stays for the bridge/app suites | smallest real test framework; no Qt in lib tests |
| D10 | Repo | incubate in-tree at `libinnertube/`, then `git subtree split` → own repo, consumed by MeeTube as `deps/innertube-cpp` submodule | keeps MeeTube green at every commit during the migration |
| D11 | Fragile constants | client versions/UAs **overridable at runtime** (`ClientOptions::clients`) | YouTube rotates them; a consumer must not wait for a lib release (a real calibration knob) |
| D12 | Network backends | a public **`HttpTransport`** interface at the **HTTP request/response level**, chosen at build time (`INNERTUBE_HTTP_BACKEND=curl\|none\|<path.cpp>`, like baileys-cpp's `BAILEYS_NET_BACKEND`) and overridable at runtime (`ClientOptions::transport` factory); a framework-agnostic **conformance suite** every backend must pass; **security policy (schemes, redirects, body cap, timeouts) lives in the library, not in backends** | every target either has libcurl or a native HTTP stack with TLS/proxy/trust store (NSURLSession, WinHTTP, Android, emscripten fetch) — wrapping one is far easier than porting sockets + TLS + an HTTP client. A baileys-style socket layer (`sockets.h` + built-in HTTP/1.1 + TLS) is an optional later backend (§3.5), not a core change |

---

## 3. Target architecture

```
┌──────────────── consumer (MeeTube app) ────────────────────────────────┐
│ QML ── bridge (src/bridge, Qt 4.7): Innertube QObject, VideoApi/…Api,   │
│        VideoDetails/StreamSet/…, list models, GuiExecutor, CT:: row     │
│        types + yt→CT converters, CurlNam (QML thumbnails), SettingsStore│
│        app/media: StreamPlayer/MediaPump/GstAppPipeline/PolicyGuard     │
└──────────────────────────────┬──────────────────────────────────────────┘
                               │ public C++17 API (include/yt/*.h)
┌──────────────────────────────▼──────────────────────────────────────────┐
│ innertube::innertube (libinnertube/)                                     │
│  Client ── posts every call onto ── engine thread: net::Loop             │
│    │                                   (task queue + timers; idle wait = │
│    │                                    cv, or the backend's LoopDriver) │
│    ├─ Auth (device flow, poll timer, auto-refresh)                       │
│    ├─ chains (fetch*/submit*/oauth*) ── IHttp ── net::HttpClient         │
│    │                                   (coalesce, TTL cache, envelope,   │
│    │                                    ctx/header memo, POLICY: schemes,│
│    │                                    redirects, body cap, timeouts)   │
│    ├─ ContextBuilder / ClientConfig(overridable) / bodies                │
│    └─ parsers (Glaze + jsonscan)                                         │
│                     │ yt::HttpTransport (public, §3.5)                   │
│   ┌─────────────────┼──────────────────┬───────────────────────────┐     │
│   │ backends/curl   │ your own .cpp /  │ (optional, later) builtin │     │
│   │ multi + Loop-   │ runtime factory: │ HTTP/1.1 + TLS over a     │     │
│   │ Driver (default)│ WinHTTP, NSURL-  │ sockets.h backend         │     │
│   │                 │ Session, JNI, …  │ (baileys-cpp model)       │     │
│   └─────────────────┴──────────────────┴───────────────────────────┘     │
│ innertube::media (optional): Fmp4Demuxer, BufferPlanner, HLS master      │
│    parse, json3 captions parse, RangedSource on HttpClient streaming     │
└──────────────────────────────────────────────────────────────────────────┘
        deps: Glaze (PRIVATE), C++ stdlib; libcurl ≥ 7.68 only with the curl backend
```

### 3.1 Public API (C++17 headers)

```cpp
// include/yt/result.h
namespace yt {
enum class ErrorCode { Transport, Timeout, InvalidResponse, Api, Unavailable,
                       Unsupported, AuthRequired, Canceled };
struct Error { ErrorCode code; std::string message; long httpStatus = 0; };
template <class T> struct Result {
    std::optional<Error> error;            // nullopt = ok
    T value{};
    bool ok() const { return !error; }
};
struct Done {};                            // Result<Done> for write actions
template <class T> using Callback = std::function<void(const Result<T> &)>;
}

// include/yt/client.h
namespace yt {
using Executor = std::function<void(std::function<void()>)>;
enum class LogLevel { Debug, Info, Warn };
using Logger = std::function<void(LogLevel, std::string_view category, std::string_view msg)>;
// TransportConfig / TransportFactory / IpResolve: include/yt/transport.h (§3.5)

struct ClientOptions {
    TransportConfig net;                   // caBundlePath, ipResolve (MeeTube: V4), proxy, log
    TransportFactory transport;            // empty = makeDefaultTransport() (build-time backend)
    int timeoutMs = 20000;
    bool ownThread = true;                 // false = consumer calls runOnce()
    Executor deliver;                      // empty = callbacks on the engine thread
    Logger log;                            // empty = silent
    SessionState initial;                  // hl/gl/visitorData/refreshToken/account
    std::function<void(const SessionState &)> onStateChanged;   // persist me
    std::map<ClientId, ClientInfo> clients;                      // version/UA overrides
};

class Job {                                 // copyable handle; empty = no-op
public:
    void cancel();                         // any thread; see contract §3.2
    bool active() const;
};

class Client {
public:
    explicit Client(ClientOptions);
    ~Client();                             // cancels all, joins the engine thread
    Client(const Client &) = delete; Client &operator=(const Client &) = delete;

    Job browse(BrowseRequest, Callback<VideoPage>);
    Job search(SearchRequest, Callback<VideoPage>);
    Job watch(std::string videoId, Callback<WatchResult>);
    Job comments(std::string videoId, std::string page, Callback<CommentPage>);
    Job player(std::string videoId, std::function<void(const PlayerInfo &)>);
    Job channel(std::string channelId, Callback<Channel>);
    Job channelByUrl(std::string url, Callback<Channel>);
    Job channelSubscribed(std::string channelId, Callback<bool>);
    Job channelList(std::string browseId, std::string page, Callback<ChannelPage>);
    Job searchChannels(std::string query, Callback<ChannelPage>);
    Job playlists(std::string resourceId, std::string page, std::string params, Callback<PlaylistPage>);
    Job searchPlaylists(std::string query, Callback<PlaylistPage>);
    Job account(Callback<Account>);
    Job action(Action, std::string targetId, Callback<Done>);
    Job postComment(std::string createParams, std::string text, Callback<Done>);
    Job editPlaylist(std::string playlistId, bool add, std::string id, Callback<Done>);
    Job votes(std::string videoId, Callback<Votes>);                 // RYD
    Job suggestions(std::string query, Callback<std::vector<std::string>>);

    void setLocale(std::string hl, std::string gl);                  // clears caches
    Auth &auth();
    void runOnce(int maxWaitMs);           // only when ownThread == false
};
}

// include/yt/auth.h
namespace yt {
struct DeviceCode { std::string verificationUrl, userCode; int intervalSecs = 5; };
class Auth {
public:
    // Requests a device code, reports it via onCode, then polls at the server
    // interval (authorization_pending/slow_down) until granted / denied / canceled.
    Job signIn(std::function<void(const DeviceCode &)> onCode, Callback<Done> onDone);
    void signOut();                        // drops tokens; onStateChanged fires
    bool signedIn() const;                 // a refresh token is held
};
}
```

Value types (`include/yt/types.h`) are the `CT::` structs with `std::string`, `std::vector`,
`int64_t`; `CT::User` is renamed **`yt::Channel`**; `likeCount`/`dislikeCount` become
`std::optional<int64_t>` (no `-1` sentinel). Pages: `VideoPage{items, next}`,
`CommentPage{items, next, createCommentParams, disabled}`, `PlaylistPage`, `ChannelPage`,
`WatchResult{primary, related}`, `PlayerInfo{Result<vector<Stream>> streams;
Result<vector<Subtitle>> captions;}` (the two independent outcomes of today's `PlayerOutcome`),
`Votes{optional likes, dislikes}`. Catalog (`include/yt/catalog.h`): `topicFeeds()`,
`searchTypes()` returning plain structs (labels stay English defaults; UIs may relabel).

### 3.2 Threading & cancellation contract (normative)

1. Every `Client`/`Auth` method is callable from **any** thread; it only posts to the loop.
2. All network I/O, parsing and chain logic run on the **engine thread**.
3. Each callback runs **exactly once or never**: never if its `Job` was canceled before the
   delivery closure ran its gate. The gate runs **on the delivery thread** (inside the executor),
   so `cancel()` called *on the delivery thread* guarantees no later callback — the facade
   protocol MeeTube relies on today (`if (!live(job)) return;` MUST be first).
4. Callbacks are never invoked re-entrantly from inside the call that started them (cache hits
   go through `Loop::post`).
5. `~Client()` cancels every job, tears down curl handles **on the engine thread**, then joins.
   No callback runs after the destructor returns.
6. The transport is created **and destroyed on the engine thread** (via `ClientOptions::transport`,
   a factory), so thread-affine native APIs and curl multi handles never cross threads. The curl
   backend does `curl_global_init` once via `std::call_once` in its factory (refcounted by
   libcurl, so a consumer that also inits — MeeTube for QML thumbnails — is fine).

### 3.3 The Qt bridge (MeeTube side, `src/bridge/`, Qt 4.7.4)

- `Innertube` QObject keeps every QML-visible name; it owns one `yt::Client` configured with
  `deliver = {}` (engine thread) — bridge callbacks convert `yt::` → `CT::` **on the engine
  thread** (QString is reentrant), then hop to the GUI via `GuiExecutor` (the old `CallEvent` +
  `Dispatcher`, GUI half only), and gate on the facade's own `JobToken` there. Net placement of
  work is identical to today.
- Models, `VideoDetails` (optimistic like/subscribe/save with revert), `StreamSet`,
  `SearchSuggest`, `AccountDetails`, the API tree: moved unchanged in behaviour; `apiRef()` test
  seam becomes `client()` returning a `BridgeApi` with an injectable fake.
- `SettingsStore` stays app-side, persists `SessionState` from `onStateChanged` plus app prefs.
- `net/curl*` Qt NAM (QML thumbnails, and the player until Phase 7) moves to `src/bridge/net/`.
- `MEETUBE_DEBUG` parsing lives in the app; it builds the `Logger`.

### 3.4 Build & packaging

- `libinnertube/CMakeLists.txt`: `project(innertube VERSION 0.1.0 LANGUAGES CXX)`, configurable
  **standalone** (`cmake -S libinnertube`) and as a subdirectory. Inputs only via options/targets:
  `CURL::libcurl` (consumer may pre-define it — MeeTube's N9 build points it at the static
  curl+wolfSSL archives), `INNERTUBE_GLAZE_DIR` (default: bundled submodule), `INNERTUBE_BUILD_TESTS`,
  `INNERTUBE_BUILD_MEDIA`, `INNERTUBE_BUILD_EXAMPLES`.
- Install/export: `innertubeConfig.cmake` + `innertube::innertube`/`innertube::media` targets.
- **Qt ban** enforced by a test: no `#include <Q` under `libinnertube/`, and the configured target's
  link interface contains no `Qt`.
- Example `examples/yt-cli` (feed / search / streams / signin) doubles as the smoke test that the
  public API is usable without any framework.
- `INNERTUBE_HTTP_BACKEND` (§3.5): `curl` (default; links `CURL::libcurl`), `none` (no curl at
  all — the consumer must pass `ClientOptions::transport`), or a path to the consumer's own `.cpp`
  defining `yt::makeDefaultTransport` (+ `INNERTUBE_HTTP_BACKEND_LIBS` for its link deps).

### 3.5 Network backends (`include/yt/transport.h`)

**Level.** The seam is HTTP request/response, not sockets: that is everything the library does
on the wire (youtubei POSTs, OAuth forms, RYD/suggest/timedtext GETs, ranged googlevideo GETs).

**What a backend does — and deliberately does not.** A backend moves one HTTP exchange: send the
request line + headers + body, deliver the response status + headers + *decoded* body, report
how it ended. Everything that is *policy* stays in the library so no backend can get it wrong:

| Concern | Owner | How |
|---|---|---|
| Allowed schemes (http/https only — SSRF guard) | library | checked before `start()` and on every redirect hop |
| Redirects (bounded; only credential-free GETs follow — never with `Authorization`/`Cookie`, never for POSTs, whose bodies may carry OAuth secrets) | library | backends **must not** follow redirects; 3xx is delivered as is |
| Body cap (32 MB) | library | `onBody()` returns `false` → backend aborts |
| Timeouts | library | engine-loop timer → `cancel()` → `ErrorCode::Timeout` |
| Cancellation/late callbacks | library | per-request gate on the engine thread; late sink calls are dropped |
| Thread marshalling | library | sink calls from foreign threads are posted to the engine thread |
| TLS, trust store, proxy, IP family, HTTP/2, keep-alive, gzip | backend | `TransportConfig` hints; must not send `Expect: 100-continue` |

**Interface (C++17):**

```cpp
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
// for one request, always in order: onHead once, onBody 0..n, onDone exactly once.
class HttpResponseSink {
public:
    virtual ~HttpResponseSink() = default;
    virtual void onHead(int status, std::vector<HttpHeader> headers) = 0;   // this response (no redirect following)
    virtual bool onBody(const char *data, size_t size) = 0;                 // false = abort → onDone(Canceled)
    virtual void onDone(TransportStatus status) = 0;
};

// Engine-thread integration for loop-driven backends (curl multi). Self-driven backends
// (own threads, OS callbacks) return nullptr from driver().
class LoopDriver {
public:
    virtual ~LoopDriver() = default;
    virtual void wait(int maxWaitMs) = 0;  // engine thread: block for I/O ≤ maxWaitMs or until wake(), then dispatch
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
struct TransportConfig {
    std::string caBundlePath;              // empty = backend/OS default trust store
    IpResolve ipResolve = IpResolve::Any;
    std::string proxy;                     // empty = direct / system setting
    Logger log;
};
using TransportFactory = std::function<std::unique_ptr<HttpTransport>(const TransportConfig &)>;
std::unique_ptr<HttpTransport> makeDefaultTransport(const TransportConfig &);   // build-time backend; nullptr for "none"
}
```

**Selection.** Build time: `-DINNERTUBE_HTTP_BACKEND=curl|none|path/to/my_transport.cpp` decides
what `makeDefaultTransport` returns (and whether libcurl is linked). Runtime:
`ClientOptions::transport = [](const TransportConfig &c){ return std::make_unique<MyTransport>(c); }`
overrides it per `Client` (a test, a second backend, an app that already owns an HTTP stack).

**Conformance.** `innertube_conformance` (static lib, no test framework):
`std::vector<std::string> runTransportConformance(const TransportFactory &)` returns the failed
checks by name. It runs against a scripted loopback HTTP server and checks: GET/POST bodies,
request + response headers, **no redirect following**, byte-exact URLs (`%2C`/`%3D`), 1 MB
streamed in order, `onBody→false` aborts, cancel mid-transfer, connect refused → `Connect`,
unresolvable host → `Resolve`, truncated body → error, no `Expect: 100-continue`, 32 concurrent
requests, no overlapping/out-of-order sink calls, no sink call after the transport's destructor.
Any backend — built-in or the consumer's — is "supported" exactly when this returns empty.

**Shipped backends.** `curl` (default — today's hardened `CurlNetworkReply` options; its
`LoopDriver` is `curl_multi_poll`/`curl_multi_wakeup`) and `examples/threaded-transport` (a
self-driven reference: one blocking `curl_easy_perform` per request on its own thread — proves the
foreign-thread path and is the template for wrapping blocking/native APIs). Both pass the suite in CI.

**Later, on demand (not in this plan's scope):** native backends (WinHTTP, NSURLSession, Android
via JNI, `emscripten_fetch`) — each its own small plan when a target needs it; and a **builtin**
backend in the baileys-cpp mould — a `sockets.h` free-function socket layer (posix/win32/your
own `.cpp`) + a minimal HTTP/1.1 client + TLS (wolfSSL/mbedTLS) — for targets that have neither
libcurl nor a native HTTP API. Both are just more `HttpTransport` implementations; the core does
not change.

---

## 4. Migration strategy

Strangler, bottom-up, **both targets green + ctest green at every commit** (the 07-04 rework
discipline). The old `meetube-core` shrinks phase by phase into the bridge.

| Phase | Moves into the lib | MeeTube stays working via |
|---|---|---|
| 0 | scaffold, doctest, Qt-ban guard, `job.h`/`status`/`clientconfig`/`catalog` | nothing changes |
| 1 | `yt::` types, parsers, bodies | `bridge/convert.{h,cpp}` (`yt→CT`) used by the still-Qt chains |
| 2 | `SessionState`, `ContextBuilder`, logging, `Result/Error` | Qt `core::Http` reads `yt::SessionState` |
| 3 | `net::Loop`, `yt::HttpTransport` + policy `net::HttpClient` (tested on a fake transport), curl backend + conformance suite, threaded reference backend | not yet wired — app still on QNAM |
| 4 | chains on std `IHttp`; std `FakeHttp` | old Qt `core::Http` implements the std `IHttp` (converts at its edge) |
| 5 | `yt::Client`, `Auth` (+ token-expiry refresh), catalog, `yt-cli` | not yet wired |
| 6 | — | **switch**: bridge on `yt::Client`; delete Qt `core::Http`, `WorkerHost` worker half, `meetube-core` |
| 7 | `innertube::media` | player re-plumbed onto it; **device verification** |
| 8 | install/export, CI matrix, sanitizers, fuzz, docs, repo split, 0.1.0 | consumes the submodule |

## 5. Risks

- **Device regressions in Phase 6/7** (threading switch, curl driver switch, player sources).
  Mitigation: phases end with the device checklist from `memory/n9-device-deploy` +
  `smoke-covers-home-only` (detail pages verified on device, not only by the headless smoke).
- **curl driver change**: `curl_multi_poll`/`perform` loop instead of socket-action on a Qt loop.
  Same libcurl, same easy options; HTTP/2 multiplexing via `PIPEWAIT` unaffected. Redirects move
  from libcurl (`FOLLOWLOCATION`) into the library policy — googlevideo `redirector` 302s must
  still play (Phase 6/7 device check).
- **Backend contract drift** (a third-party backend that follows redirects, calls back after
  destruction, or overlaps callbacks) — the conformance suite is the contract; the library
  additionally gates every late/foreign callback on the engine thread.
- **GUI-thread cost**: conversion stays on the engine thread (D7) — re-measure feed delivery with
  `MEETUBE_DEBUG=startup` against the 2026-09 baseline (feed parse 118 ms on a 2.2 MB feed).
- **License** (D2) blocks "any project" adoption if left GPLv3 — owner decision.
- **ToS/credential note**: the TV OAuth client id/secret are public scraped values (as in yt-dlp);
  shipping them in a library is the same exposure, but rotating them must not need a release (D11
  covers versions; extend to OAuth creds in `ClientOptions` if they rotate).
