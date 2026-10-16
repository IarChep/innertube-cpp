# JSON migration onto yt::json, Glaze removed (plan 2 of 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move every JSON read and write in the library onto `yt::json` (plan 1), so the transport indexes each reply once, the parsers read it by path, request bodies are written by `json::Writer` — and delete Glaze, `json/shapes/` and the scanner.

**Architecture:** Bottom-up in five green steps. Task 1: `net::HttpClient`'s envelope builds the `json::Doc` and `core::Reply` carries it (a malformed 2xx body now fails as `InvalidResponse`). Task 2: the YouTube helpers over `json::Value` (`gj::textOf`, `lastThumbUrl`, `flexInt`, `first`…) and the readers with a fixed root (player, json3, suggestions, OAuth, QR, RYD, `rawPost`). Task 3: the renderer converters and every list/watch/channel/playlist/comment/account parser, with `Value::find` replacing the scanner visitors. Task 4: `json::Writer` writes the bodies and the context. Task 5: Glaze, the shapes and the scanner are deleted; docs follow; a differential dump proves the parsers' output did not change.

**Tech Stack:** C++23 implementation / C++17 public headers, `yt::json` (plan 1), doctest, CMake + Ninja, libcurl backend.

**Spec:** [`docs/specs/2026-10-16-json-design.md`](../specs/2026-10-16-json-design.md) §6.2–§6.4, §10, §11. **Requires plan 1** ([`2026-10-16-json-core.md`](2026-10-16-json-core.md)) executed first: `src/json/json.{h,cpp}` with `Visit::Stop` and `json::Writer`.

Every task's patch below was applied in this order to the tree as it stood after plan 1 (commit `b5387a5` + plan 1), built, and run green in `build`; the end state was also run in `build-asan`, `build-tsan`, `build-none`, with the dead-proxy conformance run, a live `yt-cli`/`yt-watch`/`yt-suggest` session, and the differential dump of Task 5 (2 364 lines of parser output over 21 live answers and 22 fixtures: identical before and after).

## Global Constraints

- Public headers (`include/yt/`) unchanged; C++17 there (`tst_types`, `tst_link`). The library stays C++23 (dropping to a lower standard is not part of this plan).
- No new third-party dependency; after Task 5 the library depends on libcurl (default backend) only, tests on doctest.
- Parsers never fail: an absent `json::Value` reads as `""`/`0`/`false`; transport and envelope errors are decided once, in `HttpClient`.
- No recursion over untrusted JSON: walks are `Value::find` (iterative); documents deeper than `json::kMaxDepth` (128) are refused by the transport.
- URLs are byte-exact; credentials never reach a log; files holding tokens are written owner-only (yt-cli state).
- Style (AGENTS.md §5–§6): 4 spaces; `&`/`*` bind to the name; definition braces on their own line; `m_` members, `k` constants; `(int) x` casts; `nullptr`; comments say *why*; no `TODO`/`FIXME`.
- Done = warning-free build and all ctests green in `build`, `build-asan`, `build-tsan`, `build-none`; `http_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl` green; examples run against the live service (Task 5).
- Commit only when the repository owner has said so for this plan (AGENTS.md §10). Commit steps show the message; skip `git commit` if commits were not authorized. Trailer lines (both): `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN`.

## Review Focus

1. **A 2xx body that is not JSON** — cut short by a proxy, an HTML consent page, empty: `InvalidResponse`, never cached, never parsed into an empty list (`truncatedJsonIsInvalidAndUncached`, existing `htmlInvalid`, Task 1).
2. **A cached reply** is delivered with the same index, still asynchronously, and not re-indexed (`replyCarriesTheIndex`, Task 1).
3. **Parser output on real answers must not change** — the differential dump recorded before Task 2 and compared after Task 5 (Task 2 Step 1, Task 5 Step 5).
4. **Text with control bytes** typed or pasted into a comment or a search goes out escaped, not as invalid JSON (`control bytes are escaped`, Task 4).
5. **A signed-in yt-cli user after the upgrade**: the state file becomes `yt-cli-state.txt` (key=value); the old `yt-cli-state.json` is no longer read, so `yt-cli signin` once again — said in the commit message and README-visible usage line (Task 5).

Behaviour changes a consumer can see (all intended, all tested): malformed 2xx bodies fail as `InvalidResponse` (Task 1); `createCommentParams` is also found under `onResponseReceivedEndpoints`, where the old scanner never looked (Task 3); a document nested deeper than 128 levels is refused whole instead of having its deep part skipped (Tasks 1–2); control bytes in bodies are escaped (Task 4).

---

## File map

| File | Task | Change |
|---|---|---|
| `src/core/ihttp.h`, `src/net/httpclient.{h,cpp}`, `tests/fakehttp.h`, `tests/tst_http.cpp` | 1 | `Reply::doc` / `Reply::json()`; the envelope over `json::Doc`; the cache keeps the index |
| `src/json/ytjson.h`, `src/json/shapes/common.h` | 2, 5 | helpers over `json::Value` (Task 2); the Glaze half removed (Task 5) |
| `src/parsers/playerparser.{h,cpp}`, `src/json/json3.{h,cpp}`, `src/parsers/suggestparser.{h,cpp}` | 2 | fixed-root readers by path |
| `src/core/chains.cpp` | 2, 3 | OAuth/QR/RYD/rawPost reads (2); parsers fed `r.json()` (3) |
| `src/parsers/rendererinternal.h`, `rendererparser.h`, `continuation.{h,cpp}`, `videolist/playlist/channel/comment/account/watchparser.cpp` | 3 | converters and parsers over `json::Value` + `Value::find` |
| `tests/tst_parsers.cpp`, `tests/parserpayloads.h` | 2, 3 | fixtures via `J(text)`; two malformed inline fixtures fixed |
| `src/innertube/bodies.{h,cpp}`, `src/innertube/contextbuilder.cpp`, `tests/tst_bodies.cpp` | 4 | `json::Writer` |
| `src/json/jsonscan.h`, `src/json/tokenscan.h`, `src/json/shapes/`, `third_party/glaze` | 5 | deleted |
| `CMakeLists.txt`, `tests/CMakeLists.txt`, `examples/CMakeLists.txt`, `examples/yt-cli/main.cpp`, `.gitignore`, `AGENTS.md`, `README.md` | 5 | Glaze gone from the build and the docs; yt-cli state as key=value |

How to apply a task: save its patch block to `/tmp/tN.patch` (everything between the ```` fences), then `git apply --include='tests/*' /tmp/tN.patch` for the tests and `git apply --exclude='tests/*' /tmp/tN.patch` for the rest, in the order the steps say. `git apply --check` first; a patch that does not apply means the tree drifted from plan 1's end state — stop and reconcile by hand, the patch shows the intended result.

---

### Task 1: one JSON index per reply

**Files:**
- Modify: `src/core/ihttp.h`, `src/net/httpclient.h`, `src/net/httpclient.cpp`, `tests/fakehttp.h`, `tests/tst_http.cpp`

**Interfaces:**
- Consumes: `yt::json::Doc` (plan 1), `Doc(std::shared_ptr<const std::string>)`.
- Produces: `core::Reply::doc` (`std::shared_ptr<const json::Doc>`, set for every well-formed JSON reply of `post`/`postForm`/`get`, also an `"error"` one; null for `fetch()` and non-JSON) and `json::Value core::Reply::json() const` (absent when `doc` is null). `HttpClient::CacheEntry` gains `doc`. `FakeHttp` sets `doc` for well-formed bodies.

- [ ] **Step 1: Apply the tests**

Save the patch below as `/tmp/t1.patch`, then: `git apply --include='tests/*' /tmp/t1.patch`

- [ ] **Step 2: See them fail**

Run: `cmake --build build 2>&1 | grep -m2 error`
Expected: `'const struct yt::core::Reply' has no member named 'doc'` (tst_http, fakehttp.h).

- [ ] **Step 3: Implement**

Run: `git apply --exclude='tests/*' /tmp/t1.patch`

- [ ] **Step 4: Run, and prove the regression test bites**

Run: `cmake --build build -j && (cd build && ctest -j8)` — expected all green.
Then: `git stash push src/net/httpclient.cpp src/net/httpclient.h && cmake --build build --target tst_http; ./build/tests/tst_http -tc='truncated*'` — expected `CHECK_FALSE( a.r.ok ) is NOT correct` (the old envelope only looked at the first byte, the truncated body passed and was cached); `git stash pop`, rebuild, green again.

- [ ] **Step 5: Commit**

```bash
git add src/core/ihttp.h src/net/httpclient.h src/net/httpclient.cpp tests/fakehttp.h tests/tst_http.cpp
git commit -m "fix(lib): a 2xx body that is not JSON fails; each reply is indexed once" -m "- The envelope only checked that a body began with '{' or '['; a 2xx body cut short
  without a transport error was parsed as an empty list and cached. The envelope now
  builds the reply's json::Doc (one validating pass) and fails anything malformed as
  InvalidResponse.
- Reply carries that index (Reply::json()); the cache keeps it, so a hit is not
  re-indexed. Parsers move onto it in the next commits." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

````diff
diff --git a/src/core/ihttp.h b/src/core/ihttp.h
index 9fe6414..0da6b6d 100644
--- a/src/core/ihttp.h
+++ b/src/core/ihttp.h
@@ -8,6 +8,7 @@
 #include "core/session.h"
 #include "innertube/clientconfig.h"
 #include "innertube/contextbuilder.h"
+#include "json/json.h"
 #include <functional>
 #include <memory>
 #include <string>
@@ -16,14 +17,19 @@
 namespace yt::core {
 
 // One finished request. body is never null and stays populated on !ok (OAuth reads
-// its error code from a failed reply); visitorData is pre-extracted by the envelope scan.
+// its error code from a failed reply); visitorData is pre-extracted by the envelope.
+// doc is the body's JSON index, built once by the transport for every JSON reply (post,
+// postForm, get) that is well-formed — also an "error" one; null for fetch() and for a
+// body that is not JSON. Parsers read json(), never the body again.
 struct Reply {
     bool ok = false;
     std::shared_ptr<const std::string> body = std::make_shared<std::string>();
+    std::shared_ptr<const json::Doc> doc;
     ErrorCode code = ErrorCode::Transport;   // meaningful when !ok
     std::string error;
     long httpStatus = 0;
     std::string visitorData;
+    json::Value json() const { return doc ? doc->root() : json::Value(); }
 };
 using HttpFn = std::function<void(const Reply &)>;
 
diff --git a/src/net/httpclient.cpp b/src/net/httpclient.cpp
index 34bc63f..82f8ee9 100644
--- a/src/net/httpclient.cpp
+++ b/src/net/httpclient.cpp
@@ -1,8 +1,7 @@
 #include "net/httpclient.h"
 #include "core/log.h"
 #include "innertube/contextbuilder.h"
-#include "json/jsonscan.h"
-#include "json/shapes/common.h"
+#include "json/json.h"
 #include "net/url.h"
 #include <algorithm>
 #include <atomic>
@@ -82,61 +81,31 @@ bool isRedirect(int status)
     return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
 }
 
-// The single top-level envelope scan over a delivered body (MeeTube's makeReply,
-// minus its transport branches which the caller has already handled).
+// The envelope of a delivered JSON body (MeeTube's makeReply, minus its transport
+// branches which the caller has already handled). The one pass over the body: it builds
+// the JSON index the parsers then read, so a body that is not well-formed JSON (empty,
+// an HTML consent page, cut short) fails here as InvalidResponse — it used to reach the
+// parsers and come back as a spuriously empty (and cached) list.
 void envelope(Reply &out, bool wantVisitor)
 {
-    const std::string_view sv(*out.body);
-    // Cheap validity check instead of a whole-document validate pass: a JSON value must
-    // begin with '{' or '['. Empty / HTML (consent page) -> "invalid JSON response". A
-    // body that opens like JSON but is truncated degrades to "nothing collected" in the
-    // single-pass parsers (an empty result, not an error).
-    const char *p = gj::scan::skipWs(sv.data(), sv.data() + sv.size());
-    const char *e = sv.data() + sv.size();
-    if (p >= e || (*p != '{' && *p != '[')) {
+    std::shared_ptr<const json::Doc> doc = std::make_shared<const json::Doc>(out.body);
+    if (!doc->ok()) {
         out.ok = false;
         out.code = ErrorCode::InvalidResponse;
         out.error = "invalid JSON response";
         return;
     }
-    struct EnvelopeScan {
-        std::string_view error, responseContext;
-        bool wantVisitor;
-        void enter(int) {}
-        void leave(int) {}
-        gj::scan::Action what(std::string_view k, int depth)
-        {
-            if (depth != 0) return gj::scan::Action::Skip;
-            if (k == "error" && error.empty()) return gj::scan::Action::Capture;
-            if (wantVisitor && k == "responseContext" && responseContext.empty())
-                return gj::scan::Action::Capture;
-            return gj::scan::Action::Skip;
-        }
-        void capture(std::string_view k, std::string_view v, int)
-        { if (k == "error") error = v; else responseContext = v; }
-    } es{ {}, {}, wantVisitor };
-    gj::scan::document(sv, es);
-
+    out.doc = doc;
     // A top-level "error" of ANY type fails the reply; its message is used only when it
-    // is an object with a string message. The body stays populated (OAuth reads it).
-    if (!es.error.empty()) {
+    // is an object with a string message. The doc stays (OAuth reads its error code).
+    if (const json::Value err = doc->root()["error"]) {
         out.ok = false;
         out.code = ErrorCode::Api;
         out.error = "InnerTube error";
-        if (es.error.front() == '{') {
-            gj::ErrorEnvelope err{};
-            gj::readJson(err, es.error);
-            if (err.message) out.error = *err.message;
-        }
+        if (const json::Value m = err["message"]; m.isString()) out.error = m.string();
         return;
     }
-    if (wantVisitor && !es.responseContext.empty() && es.responseContext.front() == '{') {
-        const std::string_view vdv = gj::scan::topLevelValue(es.responseContext, "visitorData");
-        if (!vdv.empty() && vdv.front() == '"') {
-            std::string vd;
-            if (!glz::read<gj::kIn>(vd, vdv)) out.visitorData = std::move(vd);
-        }
-    }
+    if (wantVisitor) out.visitorData = doc->root().path("responseContext", "visitorData").string();
     out.ok = true;
 }
 
@@ -325,13 +294,15 @@ void HttpClient::post(std::string_view endpoint, ClientId client, std::string bo
         auto hit = m_cache.find(key);
         if (hit != m_cache.end() && hit->second.expires > Loop::Clock::now()) {
             std::weak_ptr<char> life = m_life;
+            std::shared_ptr<const json::Doc> doc = hit->second.doc;
             std::shared_ptr<const std::string> body = hit->second.body;
-            m_loop.post([life, job, done, body]() {
+            m_loop.post([life, job, done, body, doc]() {
                 if (life.expired() || !core::live(job)) return;
                 Reply r;
                 r.ok = true;
                 r.httpStatus = 200;
                 r.body = body;
+                r.doc = doc;
                 done(r);
             });
             return;
@@ -545,7 +516,7 @@ void HttpClient::complete(const std::shared_ptr<Exchange> &ex, Reply r)
 
     if (r.ok && !ex->cacheKey.empty() && ex->ttlSecs > 0 && ex->gen == m_gen) {
         if (!m_cache.count(ex->cacheKey)) m_cacheOrder.push_back(ex->cacheKey);
-        m_cache[ex->cacheKey] = CacheEntry{r.body, Loop::Clock::now() + std::chrono::seconds(ex->ttlSecs)};
+        m_cache[ex->cacheKey] = CacheEntry{r.body, r.doc, Loop::Clock::now() + std::chrono::seconds(ex->ttlSecs)};
         while (m_cacheOrder.size() > kMaxCacheEntries) {
             m_cache.erase(m_cacheOrder.front());
             m_cacheOrder.pop_front();
diff --git a/src/net/httpclient.h b/src/net/httpclient.h
index 399888b..82e5768 100644
--- a/src/net/httpclient.h
+++ b/src/net/httpclient.h
@@ -62,7 +62,8 @@ private:
     // Each waiter has its own deadline: a request coalesced onto an older exchange still
     // gets the full timeout, not what is left of the first one's.
     struct Waiter { core::JobToken job; core::HttpFn fn; Loop::Clock::time_point deadline; };
-    struct CacheEntry { std::shared_ptr<const std::string> body; Loop::Clock::time_point expires; };
+    // The index is cached with the body: a hit is parsed without another pass.
+    struct CacheEntry { std::shared_ptr<const std::string> body; std::shared_ptr<const json::Doc> doc; Loop::Clock::time_point expires; };
 
     std::shared_ptr<Exchange> newExchange(HttpRequest req, core::JobToken job, core::HttpFn fn);
     void launch(const std::shared_ptr<Exchange> &ex);
diff --git a/tests/fakehttp.h b/tests/fakehttp.h
index 3bd74c5..b014e36 100644
--- a/tests/fakehttp.h
+++ b/tests/fakehttp.h
@@ -58,6 +58,7 @@ public:
             r.ok = true;
             r.httpStatus = 200;
             r.body = std::make_shared<const std::string>(m_getBody);
+            r.doc = docOf(r.body);
         } else {
             r.error = "no get fixture";
         }
@@ -103,6 +104,12 @@ public:
 
 private:
     struct Queued { std::string body; long status; std::string visitor; };
+    // Like HttpClient's envelope: a well-formed JSON body carries its index.
+    static std::shared_ptr<const yt::json::Doc> docOf(const std::shared_ptr<const std::string> &body)
+    {
+        auto d = std::make_shared<const yt::json::Doc>(body);
+        return d->ok() ? d : nullptr;
+    }
     struct Waiter { yt::core::JobToken job; yt::core::HttpFn fn; yt::core::Reply reply; };
     void deliver(Waiter &w)
     {
@@ -121,6 +128,7 @@ private:
             r.httpStatus = q.status;
             r.visitorData = q.visitor;
             r.body = std::make_shared<const std::string>(std::move(q.body));
+            r.doc = docOf(r.body);
             if (q.status == 0) { r.code = yt::ErrorCode::Transport; r.error = "network down"; r.body = std::make_shared<const std::string>(); }
             else if (q.status >= 400) { r.code = yt::ErrorCode::Api; r.error = "HTTP " + std::to_string(q.status); }
             else r.ok = true;
diff --git a/tests/tst_http.cpp b/tests/tst_http.cpp
index cd5535a..7a18383 100644
--- a/tests/tst_http.cpp
+++ b/tests/tst_http.cpp
@@ -134,6 +134,35 @@ TEST_CASE("htmlInvalid") {
     CHECK_FALSE(g.r.ok);
     CHECK(g.r.code == ErrorCode::InvalidResponse);
 }
+// A 2xx body cut short without a transport error (a proxy closing early) used to pass the
+// envelope check — it only looked at the first byte — and parse as an empty, cached list.
+TEST_CASE("truncatedJsonIsInvalidAndUncached") {
+    Fx f;
+    f.ft.script(kBrowse, ok("{\"contents\":[{\"a\":1},"));
+    f.ft.script(kBrowse, ok("{\"contents\":[{\"a\":1},"));
+    Got a, b;
+    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
+    f.pump();
+    CHECK_FALSE(a.r.ok);
+    CHECK(a.r.code == ErrorCode::InvalidResponse);
+    CHECK(a.r.doc == nullptr);
+    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(b));
+    f.pump();
+    CHECK(f.ft.started.size() == 2);   // not served from the cache
+}
+TEST_CASE("replyCarriesTheIndex") {
+    Fx f;
+    f.ft.script(kBrowse, ok("{\"x\":{\"y\":\"z\"}}"));
+    Got a, b;
+    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(a));
+    f.pump();
+    REQUIRE(a.r.ok);
+    CHECK(a.r.json()["x"]["y"].str() == "z");
+    f.h().post("browse", ClientId::WEB, "{\"browseId\":\"A\"}", yt::core::newJob(), rec(b));
+    f.pump();
+    CHECK(b.r.doc == a.r.doc);           // a cache hit is not indexed again
+    CHECK(f.ft.started.size() == 1);
+}
 TEST_CASE("timeout") {
     Fx f; f.o.timeoutMs = 50;
     FakeExchange x; x.hang = true; f.ft.script(kBrowse, x);
````

---

### Task 2: helpers over `json::Value`; the fixed-root readers

**Files:**
- Modify: `src/json/ytjson.h` (helpers appended; `withScheme` moves here), `src/json/shapes/common.h` (`withScheme` removed), `src/parsers/playerparser.{h,cpp}`, `src/json/json3.{h,cpp}`, `src/parsers/suggestparser.{h,cpp}`, `src/core/chains.cpp` (tokenGrantOf, oauthDeviceCode, fetchSignInQr, fetchDislikes, rawPost, the player/suggestion call sites), `tests/tst_parsers.cpp`

**Interfaces:**
- Consumes: Task 1's `Reply::json()`.
- Produces (namespace `yt::gj`, `json/ytjson.h`): `std::string textOf(json::Value)`, `std::string withScheme(std::string)`, `std::string lastThumbUrl(json::Value)`, `std::string lastSourceUrl(json::Value)`, `std::string firstSourceUrl(json::Value)`, `std::int64_t flexInt(json::Value)`, `json::Value first(json::Value, std::string_view key)`. Parser signatures: `PlayerPart parsePlayerResponse(json::Value root)`, `std::vector<Cue> parseJson3(std::string_view body)`, `std::vector<std::string> parseSuggestions(json::Value)`. Tests: `static json::Value J(std::string_view text)` in `tst_parsers.cpp` (each Doc owns a copy of the text and lives until the suite ends).

- [ ] **Step 1: Record the parsers' output before migrating them**

The differential dump runs every parser over the live corpus (`bench/corpus.tar.zst`, plan 1) and the fixtures and prints every field. Build it against the library as it is now (old parsers, Glaze still present), and keep the output:

```bash
mkdir -p /tmp/jd && zstd -dc bench/corpus.tar.zst | tar -C /tmp/jd -x
cat > /tmp/jd/dump.cpp <<'EOF'
// Differential dump: every parser over every document, every yt:: field printed.
// -DOLD: the Glaze/scanner library (string API); else the yt::json library (Value API).
#include "parsers/rendererparser.h"
#include "parsers/playerparser.h"
#include "parsers/continuation.h"
#include "json/json3.h"
#ifndef OLD
#include "json/json.h"
#endif
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace yt;
static std::string q(const std::string &s) { std::string o; for (char c : s) { if (c == '\n') o += "\\n"; else o += c; } return o; }
static void ref(const char *k, const ChannelRef &c) { std::printf("  %s{%s|%s|%s|%d}\n", k, c.id.c_str(), q(c.name).c_str(), c.avatarUrl.c_str(), c.subscribed); }
static void item(const VideoItem &v)
{
    std::printf(" V %s|%s|%s|%s|%s|%s|%lld|%lld\n", v.id.c_str(), q(v.title).c_str(), v.thumbnailUrl.c_str(), v.durationText.c_str(),
                q(v.viewText).c_str(), q(v.publishedText).c_str(), (long long) v.durationSecs, (long long) v.viewCount);
    ref("ch", v.channel);
}
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i]); std::stringstream ss; ss << f.rdbuf(); const std::string body = ss.str();
        std::printf("=== %s\n", argv[i]);
#ifdef OLD
#define IN body
#else
        const json::Doc doc(body);
#define IN doc.root()
#endif
        std::string tok;
        for (const VideoItem &v : parseVideoList(IN, &tok)) item(v);
        std::printf(" videos.token %s\n", tok.c_str());
        for (const Playlist &p : parsePlaylistList(IN, &tok)) { std::printf(" P %s|%s|%s|%lld\n", p.id.c_str(), q(p.title).c_str(), p.thumbnailUrl.c_str(), (long long) p.videoCount); ref("own", p.owner); }
        std::printf(" playlists.token %s\n", tok.c_str());
        for (const Channel &c : parseUserList(IN, &tok)) std::printf(" U %s|%s|%s|%s|%s|%s\n", c.id.c_str(), q(c.name).c_str(), c.handle.c_str(), c.avatarUrl.c_str(), q(c.subscriberText).c_str(), q(c.description).c_str());
        std::printf(" users.token %s\n", tok.c_str());
        std::string cp;
        for (const CommentThread &t : parseComments(IN, &tok, &cp)) { std::printf(" C %s|%s|%s|%s|%s|%d\n", t.comment.id.c_str(), q(t.comment.text).c_str(), t.comment.publishedText.c_str(), t.comment.likeText.c_str(), t.replyText.c_str(), t.comment.mine); ref("au", t.comment.author); }
        std::printf(" comments.token %s params %s\n", tok.c_str(), cp.c_str());
        Video w; std::string rt;
        parseWatchPage(IN, &w, &rt);
        std::printf(" W %s|%s|%s|%s|%s|%s|%lld|%d\n", q(w.title).c_str(), q(w.description).c_str(), q(w.viewText).c_str(), w.publishedText.c_str(), w.likeText.c_str(), rt.c_str(), (long long) w.likeCount.value_or(-7), (int) w.myRating);
        ref("wch", w.channel);
        for (const VideoItem &v : w.related.items) item(v);
        const Channel c = parseChannel(IN);
        std::printf(" CH %s|%s|%s|%s|%s|%s|%s|%s|%d sub=%d\n", c.id.c_str(), q(c.name).c_str(), c.handle.c_str(), q(c.description).c_str(), c.avatarUrl.c_str(), c.bannerUrl.c_str(), q(c.subscriberText).c_str(), q(c.videoCountText).c_str(), c.subscribed, parseChannelSubscribed(IN));
        const Playlist pp = parsePlaylistPage(IN);
        std::printf(" PP %s|%s|%s|%lld\n", q(pp.title).c_str(), q(pp.description).c_str(), pp.thumbnailUrl.c_str(), (long long) pp.videoCount); ref("ppown", pp.owner);
        const AccountInfo a = parseAccountsList(IN);
        std::printf(" A %s|%s|%s|%s\n", a.name.c_str(), a.handle.c_str(), a.avatarUrl.c_str(), a.channelId.c_str());
        std::printf(" R %s\n", parseResolvedBrowseId(IN).c_str());
        std::printf(" T %s | %s\n", findContinuationToken(IN).c_str(), findContinuationTokenUnder(IN, "engagementPanels").c_str());
        const PlayerPart p = parsePlayerResponse(IN);
        std::printf(" PL %s|%s|%d|%lld|%s|final=%d|%s|%s|%s|%s|%lld|%s|%s|%s\n", p.status.status.c_str(), q(p.status.reason).c_str(), (int) p.status.kind, (long long) p.status.scheduledStartSecs, q(p.status.slateText).c_str(), p.final,
                    p.hlsUrl.c_str(), p.dashUrl.c_str(), p.sabrUrl.c_str(), p.sabrConfig.c_str(), (long long) p.expiresInSecs, p.storyboardSpec.c_str(), p.watchtimeUrl.c_str(), p.playbackUrl.c_str());
        const VideoDetails &d = p.details;
        std::printf("  D %s|%s|%s|%s|%s|%s|%lld|%lld|%d%d%d\n", d.id.c_str(), q(d.title).c_str(), d.author.c_str(), d.channelId.c_str(), q(d.description).c_str(), d.thumbnailUrl.c_str(), (long long) d.lengthSecs, (long long) d.viewCount, d.isLive, d.isLiveContent, d.isUpcoming);
        for (const Format &fm : p.formats)
            std::printf("  F %d|%s|%d|%s|%s|%s|%dx%d@%d|%lld|%lld|%lld|%lld|%s|%s|%d%d|%lld|%s|%s\n", fm.itag, fm.url.c_str(), fm.sealed, fm.mimeType.c_str(), fm.codecs.c_str(), fm.qualityLabel.c_str(), fm.width, fm.height, fm.fps,
                        (long long) fm.bitrate, (long long) fm.averageBitrate, (long long) fm.contentLength, (long long) fm.durationMs,
                        fm.initRange ? (std::to_string(fm.initRange->first) + "-" + std::to_string(fm.initRange->last)).c_str() : "-",
                        fm.indexRange ? (std::to_string(fm.indexRange->first) + "-" + std::to_string(fm.indexRange->last)).c_str() : "-",
                        fm.hasVideo, fm.hasAudio, (long long) fm.lastModified, fm.xtags.c_str(),
                        fm.audio ? (fm.audio->id + "," + fm.audio->name + "," + fm.audio->language + "," + std::to_string(fm.audio->isDefault) + std::to_string(fm.audio->original) + std::to_string(fm.audio->autoDubbed) + std::to_string(fm.audio->drc)).c_str() : "-");
        for (const CaptionTrack &ct : p.captions) std::printf("  CT %s|%s|%s|%d%d\n", ct.url.c_str(), ct.language.c_str(), ct.name.c_str(), ct.autoGenerated, ct.translatable);
        for (const Cue &cue : parseJson3(body)) std::printf("  J %lld-%lld %s\n", (long long) cue.startMs, (long long) cue.endMs, q(cue.text).c_str());
    }
}
EOF
cmake --build build -j
g++ -std=c++23 -O1 -DOLD -Isrc -Iinclude -isystem third_party/glaze/include /tmp/jd/dump.cpp build/libinnertube.a -lcurl -o /tmp/jd/dump_old
/tmp/jd/dump_old /tmp/jd/corpus/* tests/fixtures/* > /tmp/jd/old.txt && wc -l /tmp/jd/old.txt
```
Expected: about 2 360 lines.

- [ ] **Step 2: Apply the tests and see them fail**

Save the patch as `/tmp/t2.patch`; `git apply --include='tests/*' /tmp/t2.patch`; `cmake --build build 2>&1 | grep -m1 error`
Expected: `could not convert … to 'yt::json::Value'` at the first `parsePlayerResponse(J(…))`.

- [ ] **Step 3: Implement**

`git apply --exclude='tests/*' /tmp/t2.patch`

- [ ] **Step 4: Run**

`cmake --build build -j && (cd build && ctest -j8)` — expected all green (`tst_parsers` included: the list parsers are untouched in this task and keep their string API).

- [ ] **Step 5: Commit**

```bash
git add src/json/ytjson.h src/json/shapes/common.h src/parsers/playerparser.h src/parsers/playerparser.cpp src/json/json3.h src/json/json3.cpp src/parsers/suggestparser.h src/parsers/suggestparser.cpp src/core/chains.cpp tests/tst_parsers.cpp
git commit -m "lib: player, captions, suggestions and OAuth read through yt::json" -m "The readers whose root is fixed read it by path from the reply's index: /player
(the offline slate is gj::first instead of a scanner visitor), json3, suggestions, the
OAuth device code and token, the sign-in QR, RYD votes, and rawPost's \"context\"
check. gj gains textOf/lastThumbUrl/lastSourceUrl/flexInt/first over json::Value.
A document deeper than json::kMaxDepth is refused whole (the slate test now expects
nothing parsed, still without recursion)." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

````diff
diff --git a/src/core/chains.cpp b/src/core/chains.cpp
index 1f5e34e..1915709 100644
--- a/src/core/chains.cpp
+++ b/src/core/chains.cpp
@@ -8,11 +8,9 @@
 #include "innertube/catalog.h"
 #include "innertube/clientconfig.h"
 #include "innertube/playerjs.h"
+#include "json/json.h"
 #include "json/json3.h"
-#include "json/jsonscan.h"
-#include "json/shapes/account.h"
-#include "json/shapes/oauth.h"
-#include "json/shapes/ryd.h"
+#include "json/ytjson.h"
 #include "parsers/continuation.h"
 #include "parsers/playerparser.h"
 #include "parsers/rendererparser.h"
@@ -28,7 +26,6 @@ namespace {
 
 Error errorOf(const Reply &r) { return Error{r.code, r.error, r.httpStatus}; }
 Error unavailable(const char *what) { return Error{ErrorCode::Unavailable, what, 0}; }
-std::string str(const std::optional<std::string> &s) { return s ? *s : std::string(); }
 bool isPng(const std::string &b) { return b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0; }
 // "browse", "account/accounts_list": path segments of [A-Za-z0-9_] — nothing that could
 // leave the youtubei path or add a query when appended to the base URL.
@@ -174,12 +171,12 @@ TokenGrant tokenGrantOf(const Reply &r)
     g.transportOk = r.ok;
     g.httpStatus = r.httpStatus;
     if (!r.ok) g.transportError = r.error;
-    oj::Token tok{};
-    gj::readJsonDoc(tok, *r.body);
-    g.accessToken = str(tok.access_token);
-    g.refreshToken = str(tok.refresh_token);
-    g.error = str(tok.error);
-    g.expiresInSecs = (int) gj::toInt64(tok.expires_in);
+    // RFC 6749 §5.1/§5.2: the grant, or {"error": "authorization_pending" | …}.
+    const json::Value tok = r.json();
+    g.accessToken = tok["access_token"].string();
+    g.refreshToken = tok["refresh_token"].string();
+    g.error = tok["error"].string();
+    g.expiresInSecs = (int) gj::flexInt(tok["expires_in"]);
     return g;
 }
 
@@ -377,7 +374,7 @@ void seedVisitor(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job)
     http.post("player", ClientId::WEB, bodies::player(w->req.videoId), job, [h, w, job](const Reply &r) {
         if (r.ok) {
             w->anyParsed = true;
-            PlayerPart p = parsePlayerResponse(*r.body);
+            PlayerPart p = parsePlayerResponse(r.json());
             // WEB formats are n-protected or SABR-only, and WEB answers UNPLAYABLE for
             // ordinary videos (live-verified): neither a source nor a verdict.
             p.formats.clear(); p.hlsUrl.clear(); p.sabrUrl.clear(); p.dashUrl.clear();
@@ -419,7 +416,7 @@ void playerStep(IHttp &http, std::shared_ptr<PlayerWalk> w, JobToken job)
             return;
         }
         w->anyParsed = true;
-        PlayerPart p = parsePlayerResponse(*r.body);
+        PlayerPart p = parsePlayerResponse(r.json());
         YTLOG(h->log(), "player") << "player" << h->session().clients[(int) c].name << p.status.status
             << "formats=" << p.formats.size() << "kinds=" << kindsOf(p) << "hls=" << !p.hlsUrl.empty()
             << "sabr=" << !p.sabrUrl.empty();
@@ -748,7 +745,8 @@ void rawPost(IHttp &http, std::string endpoint, ClientId client, std::string jso
     const size_t first = json.find_first_not_of(" \t\r\n"), last = json.find_last_not_of(" \t\r\n");
     const bool object = first != std::string::npos && json[first] == '{' && json[last] == '}' && last > first;
     if (object) json = json.substr(first, last - first + 1);
-    if (!object || !gj::scan::topLevelValue(json, "context").empty()) {
+    const yt::json::Doc parsed(json);              // `json` is the parameter here
+    if (!object || !parsed.root().isObject() || parsed["context"]) {
         out.error = Error{ErrorCode::InvalidArgument, "the body must be one JSON object without \"context\"", 0};
         done(out);
         return;
@@ -768,15 +766,15 @@ void oauthDeviceCode(IHttp &http, const JobToken &job, Callback<DeviceCodeGrant>
     http.postForm(Catalog::kDeviceCodeUrl, std::move(f), job, [done](const Reply &r) {
         Result<DeviceCodeGrant> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        oj::DeviceCode dc{};
-        gj::readJsonDoc(dc, *r.body);
-        out.value.deviceCode = str(dc.device_code);
-        out.value.userCode = str(dc.user_code);
-        out.value.verificationUrl = str(dc.verification_url);
-        if (out.value.verificationUrl.empty()) out.value.verificationUrl = str(dc.verification_uri);
-        out.value.intervalSecs = (int) gj::toInt64(dc.interval);
+        // RFC 8628 §3.2 (Google spells it verification_url).
+        const json::Value dc = r.json();
+        out.value.deviceCode = dc["device_code"].string();
+        out.value.userCode = dc["user_code"].string();
+        out.value.verificationUrl = dc["verification_url"].string();
+        if (out.value.verificationUrl.empty()) out.value.verificationUrl = dc["verification_uri"].string();
+        out.value.intervalSecs = (int) gj::flexInt(dc["interval"]);
         if (out.value.intervalSecs <= 0) out.value.intervalSecs = 5;
-        out.value.expiresInSecs = (int) gj::toInt64(dc.expires_in);
+        out.value.expiresInSecs = (int) gj::flexInt(dc["expires_in"]);
         if (out.value.deviceCode.empty() || out.value.userCode.empty())
             out.error = Error{ErrorCode::Api, "device code request failed", r.httpStatus};
         done(out);
@@ -807,13 +805,8 @@ void fetchSignInQr(IHttp &http, std::string userCode, const JobToken &job, Callb
     http.post("mdx/handoff", ClientId::TVHTML5, bodies::mdxHandoff(userCode), job, [h, job, done](const Reply &r) {
         Result<std::string> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        rj::Handoff hf{};
-        gj::readJsonDoc(hf, *r.body);
-        std::string url;
-        if (hf.rapidQrRenderer && hf.rapidQrRenderer->qrCodeRenderer && hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage
-            && hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails
-            && !hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails->empty())
-            url = str(hf.rapidQrRenderer->qrCodeRenderer->qrCodeImage->thumbnails->front().url);
+        const std::string url =
+            r.json().path("rapidQrRenderer", "qrCodeRenderer", "qrCodeImage", "thumbnails")[0]["url"].string();
         static const std::string_view kData = "data:image/png;base64,";
         if (url.compare(0, kData.size(), kData) == 0) {
             if (!gj::base64Decode(std::string_view(url).substr(kData.size()), &out.value) || !isPng(out.value)) {
@@ -846,10 +839,9 @@ void fetchDislikes(IHttp &http, std::string videoId, const JobToken &job, Callba
     http.get("https://returnyoutubedislikeapi.com/votes?videoId=" + pctEncode(videoId), job, [done](const Reply &r) {
         Result<Votes> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        oj::Ryd v{};
-        gj::readJsonDoc(v, *r.body);
-        if (v.likes) out.value.likes = gj::toInt64(v.likes);
-        if (v.dislikes) out.value.dislikes = gj::toInt64(v.dislikes);
+        const json::Value v = r.json();
+        if (v["likes"]) out.value.likes = gj::flexInt(v["likes"]);
+        if (v["dislikes"]) out.value.dislikes = gj::flexInt(v["dislikes"]);
         done(out);
     });
 }
@@ -862,7 +854,7 @@ void fetchSearchSuggestions(IHttp &http, std::string query, const JobToken &job,
     http.get(url, job, [done](const Reply &r) {
         Result<std::vector<std::string>> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        out.value = parseSuggestions(*r.body);
+        out.value = parseSuggestions(r.json());
         done(out);
     });
 }
diff --git a/src/json/json3.cpp b/src/json/json3.cpp
index 6b1eb84..2d8337f 100644
--- a/src/json/json3.cpp
+++ b/src/json/json3.cpp
@@ -1,22 +1,21 @@
-// json3 caption parsing: one partial read of events[] (see json3.h).
+// json3 caption parsing: the events[] of one document (see json3.h).
 #include "json/json3.h"
-#include "json/shapes/timedtext.h"
+#include "json/json.h"
+#include "json/ytjson.h"
 namespace yt {
 
-std::vector<Cue> parseJson3(const std::string &body)
+std::vector<Cue> parseJson3(std::string_view body)
 {
-    tj::Root root{};
-    gj::readJsonDoc(root, body);
+    const json::Doc doc(body);
     std::vector<Cue> out;
-    if (!root.events) return out;
-    for (const tj::Event &e : *root.events) {
-        if (!e.segs) continue;                        // window/pen definitions
+    for (json::Value e : doc["events"].items()) {
+        if (!e["segs"]) continue;                     // window/pen definitions
         std::string text;
-        for (const tj::Seg &s : *e.segs) if (s.utf8) text += *s.utf8;
+        for (json::Value s : e["segs"].items()) text += s["utf8"].str();
         if (text.find_first_not_of(" \n") == std::string::npos) continue;   // ASR line-append
         Cue c;
-        c.startMs = gj::toInt64(e.tStartMs);
-        c.endMs = c.startMs + gj::toInt64(e.dDurationMs);
+        c.startMs = gj::flexInt(e["tStartMs"]);
+        c.endMs = c.startMs + gj::flexInt(e["dDurationMs"]);
         c.text = std::move(text);
         out.push_back(std::move(c));
     }
diff --git a/src/json/json3.h b/src/json/json3.h
index 955ef7c..f6a665f 100644
--- a/src/json/json3.h
+++ b/src/json/json3.h
@@ -1,16 +1,17 @@
 // Timedtext captions in fmt=json3 → timed cues (Client::captions). The format is
-// YouTube's own caption JSON, not an InnerTube answer: the shapes are in
-// json/shapes/timedtext.h. Never fails: anything malformed yields no cues.
+// YouTube's own caption JSON, not an InnerTube answer: it arrives through fetch() (no
+// envelope), so this indexes the body itself. Never fails: anything malformed yields no cues.
 #ifndef YT_JSON_JSON3_H
 #define YT_JSON_JSON3_H
 #include <yt/player.h>
 #include <string>
+#include <string_view>
 #include <vector>
 namespace yt {
 
 // Window events and whitespace-only appends are skipped; an event's segments are
 // joined; an event without dDurationMs ends where it starts.
-std::vector<Cue> parseJson3(const std::string &body);
+std::vector<Cue> parseJson3(std::string_view body);
 
 }
 #endif
diff --git a/src/json/shapes/common.h b/src/json/shapes/common.h
index 35e1baf..c0bc756 100644
--- a/src/json/shapes/common.h
+++ b/src/json/shapes/common.h
@@ -40,14 +40,6 @@ struct Thumb {
 struct ThumbSet {
     std::optional<std::vector<Thumb>> thumbnails;
 };
-// YouTube channel avatars come back protocol-relative ("//yt3.ggpht.com/..."); Qt's image
-// loader rejects the missing scheme ("Unsupported protocol"). Force it to https.
-inline std::string withScheme(std::string u)
-{
-    if (u.size() >= 2 && u[0] == '/' && u[1] == '/')
-        u.insert(0, "https:");
-    return u;
-}
 // Largest (= last) thumbnail url, "" when absent — mirrors lastThumb()/lastOf().
 inline std::string lastThumbUrl(const ThumbSet &s)
 {
diff --git a/src/json/ytjson.h b/src/json/ytjson.h
index a0b2538..ee1f4e3 100644
--- a/src/json/ytjson.h
+++ b/src/json/ytjson.h
@@ -4,6 +4,7 @@
 // the machinery every parser shares. The JSON shapes themselves live in
 // json/shapes/ (one header per API family). Pure std types only.
 //
+#include "json/json.h"
 #include <glaze/json.hpp>
 
 #include <charconv>
@@ -93,6 +94,57 @@ inline bool base64Decode(std::string_view in, std::string *out)
     return true;
 }
 
+// ---- reading InnerTube shapes over yt::json ---------------------------------
+// {"simpleText": "…"} or {"runs": [{"text": "…"}, …]}: simpleText wins, else the runs
+// joined, else "" (also for anything that is not such an object).
+inline std::string textOf(json::Value t)
+{
+    if (const json::Value s = t["simpleText"]; s.isString()) return s.string();
+    std::string out;
+    for (json::Value r : t["runs"].items()) out += r["text"].str();
+    return out;
+}
+
+// YouTube channel avatars come back protocol-relative ("//yt3.ggpht.com/..."); Qt's image
+// loader rejects the missing scheme ("Unsupported protocol"). Force it to https.
+inline std::string withScheme(std::string u)
+{
+    if (u.size() >= 2 && u[0] == '/' && u[1] == '/')
+        u.insert(0, "https:");
+    return u;
+}
+
+// {"thumbnails": [{"url": …}, …]} is ordered small→large: the last url, "" when absent.
+inline std::string lastThumbUrl(json::Value set) { return withScheme(set["thumbnails"].last()["url"].string()); }
+// View-model images, {"sources": [{"url": …}, …]}, also small→large.
+inline std::string lastSourceUrl(json::Value set) { return withScheme(set["sources"].last()["url"].string()); }
+inline std::string firstSourceUrl(json::Value set) { return withScheme(set["sources"][0]["url"].string()); }
+
+// InnerTube counts arrive as JSON numbers (itag, width) or quoted strings
+// (videoDetails.viewCount): an integer, a float (truncated), or a strictly numeric string;
+// anything else — including a partial numeric prefix like "12a" — is 0.
+inline std::int64_t flexInt(json::Value v)
+{
+    std::int64_t n = 0;
+    if (v.toInt(n)) return n;
+    double d = 0;
+    if (v.toDouble(d)) return (d > -9.2e18 && d < 9.2e18) ? (std::int64_t) d : 0;
+    const std::string_view s = v.str();
+    const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), n, 10);
+    return (!s.empty() && r.ec == std::errc() && r.ptr == s.data() + s.size()) ? n : 0;
+}
+
+// The first member named key anywhere below v, in document order; absent when none.
+inline json::Value first(json::Value v, std::string_view key)
+{
+    json::Value got;
+    v.find({key}, [&](std::string_view, json::Value x) {
+        got = x;
+        return json::Visit::Stop;
+    });
+    return got;
+}
+
 } // namespace gj
 } // namespace yt
 #endif
diff --git a/src/parsers/playerparser.cpp b/src/parsers/playerparser.cpp
index 55a5935..9a3a535 100644
--- a/src/parsers/playerparser.cpp
+++ b/src/parsers/playerparser.cpp
@@ -1,18 +1,17 @@
 // /player answers → the client walk's view (PlayerPart) and storyboard specs. A fixed
-// root-level schema (json/shapes/player.h): one typed partial read (unknown keys
-// skipped); only the offline slate, whose path differs per client, is a bounded scan.
+// root-level schema read by path; only the offline slate, whose path differs per client,
+// is searched for.
 #include "playerparser.h"
-#include "json/jsonscan.h"
-#include "json/shapes/player.h"
+#include "json/ytjson.h"
 #include <charconv>
 #include <climits>
 #include <cstdint>
 #include <string>
 namespace yt {
 
-using namespace gj;
-static const std::string &str(const std::string &s) { return s; }
-static std::string str(const std::optional<std::string> &s) { return s ? str(*s) : std::string(); }
+using gj::flexInt;
+using gj::textOf;
+using json::Value;
 
 namespace {
 
@@ -62,59 +61,46 @@ int64_t unixFromIso8601(std::string_view s)
     return t;
 }
 
-// The offline slate lies under playabilityStatus on WEB-family answers and elsewhere on
-// others: find the renderer anywhere, bounded by kScanMaxDepth like every scan.
-struct SlateFinder {
-    std::string_view found;
-    void enter(int) {}
-    void leave(int) {}
-    scan::Action what(std::string_view k, int)
-    {
-        if (!found.empty()) return scan::Action::Skip;
-        return k == "liveStreamOfflineSlateRenderer" ? scan::Action::Capture : scan::Action::Descend;
-    }
-    void capture(std::string_view, std::string_view v, int) { found = v; }
-};
-
-Format formatOf(const pj::FormatJ &f, bool muxed)
+Format formatOf(Value f, bool muxed)
 {
     Format o;
-    o.itag = (int) toInt64(f.itag);
-    o.url = str(f.url);
-    o.sealed = o.url.empty() && (f.signatureCipher.has_value() || f.cipher.has_value());
-    const std::string mime = str(f.mimeType);         // video/mp4; codecs="avc1.640028"
+    o.itag = (int) flexInt(f["itag"]);
+    o.url = f["url"].string();
+    o.sealed = o.url.empty() && (f["signatureCipher"] || f["cipher"]);
+    const std::string mime = f["mimeType"].string();   // video/mp4; codecs="avc1.640028"
     o.mimeType = mime.substr(0, mime.find(';'));
     const size_t q = mime.find("codecs=\"");
     if (q != std::string::npos) {
         const size_t b = q + 8, e = mime.find('"', b);
         o.codecs = mime.substr(b, e == std::string::npos ? std::string::npos : e - b);
     }
-    o.qualityLabel = str(f.qualityLabel);
-    o.width = (int) toInt64(f.width);
-    o.height = (int) toInt64(f.height);
-    o.fps = (int) toInt64(f.fps);
-    o.bitrate = toInt64(f.bitrate);
-    o.averageBitrate = toInt64(f.averageBitrate);
-    o.contentLength = toInt64(f.contentLength);
-    o.durationMs = toInt64(f.approxDurationMs);
-    o.lastModified = toInt64(f.lastModified);
-    if (f.initRange) o.initRange = ByteRange{toInt64(f.initRange->start), toInt64(f.initRange->end)};
-    if (f.indexRange) o.indexRange = ByteRange{toInt64(f.indexRange->start), toInt64(f.indexRange->end)};
-    o.xtags = str(f.xtags);
+    o.qualityLabel = f["qualityLabel"].string();
+    o.width = (int) flexInt(f["width"]);
+    o.height = (int) flexInt(f["height"]);
+    o.fps = (int) flexInt(f["fps"]);
+    o.bitrate = flexInt(f["bitrate"]);
+    o.averageBitrate = flexInt(f["averageBitrate"]);
+    o.contentLength = flexInt(f["contentLength"]);
+    o.durationMs = flexInt(f["approxDurationMs"]);
+    o.lastModified = flexInt(f["lastModified"]);
+    if (const Value r = f["initRange"]) o.initRange = ByteRange{flexInt(r["start"]), flexInt(r["end"])};
+    if (const Value r = f["indexRange"]) o.indexRange = ByteRange{flexInt(r["start"]), flexInt(r["end"])};
+    o.xtags = f["xtags"].string();
     o.hasVideo = o.mimeType.rfind("video/", 0) == 0;
     o.hasAudio = muxed || o.mimeType.rfind("audio/", 0) == 0;
-    const bool drcFlag = f.isDrc.value_or(false);
-    if (f.audioTrack || drcFlag) {
+    const bool drcFlag = f["isDrc"].boolean();
+    const Value track = f["audioTrack"];
+    if (track || drcFlag) {
         // xtags is a small protobuf whose pairs ("acont" → "original" / "dubbed" /
         // "dubbed-auto", "drc" → "1") are in clear text; displayName is localized and
         // useless as a marker.
         std::string marks;
-        base64Decode(o.xtags, &marks);
+        gj::base64Decode(o.xtags, &marks);
         AudioTrack a;
-        if (f.audioTrack) {
-            a.id = str(f.audioTrack->id);
-            a.name = str(f.audioTrack->displayName);
-            a.isDefault = f.audioTrack->audioIsDefault.value_or(false);
+        if (track) {
+            a.id = track["id"].string();
+            a.name = track["displayName"].string();
+            a.isDefault = track["audioIsDefault"].boolean();
             a.language = a.id.substr(0, a.id.find('.'));
         }
         a.original = marks.find("original") != std::string::npos;
@@ -151,83 +137,64 @@ void replaceAll(std::string &s, std::string_view what, std::string_view with)
 
 } // namespace
 
-PlayerPart parsePlayerResponse(const std::string &body)
+PlayerPart parsePlayerResponse(Value root)
 {
-    pj::Root root{};
-    readJsonDoc(root, body);
     PlayerPart p;
-    if (root.playabilityStatus) {
-        const pj::StatusJ &st = *root.playabilityStatus;
-        p.status.status = str(st.status);
-        p.status.reason = str(st.reason);
-        p.status.kind = kindOf(p.status.status, st.desktopLegacyAgeGateReason.has_value());
-    }
+    const Value st = root["playabilityStatus"];
+    p.status.status = st["status"].string();
+    p.status.reason = st["reason"].string();
+    p.status.kind = kindOf(p.status.status, (bool) st["desktopLegacyAgeGateReason"]);
     if (p.status.status == "LIVE_STREAM_OFFLINE") {
-        SlateFinder sf;
-        scan::document(body, sf);
-        if (!sf.found.empty()) {
-            pj::SlateJ sl{};
-            readJson(sl, sf.found);
-            p.status.scheduledStartSecs = toInt64(sl.scheduledStartTime);
-            const std::string main = textOf(sl.mainText), sub = textOf(sl.subtitleText);
+        // The offline slate lies under playabilityStatus on WEB-family answers and
+        // elsewhere on others: find the renderer anywhere.
+        if (const Value sl = gj::first(root, "liveStreamOfflineSlateRenderer")) {
+            p.status.scheduledStartSecs = flexInt(sl["scheduledStartTime"]);
+            const std::string main = textOf(sl["mainText"]), sub = textOf(sl["subtitleText"]);
             p.status.slateText = (!main.empty() && !sub.empty()) ? main + "\n" + sub : main + sub;
         }
-        if (p.status.scheduledStartSecs <= 0 && root.microformat && root.microformat->playerMicroformatRenderer
-            && root.microformat->playerMicroformatRenderer->liveBroadcastDetails)
-            p.status.scheduledStartSecs =
-                unixFromIso8601(str(root.microformat->playerMicroformatRenderer->liveBroadcastDetails->startTimestamp));
+        if (p.status.scheduledStartSecs <= 0)
+            p.status.scheduledStartSecs = unixFromIso8601(
+                root.path("microformat", "playerMicroformatRenderer", "liveBroadcastDetails", "startTimestamp").str());
         if (p.status.scheduledStartSecs > 0) p.status.kind = Playability::Upcoming;
     }
     p.final = p.status.status == "ERROR" || p.status.kind == Playability::Upcoming
            || p.status.kind == Playability::LiveOffline;
-    if (root.streamingData) {
-        const pj::StreamingJ &sd = *root.streamingData;
-        if (sd.formats) for (const pj::FormatJ &f : *sd.formats) p.formats.push_back(formatOf(f, true));
-        if (sd.adaptiveFormats)
-            for (const pj::FormatJ &f : *sd.adaptiveFormats) p.formats.push_back(formatOf(f, false));
-        p.hlsUrl = str(sd.hlsManifestUrl);
-        p.dashUrl = str(sd.dashManifestUrl);
-        p.sabrUrl = str(sd.serverAbrStreamingUrl);
-        p.expiresInSecs = toInt64(sd.expiresInSeconds);
-    }
-    if (root.playerConfig && root.playerConfig->mediaCommonConfig
-        && root.playerConfig->mediaCommonConfig->mediaUstreamerRequestConfig)
-        p.sabrConfig =
-            str(root.playerConfig->mediaCommonConfig->mediaUstreamerRequestConfig->videoPlaybackUstreamerConfig);
-    if (root.videoDetails) {
-        const pj::DetailsJ &d = *root.videoDetails;
-        p.details.id = str(d.videoId);
-        p.details.title = str(d.title);
-        p.details.author = str(d.author);
-        p.details.channelId = str(d.channelId);
-        p.details.description = str(d.shortDescription);
-        p.details.lengthSecs = toInt64(d.lengthSeconds);
-        p.details.viewCount = toInt64(d.viewCount);
-        p.details.isLive = d.isLive.value_or(false);
-        p.details.isLiveContent = d.isLiveContent.value_or(false);
-        p.details.isUpcoming = d.isUpcoming.value_or(false);
-        p.details.thumbnailUrl = lastThumbUrl(d.thumbnail);
+    const Value sd = root["streamingData"];
+    for (Value f : sd["formats"].items()) p.formats.push_back(formatOf(f, true));
+    for (Value f : sd["adaptiveFormats"].items()) p.formats.push_back(formatOf(f, false));
+    p.hlsUrl = sd["hlsManifestUrl"].string();
+    p.dashUrl = sd["dashManifestUrl"].string();
+    p.sabrUrl = sd["serverAbrStreamingUrl"].string();
+    p.expiresInSecs = flexInt(sd["expiresInSeconds"]);
+    p.sabrConfig = root.path("playerConfig", "mediaCommonConfig", "mediaUstreamerRequestConfig",
+                             "videoPlaybackUstreamerConfig").string();
+    if (const Value d = root["videoDetails"]) {
+        p.details.id = d["videoId"].string();
+        p.details.title = d["title"].string();
+        p.details.author = d["author"].string();
+        p.details.channelId = d["channelId"].string();
+        p.details.description = d["shortDescription"].string();
+        p.details.lengthSecs = flexInt(d["lengthSeconds"]);
+        p.details.viewCount = flexInt(d["viewCount"]);
+        p.details.isLive = d["isLive"].boolean();
+        p.details.isLiveContent = d["isLiveContent"].boolean();
+        p.details.isUpcoming = d["isUpcoming"].boolean();
+        p.details.thumbnailUrl = gj::lastThumbUrl(d["thumbnail"]);
         if (p.details.thumbnailUrl.empty() && !p.details.id.empty())
             p.details.thumbnailUrl = "https://i.ytimg.com/vi/" + p.details.id + "/hqdefault.jpg";
     }
-    if (root.captions && root.captions->playerCaptionsTracklistRenderer
-        && root.captions->playerCaptionsTracklistRenderer->captionTracks)
-        for (const pj::TrackJ &t : *root.captions->playerCaptionsTracklistRenderer->captionTracks) {
-            CaptionTrack c;
-            c.url = str(t.baseUrl);
-            c.language = str(t.languageCode);
-            c.name = textOf(t.name);
-            c.autoGenerated = str(t.kind) == "asr";
-            c.translatable = t.isTranslatable.value_or(false);
-            if (!c.url.empty()) p.captions.push_back(std::move(c));
-        }
-    if (root.storyboards && root.storyboards->playerStoryboardSpecRenderer)
-        p.storyboardSpec = str(root.storyboards->playerStoryboardSpecRenderer->spec);
-    if (root.playbackTracking) {
-        const pj::TrackingJ &tr = *root.playbackTracking;
-        if (tr.videostatsWatchtimeUrl) p.watchtimeUrl = str(tr.videostatsWatchtimeUrl->baseUrl);
-        if (tr.videostatsPlaybackUrl) p.playbackUrl = str(tr.videostatsPlaybackUrl->baseUrl);
+    for (Value t : root.path("captions", "playerCaptionsTracklistRenderer", "captionTracks").items()) {
+        CaptionTrack c;
+        c.url = t["baseUrl"].string();
+        c.language = t["languageCode"].string();
+        c.name = textOf(t["name"]);
+        c.autoGenerated = t["kind"].str() == "asr";
+        c.translatable = t["isTranslatable"].boolean();
+        if (!c.url.empty()) p.captions.push_back(std::move(c));
     }
+    p.storyboardSpec = root.path("storyboards", "playerStoryboardSpecRenderer", "spec").string();
+    p.watchtimeUrl = root.path("playbackTracking", "videostatsWatchtimeUrl", "baseUrl").string();
+    p.playbackUrl = root.path("playbackTracking", "videostatsPlaybackUrl", "baseUrl").string();
     return p;
 }
 
diff --git a/src/parsers/playerparser.h b/src/parsers/playerparser.h
index 3e943bf..8bdde59 100644
--- a/src/parsers/playerparser.h
+++ b/src/parsers/playerparser.h
@@ -3,10 +3,11 @@
 // fail: malformed input yields absent fields / empty lists.
 #ifndef YT_PLAYERPARSER_H
 #define YT_PLAYERPARSER_H
+#include <yt/player.h>
+#include "json/json.h"
 #include <string>
 #include <string_view>
 #include <vector>
-#include <yt/player.h>
 namespace yt {
 
 // The full view of one /player answer the client walk needs: one partial Glaze read,
@@ -22,7 +23,7 @@ struct PlayerPart {
     std::string storyboardSpec;           // storyboards.playerStoryboardSpecRenderer.spec
     std::string watchtimeUrl, playbackUrl;
 };
-PlayerPart parsePlayerResponse(const std::string &body);
+PlayerPart parsePlayerResponse(json::Value root);
 // "<template>|w#h#count#cols#rows#intervalMs#name#sigh|…" → levels; a level whose
 // interval is 0 gets lengthSecs·1000 / count. Malformed levels are skipped.
 std::vector<StoryboardLevel> parseStoryboardSpec(std::string_view spec, int64_t lengthSecs);
diff --git a/src/parsers/suggestparser.cpp b/src/parsers/suggestparser.cpp
index e189238..bac5798 100644
--- a/src/parsers/suggestparser.cpp
+++ b/src/parsers/suggestparser.cpp
@@ -1,20 +1,14 @@
+// Search suggestions (suggestqueries, client=firefox): ["query", ["s1", "s2", …]].
 #include "suggestparser.h"
-#include "json/ytjson.h"
-#include <vector>
-#include <string>
 
 namespace yt {
-std::vector<std::string> parseSuggestions(std::string_view json) {
+
+std::vector<std::string> parseSuggestions(json::Value response)
+{
     std::vector<std::string> out;
-    // Read the outer array generically (2 elements normally; tolerate more), then
-    // read element [1] as an array of strings.
-    std::vector<glz::raw_json> top;
-    gj::readJson(top, json);
-    if (top.size() < 2) return out;
-    std::vector<std::string> sugg;
-    gj::readJson(sugg, top[1].str);
-    for (size_t i = 0; i < sugg.size(); ++i)
-        out.push_back(std::move(sugg[i]));
+    for (json::Value s : response[1].items())
+        if (s.isString()) out.push_back(s.string());
     return out;
 }
+
 }
diff --git a/src/parsers/suggestparser.h b/src/parsers/suggestparser.h
index ba037fb..80332d7 100644
--- a/src/parsers/suggestparser.h
+++ b/src/parsers/suggestparser.h
@@ -1,11 +1,11 @@
 #ifndef YT_SUGGESTPARSER_H
 #define YT_SUGGESTPARSER_H
+#include "json/json.h"
 #include <string>
 #include <vector>
-#include <string_view>
 namespace yt {
 // YouTube suggest endpoint (client=firefox) returns ["query",["s1","s2",…]].
 // Returns the suggestion strings; malformed/empty input → empty list.
-std::vector<std::string> parseSuggestions(std::string_view json);
+std::vector<std::string> parseSuggestions(json::Value response);
 }
 #endif
diff --git a/tests/tst_parsers.cpp b/tests/tst_parsers.cpp
index 683682d..a660fde 100644
--- a/tests/tst_parsers.cpp
+++ b/tests/tst_parsers.cpp
@@ -4,8 +4,9 @@
 #include <doctest/doctest.h>
 #include "innertube/playerjs.h"
 #include "parsers/continuation.h"
+#include "json/json.h"
 #include "json/json3.h"
-#include "json/shapes/common.h"
+#include "json/ytjson.h"
 #include "parsers/rendererparser.h"
 #include "parsers/playerparser.h"
 #include "parsers/suggestparser.h"
@@ -13,10 +14,20 @@
 #include "testutil.h"
 #include "parserpayloads.h"
 
+#include <deque>
+#include <memory>
 #include <string>
 
 using namespace yt;
 
+// Parsers read json::Values; the fixtures are JSON text. Each Doc keeps its own copy of
+// the text and lives until the suite ends, so a Value never outlives what it points into.
+static json::Value J(std::string_view text)
+{
+    static std::deque<json::Doc> docs;
+    return docs.emplace_back(std::make_shared<const std::string>(text)).root();
+}
+
 TEST_CASE("textmatch") {
     using namespace yt::gj;
     CHECK(digitsOf("1,234,567 views") == 1234567);
@@ -32,28 +43,22 @@ TEST_CASE("textmatch") {
 
 TEST_CASE("suggestionsParsed") {
     const std::string body = "[\"cat\",[\"cat videos\",\"cats\",\"cat memes\"]]";
-    std::vector<std::string> s = parseSuggestions(body);
+    std::vector<std::string> s = parseSuggestions(J(body));
     REQUIRE(s.size() == 3);
     CHECK(s[0] == "cat videos");
     CHECK(s[2] == "cat memes");
 }
 TEST_CASE("suggestionsGarbageIsEmpty") {
-    CHECK(parseSuggestions(std::string("not json")).empty());
-    CHECK(parseSuggestions(std::string("[\"only-query\"]")).empty());
+    CHECK(parseSuggestions(J(std::string("not json"))).empty());
+    CHECK(parseSuggestions(J(std::string("[\"only-query\"]"))).empty());
 }
 TEST_CASE("flexIntParses") {
-    gj::FlexInt s = std::string("1234");
-    CHECK(gj::toInt64(s) == 1234);
-    gj::FlexInt i = (std::int64_t)42;
-    CHECK(gj::toInt64(i) == 42);
-    gj::FlexInt bad = std::string("abc");
-    CHECK(gj::toInt64(bad) == 0);
-    gj::FlexInt partial = std::string("12a");
-    CHECK(gj::toInt64(partial) == 0);
-    std::optional<gj::FlexInt> missing;
-    CHECK(gj::toInt64(missing) == 0);
-    gj::FlexInt f = 3.9;
-    CHECK(gj::toInt64(f) == 3);
+    CHECK(gj::flexInt(J("\"1234\"")) == 1234);
+    CHECK(gj::flexInt(J("42")) == 42);
+    CHECK(gj::flexInt(J("\"abc\"")) == 0);
+    CHECK(gj::flexInt(J("\"12a\"")) == 0);
+    CHECK(gj::flexInt(json::Value()) == 0);
+    CHECK(gj::flexInt(J("3.9")) == 3);
 }
 TEST_CASE("parseTextBehavior") {
     CHECK(parseText(payloads::kSimpleText) == "hi");
@@ -306,7 +311,7 @@ TEST_CASE("base64Decode") {
 
 // ---- /player (parsePlayerResponse) ----
 TEST_CASE("playerResponseVisionos") {
-    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_ok.json"));
+    const PlayerPart p = parsePlayerResponse(J(loadFixtureRaw("player_visionos_ok.json")));
     CHECK(p.status.kind == Playability::Ok);
     CHECK_FALSE(p.final);
     REQUIRE(p.formats.size() == 4);
@@ -358,7 +363,7 @@ TEST_CASE("playerResponseVisionos") {
     CHECK(p.details.thumbnailUrl == "https://i.ytimg.com/vi/dQw4w9WgXcQ/hq720.jpg");
 }
 TEST_CASE("playerResponseProgressiveFromFormats") {
-    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_vr_ok.json"));
+    const PlayerPart p = parsePlayerResponse(J(loadFixtureRaw("player_vr_ok.json")));
     REQUIRE(p.formats.size() == 3);
     CHECK(p.formats[0].itag == 18);
     CHECK((p.formats[0].hasVideo && p.formats[0].hasAudio));   // formats[] are muxed
@@ -366,7 +371,7 @@ TEST_CASE("playerResponseProgressiveFromFormats") {
     CHECK(p.hlsUrl.empty());
 }
 TEST_CASE("playerResponseAudioTracks") {
-    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_dubbed.json"));
+    const PlayerPart p = parsePlayerResponse(J(loadFixtureRaw("player_visionos_dubbed.json")));
     REQUIRE(p.formats.size() == 3);
     REQUIRE(p.formats[1].audio.has_value());
     const AudioTrack &ru = *p.formats[1].audio;
@@ -384,58 +389,60 @@ TEST_CASE("playerResponseAudioTracks") {
     CHECK(p.formats[2].xtags == "ChEKBWFjb250EghvcmlnaW5hbAoNCgRsYW5nEgVlbi1VUw");
 }
 TEST_CASE("playerResponseVerdicts") {
-    PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_botcheck.json"));
+    PlayerPart p = parsePlayerResponse(J(loadFixtureRaw("player_botcheck.json")));
     CHECK(p.status.kind == Playability::BotCheck);
     CHECK(p.status.status == "LOGIN_REQUIRED");
     CHECK(p.status.reason.find("not a bot") != std::string::npos);
     CHECK_FALSE(p.final);
-    p = parsePlayerResponse(loadFixtureRaw("player_removed.json"));
+    p = parsePlayerResponse(J(loadFixtureRaw("player_removed.json")));
     CHECK(p.status.kind == Playability::Unplayable);
     CHECK(p.status.reason == "This video is unavailable");
     CHECK(p.final);
     // Review focus: the reason is localized by hl — the verdict must not read it.
-    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
-        "\"reason\":\"Войдите, чтобы подтвердить, что вы не бот\"}}"));
+    p = parsePlayerResponse(J(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
+        "\"reason\":\"Войдите, чтобы подтвердить, что вы не бот\"}}")));
     CHECK(p.status.kind == Playability::BotCheck);
-    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
-        "\"reason\":\"Sign in to confirm your age\",\"desktopLegacyAgeGateReason\":1}}"));
+    p = parsePlayerResponse(J(std::string("{\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\","
+        "\"reason\":\"Sign in to confirm your age\",\"desktopLegacyAgeGateReason\":1}}")));
     CHECK(p.status.kind == Playability::AgeRestricted);
-    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"AGE_CHECK_REQUIRED\"}}"));
+    p = parsePlayerResponse(J(std::string("{\"playabilityStatus\":{\"status\":\"AGE_CHECK_REQUIRED\"}}")));
     CHECK(p.status.kind == Playability::AgeRestricted);
-    p = parsePlayerResponse(std::string("not json"));
+    p = parsePlayerResponse(J(std::string("not json")));
     CHECK(p.status.kind == Playability::Unknown);
     CHECK(p.formats.empty());
 }
 TEST_CASE("playerResponseUpcoming") {
     // Shape per Troubadour (YTApi.m scheduledStartIn): no scheduled stream was at hand to capture.
-    PlayerPart p = parsePlayerResponse(std::string(
+    PlayerPart p = parsePlayerResponse(J(std::string(
         "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\",\"reason\":\"This live event will begin in 3 days.\","
         "\"liveStreamability\":{\"liveStreamabilityRenderer\":{\"offlineSlate\":{\"liveStreamOfflineSlateRenderer\":"
         "{\"scheduledStartTime\":\"1792000000\",\"mainText\":{\"runs\":[{\"text\":\"Live in 3 days\"}]},"
-        "\"subtitleText\":{\"simpleText\":\"October 13, 8:00 PM\"}}}}}}}"));
+        "\"subtitleText\":{\"simpleText\":\"October 13, 8:00 PM\"}}}}}}}")));
     CHECK(p.status.kind == Playability::Upcoming);
     CHECK(p.status.scheduledStartSecs == 1792000000LL);
     CHECK(p.status.slateText == "Live in 3 days\nOctober 13, 8:00 PM");
     CHECK(p.final);
     // No slate: the microformat's ISO time (with its zone) is the fallback.
-    p = parsePlayerResponse(std::string(
+    p = parsePlayerResponse(J(std::string(
         "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"},\"microformat\":{\"playerMicroformatRenderer\":"
-        "{\"liveBroadcastDetails\":{\"startTimestamp\":\"2026-09-09T20:00:00+03:00\"}}}}"));
+        "{\"liveBroadcastDetails\":{\"startTimestamp\":\"2026-09-09T20:00:00+03:00\"}}}}")));
     CHECK(p.status.kind == Playability::Upcoming);
     CHECK(p.status.scheduledStartSecs == 1788973200LL);
-    p = parsePlayerResponse(std::string("{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"}}"));
+    p = parsePlayerResponse(J(std::string("{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"}}")));
     CHECK(p.status.kind == Playability::LiveOffline);
     CHECK(p.final);
 }
 TEST_CASE("playerResponseSealedFormat") {
-    const PlayerPart p = parsePlayerResponse(std::string(
+    const PlayerPart p = parsePlayerResponse(J(std::string(
         "{\"playabilityStatus\":{\"status\":\"OK\"},\"streamingData\":{\"formats\":[{\"itag\":18,"
-        "\"mimeType\":\"video/mp4\",\"signatureCipher\":\"s=AB&sp=sig&url=https%3A%2F%2Fx\"}]}}"));
+        "\"mimeType\":\"video/mp4\",\"signatureCipher\":\"s=AB&sp=sig&url=https%3A%2F%2Fx\"}]}}")));
     REQUIRE(p.formats.size() == 1);
     CHECK(p.formats[0].sealed);
     CHECK(p.formats[0].url.empty());
 }
-// The slate search walks the whole answer: it must stay bounded like every scan.
+// A crafted answer nested past json::kMaxDepth is refused whole (the transport fails it as
+// InvalidResponse); a parser handed such a document sees nothing and still returns — the
+// slate search used to walk it recursively.
 TEST_CASE("playerResponseSlateScanDepthBounded") {
     const int n = 200000;
     std::string deep = "{\"playabilityStatus\":{\"status\":\"LIVE_STREAM_OFFLINE\"},\"x\":";
@@ -443,11 +450,11 @@ TEST_CASE("playerResponseSlateScanDepthBounded") {
     deep += "{}";
     for (int i = 0; i < n; ++i) deep += "}";
     deep += "}";
-    const PlayerPart p = parsePlayerResponse(deep);   // must return, not overflow the stack
-    CHECK(p.status.kind == Playability::LiveOffline);
+    const PlayerPart p = parsePlayerResponse(J(deep));   // must return, not overflow the stack
+    CHECK(p.status.kind == Playability::Unknown);
 }
 TEST_CASE("storyboardSpec") {
-    const PlayerPart p = parsePlayerResponse(loadFixtureRaw("player_visionos_ok.json"));
+    const PlayerPart p = parsePlayerResponse(J(loadFixtureRaw("player_visionos_ok.json")));
     const std::vector<StoryboardLevel> l = parseStoryboardSpec(p.storyboardSpec, 213);
     REQUIRE(l.size() == 2);
     CHECK(l[0].urlTemplate == "https://i.ytimg.com/sb/dQw4w9WgXcQ/storyboard3_L0/default.jpg?sqp=SQP&sigh=rs$SIGH0");
````

---

### Task 3: the renderer parsers on `json::Value` + `Value::find`

**Files:**
- Modify: `src/parsers/rendererinternal.h` (rewritten: converters over `json::Value`; collectors removed), `src/parsers/rendererparser.h`, `src/parsers/continuation.{h,cpp}`, `src/parsers/{videolist,playlist,channel,comment,account,watch}parser.cpp`, `src/core/chains.cpp` (list/watch/channel/playlist/account call sites, `claim`, the `kParse*` lambdas, the comments token), `tests/tst_parsers.cpp` (every parser call through `J`; `createCommentParamsUnderResponseEndpoints`), `tests/parserpayloads.h` (two inline fixtures had a stray closing brace)

**Interfaces:**
- Consumes: Task 2's `gj::` helpers and `J`.
- Produces: every function of `rendererparser.h` takes `json::Value` (`parseVideoList(json::Value, std::string *nextToken)`, `parseWatchPage(json::Value, Video *, std::string *relatedToken = nullptr)`, `parseChannel(json::Value)` — one overload, …); `continuation.h`: `bool isTokenKey(std::string_view)`, `std::string tokenOf(std::string_view key, json::Value wrapper)`, `std::string findContinuationToken(json::Value)`, `std::string findContinuationTokenUnder(json::Value, std::string_view key)`.

Collector semantics carried over exactly: a matched leaf renderer is converted and the rest of the object holding it is skipped (`Visit::SkipRest`); the first non-empty continuation token in document order wins and its wrapper is not looked into (`Visit::Skip`); the watch page's info renderers are taken without skipping their siblings (`Visit::Skip`). The one deliberate difference is `createCommentParams` (see the test's comment).

- [ ] **Step 1: Apply the tests and see them fail**

Save as `/tmp/t3.patch`; `git apply --include='tests/*' /tmp/t3.patch`; `cmake --build build 2>&1 | grep -m1 error`
Expected: `could not convert … to 'yt::json::Value'` at the first `parseVideoList(J(…))`.

- [ ] **Step 2: Implement**

`git apply --exclude='tests/*' /tmp/t3.patch`

- [ ] **Step 3: Run, and check the two inline fixtures**

`cmake --build build -j && (cd build && ctest -j8)` — expected all green.

The fixtures `kNextLikedWeb` and `kNextSubscribedTv` had one `}` too many; the old scanner stopped at the end of the root object and ignored the rest, `json::Doc` refuses them (as the transport would). Confirm the fixed ones are valid and the old ones were not:
`python3 -c "import json;json.loads(open('/dev/stdin').read())" <<< '{"a":1}}'` fails with `Extra data` — the same message the original fixtures gave.

The behaviour change pinned by `createCommentParamsUnderResponseEndpoints`: against the library before this task the same input yields `params=''` (checked while writing this plan); after it, `"CP"`.

- [ ] **Step 4: Commit**

```bash
git add src/parsers src/core/chains.cpp tests/tst_parsers.cpp tests/parserpayloads.h
git commit -m "lib: list, watch, channel, playlist, comment and account parsers on yt::json" -m "Each renderer is read by path in one converter (rendererinternal.h); renderers
found anywhere use Value::find (iterative) instead of the scanner visitors, with the
same leaf/early-return and first-token semantics. The parsers take the reply's index:
no second pass over the body, no Glaze struct per shape.
- createCommentParams is also found under onResponseReceivedEndpoints, where the
  signed-in comments header puts it; the old scan took that subtree whole for the page
  token and never looked inside.
- Two inline fixtures carried a stray closing brace that only the old scanner
  tolerated." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

````diff
diff --git a/src/core/chains.cpp b/src/core/chains.cpp
index 1915709..8003531 100644
--- a/src/core/chains.cpp
+++ b/src/core/chains.cpp
@@ -99,7 +99,7 @@ std::string sortParam(SearchSort sort)
 // the channel that was browsed (page 1 learns its name from the channel header).
 ChannelRef &channelOf(VideoItem &v) { return v.channel; }
 ChannelRef &channelOf(Playlist &p) { return p.owner; }
-template <class T> void claim(std::vector<T> &items, detail::Owner &owner, const std::string &body)
+template <class T> void claim(std::vector<T> &items, detail::Owner &owner, json::Value body)
 {
     if constexpr (std::is_same_v<T, VideoItem> || std::is_same_v<T, Playlist>) {
         if (owner.id.empty()) return;
@@ -120,9 +120,9 @@ HttpFn pageOf(Callback<Page<T>> done, std::string endpoint, ClientId client, Par
         Result<Page<T>> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
         std::string next;
-        out.value.items = parse(*r.body, &next);
+        out.value.items = parse(r.json(), &next);
         detail::Owner o = owner;
-        claim(out.value.items, o, *r.body);
+        claim(out.value.items, o, r.json());
         out.value.next = detail::CursorAccess::make<T>(endpoint, client, std::move(next), std::move(o));
         done(out);
     };
@@ -151,10 +151,10 @@ void moreOf(IHttp &http, const Cursor<T> &c, const JobToken &job, Callback<Page<
               pageOf<T>(std::move(done), endpoint, client, parse, detail::CursorAccess::owner(c)));
 }
 
-const auto kParseVideos = [](const std::string &b, std::string *n) { return parseVideoList(b, n); };
-const auto kParseUsers = [](const std::string &b, std::string *n) { return parseUserList(b, n); };
-const auto kParsePlaylists = [](const std::string &b, std::string *n) { return parsePlaylistList(b, n); };
-const auto kParseComments = [](const std::string &b, std::string *n) { return parseComments(b, n); };
+const auto kParseVideos = [](json::Value b, std::string *n) { return parseVideoList(b, n); };
+const auto kParseUsers = [](json::Value b, std::string *n) { return parseUserList(b, n); };
+const auto kParsePlaylists = [](json::Value b, std::string *n) { return parsePlaylistList(b, n); };
+const auto kParseComments = [](json::Value b, std::string *n) { return parseComments(b, n); };
 
 HttpFn doneOf(Callback<Done> done)
 {
@@ -189,7 +189,7 @@ void fetchCommentsFirst(IHttp &http, std::string videoId, const JobToken &job,
     http.post("next", ClientId::WEB, bodies::nextVideo(videoId), job, [h, job, done](const Reply &r) {
         Result<Page<CommentThread>> out;
         if (!r.ok) { out.error = errorOf(r); done(out, std::string()); return; }
-        const std::string token = findContinuationTokenUnder(*r.body, "engagementPanels");
+        const std::string token = findContinuationTokenUnder(r.json(), "engagementPanels");
         // No panel/token = comments disabled (e.g. made-for-kids): an empty, successful
         // page flagged `disabled` (distinct from a video with zero comments).
         if (token.empty()) { out.value.disabled = true; done(out, std::string()); return; }
@@ -199,7 +199,7 @@ void fetchCommentsFirst(IHttp &http, std::string videoId, const JobToken &job,
             std::string createParams;
             if (!p.ok) { page.error = errorOf(p); done(page, createParams); return; }
             std::string next;
-            page.value.items = parseComments(*p.body, &next, &createParams);
+            page.value.items = parseComments(p.json(), &next, &createParams);
             page.value.next = detail::CursorAccess::make<CommentThread>("next", ClientId::WEB, std::move(next));
             done(page, createParams);
         });
@@ -217,7 +217,7 @@ void fetchWatch(IHttp &http, std::string videoId, const JobToken &job, Callback<
         Result<Video> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
         std::string relatedNext;
-        parseWatchPage(*r.body, &out.value, &relatedNext);
+        parseWatchPage(r.json(), &out.value, &relatedNext);
         out.value.id = videoId;                      // /next does not echo the id
         out.value.related.next = detail::CursorAccess::make<VideoItem>("next", cid, std::move(relatedNext));
         done(out);
@@ -494,7 +494,7 @@ void fetchChannelById(IHttp &http, std::string channelId, const JobToken &job, C
     http.post("browse", ClientId::WEB, bodies::browse(channelId, ""), job, [done](const Reply &r) {
         Result<Channel> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        out.value = parseChannel(*r.body);
+        out.value = parseChannel(r.json());
         if (out.value.id.empty() && out.value.name.empty()) out.error = unavailable("channel unavailable");
         done(out);
     });
@@ -505,7 +505,7 @@ void fetchChannelSubscribed(IHttp &http, std::string channelId, const JobToken &
     http.post("browse", ClientId::TVHTML5, bodies::browse(channelId, ""), job, [done](const Reply &r) {
         Result<bool> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        out.value = parseChannelSubscribed(*r.body);
+        out.value = parseChannelSubscribed(r.json());
         done(out);
     });
 }
@@ -544,7 +544,7 @@ void fetchChannelId(IHttp &http, std::string link, const JobToken &job, Callback
     http.post("navigation/resolve_url", ClientId::WEB, bodies::resolveUrl(l->url()), job, [done](const Reply &r) {
         Result<std::string> id;
         if (!r.ok) { id.error = errorOf(r); done(id); return; }
-        id.value = parseResolvedBrowseId(*r.body);
+        id.value = parseResolvedBrowseId(r.json());
         if (id.value.rfind("UC", 0) != 0) { id.value.clear(); id.error = unavailable("could not resolve channel"); }
         done(id);
     });
@@ -655,10 +655,10 @@ void fetchPlaylist(IHttp &http, std::string playlistId, const JobToken &job, Cal
     http.post("browse", cid, bodies::browse(browseId, ""), job, [playlistId, cid, done](const Reply &r) {
         Result<Playlist> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        out.value = parsePlaylistPage(*r.body);
+        out.value = parsePlaylistPage(r.json());
         out.value.id = playlistId;
         std::string next;
-        out.value.entries.items = parseVideoList(*r.body, &next);
+        out.value.entries.items = parseVideoList(r.json(), &next);
         out.value.entries.next = detail::CursorAccess::make<VideoItem>("browse", cid, std::move(next));
         done(out);
     });
@@ -692,7 +692,7 @@ void fetchAccount(IHttp &http, const JobToken &job, Callback<AccountInfo> done)
     http.post("account/accounts_list", ClientId::TVHTML5, bodies::accountsList(), job, [done](const Reply &r) {
         Result<AccountInfo> out;
         if (!r.ok) { out.error = errorOf(r); done(out); return; }
-        out.value = parseAccountsList(*r.body);
+        out.value = parseAccountsList(r.json());
         if (out.value.name.empty() && out.value.channelId.empty()) out.error = unavailable("account unavailable");
         done(out);
     });
diff --git a/src/parsers/accountparser.cpp b/src/parsers/accountparser.cpp
index cf69814..5538176 100644
--- a/src/parsers/accountparser.cpp
+++ b/src/parsers/accountparser.cpp
@@ -1,65 +1,34 @@
+// accounts_list → the signed-in identity (rendererparser.h).
 #include "rendererinternal.h"
 
 namespace yt {
 
-using namespace gj;
-
 // accountItem objects sit ~7 renderer levels deep and the wrapper chain has shifted
-// before (getMultiPageMenuAction vs openPopupAction); scan recursively instead of
-// hard-coding the path. Captured items are re-scanned so accountItems nested
-// inside one another are still collected (old behavior). Each re-scan starts the
-// scanner's depth counter at 0, so the collector carries the ABSOLUTE depth itself
-// (`base`): without it a chain of nested "accountItem" keys defeated kScanMaxDepth and
-// recursed once per level (stack overflow on a crafted response).
-struct AccountItemCollector {
-    std::vector<std::string_view> *out;
-    int base = 0;                                            // depth of the scan's root
-    void enter(int) {}
-    void leave(int) {}
-    scan::Action what(std::string_view key, int depth)
-    {
-        if (base + depth > kScanMaxDepth) return scan::Action::Skip;
-        return key == "accountItem" ? scan::Action::Capture : scan::Action::Descend;
-    }
-    void capture(std::string_view, std::string_view value, int depth)
-    {
-        if (value.empty() || value.front() != '{') return;   // old is_object() guard
-        out->push_back(value);
-        const int saved = base;
-        base += depth + 1;                                   // the value sits one level below its key
-        scan::document(value, *this);                        // descend into it too
-        base = saved;
-    }
-};
-
-AccountInfo parseAccountsList(std::string_view response)
+// before (getMultiPageMenuAction vs openPopupAction): find them anywhere instead of
+// hard-coding the path. Items nested inside one another are all collected (Descend); the
+// walk is iterative and the document's depth is bounded, so a chain of nested
+// "accountItem" keys cannot exhaust the stack (it used to: one recursion per level).
+AccountInfo parseAccountsList(Value response)
 {
     AccountInfo out;
-    std::vector<std::string_view> views;
-    AccountItemCollector c;
-    c.out = &views;
-    scan::document(response, c);
-    if (views.empty()) return out;
-
-    std::vector<rj::AccountItem> items(views.size());
-    for (size_t i = 0; i < views.size(); ++i) readJson(items[i], views[i]);
-    const rj::AccountItem *pick = 0;
-    for (const rj::AccountItem &it : items)
-        if (it.isSelected && *it.isSelected) { pick = &it; break; }
-    if (!pick) pick = &items.front();
-
-    if (pick->accountName)   out.name = str(textOf(*pick->accountName));
-    if (pick->channelHandle) out.handle = str(textOf(*pick->channelHandle));
-    if (pick->accountPhoto)  out.avatarUrl = str(lastThumbUrl(*pick->accountPhoto));   // last = largest
-    if (pick->serviceEndpoint && pick->serviceEndpoint->selectActiveIdentityEndpoint
-        && pick->serviceEndpoint->selectActiveIdentityEndpoint->supportedTokens) {
-        for (const rj::SupTok &tok :
-             *pick->serviceEndpoint->selectActiveIdentityEndpoint->supportedTokens) {
-            if (!tok.offlineCacheKeyToken) continue;
-            const std::string key = str(tok.offlineCacheKeyToken->clientCacheKey);
-            if (!key.empty())
-                out.channelId = key.starts_with("UC") ? key : "UC" + key;
+    std::vector<Value> items;
+    response.find({"accountItem"}, [&](std::string_view, Value v) {
+        if (v.isObject()) items.push_back(v);
+        return Visit::Descend;
+    });
+    if (items.empty()) return out;
+    Value pick = items.front();
+    for (Value it : items)
+        if (it["isSelected"].boolean()) {
+            pick = it;
+            break;
         }
+    out.name = textOf(pick["accountName"]);
+    out.handle = textOf(pick["channelHandle"]);
+    out.avatarUrl = lastThumbUrl(pick["accountPhoto"]);   // last = largest
+    for (Value tok : pick.path("serviceEndpoint", "selectActiveIdentityEndpoint", "supportedTokens").items()) {
+        const std::string key = tok.path("offlineCacheKeyToken", "clientCacheKey").string();
+        if (!key.empty()) out.channelId = key.rfind("UC", 0) == 0 ? key : "UC" + key;
     }
     return out;
 }
diff --git a/src/parsers/channelparser.cpp b/src/parsers/channelparser.cpp
index 43cc46f..34e8e05 100644
--- a/src/parsers/channelparser.cpp
+++ b/src/parsers/channelparser.cpp
@@ -1,170 +1,91 @@
+// Channel pages and channel lists: the header (legacy c4TabbedHeaderRenderer or the 2024+
+// pageHeaderViewModel) with the channelMetadataRenderer fallback, channel renderers
+// anywhere, the subscribe state and a resolved browseId (rendererparser.h).
+#include "continuation.h"
 #include "rendererinternal.h"
 
 namespace yt {
 
-using namespace gj;
-
-// Shared by both parseChannel overloads (string_view + whole-document std::string):
-// static to this TU, which is the only place either overload lives.
-static Channel channelFromRoot(const rj::ChannelRoot &root)
+Channel parseChannel(Value response)
 {
     Channel u;
-    const rj::C4Header *c4 = 0;
-    const rj::PageHeader *ph = 0;
-    if (root.header) {
-        if (root.header->c4TabbedHeaderRenderer)     c4 = &*root.header->c4TabbedHeaderRenderer;
-        else if (root.header->pageHeaderRenderer)    ph = &*root.header->pageHeaderRenderer;
-    }
+    const Value c4 = response.path("header", "c4TabbedHeaderRenderer");
     // --- Legacy c4TabbedHeaderRenderer (flat shape) ---
     if (c4) {
-        if (c4->title) {
-            if (const std::string *s = std::get_if<std::string>(&*c4->title)) u.name = str(*s);
-            else if (const gj::Text *t = std::get_if<gj::Text>(&*c4->title))  u.name = str(textOf(*t));
-        }
-        u.id = str(c4->channelId);
-        if (c4->avatar)              u.avatarUrl = str(lastThumbUrl(*c4->avatar));
-        if (c4->subscriberCountText) u.subscriberText = str(textOf(*c4->subscriberCountText));
-        if (c4->banner)              u.bannerUrl = str(lastThumbUrl(*c4->banner));
+        const Value title = c4["title"];             // c4 uses a plain string title
+        u.name = title.isString() ? title.string() : textOf(title);
+        u.id = c4["channelId"].string();
+        u.avatarUrl = lastThumbUrl(c4["avatar"]);
+        u.subscriberText = textOf(c4["subscriberCountText"]);
+        u.bannerUrl = lastThumbUrl(c4["banner"]);
         // Authed-only subscribe state; absent (unauthed) leaves subscribed false (R1).
-        if (c4->subscribeButton && c4->subscribeButton->subscribeButtonRenderer
-            && c4->subscribeButton->subscribeButtonRenderer->subscribed)
-            u.subscribed = *c4->subscribeButton->subscribeButtonRenderer->subscribed;
+        u.subscribed = c4.path("subscribeButton", "subscribeButtonRenderer", "subscribed").boolean();
     }
     // --- Current pageHeaderRenderer.content.pageHeaderViewModel (nested view-models):
-    // 2024+ WEB channel headers moved name/avatar/subscriberCount here. ---
-    if (ph && ph->content && ph->content->pageHeaderViewModel) {
-        const rj::PageHeaderVM &vm = *ph->content->pageHeaderViewModel;
-        if (u.name.empty() && vm.title && vm.title->dynamicTextViewModel)
-            u.name = str(contentOf(vm.title->dynamicTextViewModel->text));
-        if (u.avatarUrl.empty() && vm.image && vm.image->decoratedAvatarViewModel
-            && vm.image->decoratedAvatarViewModel->avatar
-            && vm.image->decoratedAvatarViewModel->avatar->avatarViewModel)
-            u.avatarUrl = str(lastSourceUrl(
-                vm.image->decoratedAvatarViewModel->avatar->avatarViewModel->image));
-        if (u.bannerUrl.empty() && vm.banner && vm.banner->imageBannerViewModel)
-            u.bannerUrl = str(lastSourceUrl(vm.banner->imageBannerViewModel->image));
+    // 2024+ WEB channel headers moved name/avatar/subscriberCount here. Only when there
+    // is no c4 header. ---
+    const Value vm = c4 ? Value() : response.path("header", "pageHeaderRenderer", "content", "pageHeaderViewModel");
+    if (vm) {
+        if (u.name.empty()) u.name = vm.path("title", "dynamicTextViewModel", "text", "content").string();
+        if (u.avatarUrl.empty())
+            u.avatarUrl = lastSourceUrl(vm.path("image", "decoratedAvatarViewModel", "avatar", "avatarViewModel", "image"));
+        if (u.bannerUrl.empty()) u.bannerUrl = lastSourceUrl(vm.path("banner", "imageBannerViewModel", "image"));
         // metadata.contentMetadataViewModel.metadataRows[].metadataParts[].text.content —
         // the rows carry the subscriber line, the @handle and "N videos" with no fixed
         // index, so pick each by its shape (contains "subscriber" / starts with '@' /
         // contains "video").
-        if (u.subscriberText.empty() && vm.metadata
-            && vm.metadata->contentMetadataViewModel
-            && vm.metadata->contentMetadataViewModel->metadataRows) {
-            for (const rj::MetaRow &row : *vm.metadata->contentMetadataViewModel->metadataRows) {
-                if (!row.metadataParts) continue;
-                for (const rj::MetaPart &p : *row.metadataParts) {
-                    if (!p.text) continue;
-                    const std::string txt = p.text ? str(p.text->content) : std::string();
-                    if (u.subscriberText.empty()
-                        && gj::containsCI(txt, "subscriber"))
-                        u.subscriberText = txt;
-                    else if (u.handle.empty() && txt.starts_with('@'))
-                        u.handle = txt;
-                    else if (u.videoCountText.empty()
-                             && gj::containsCI(txt, "video"))
-                        u.videoCountText = txt;
+        if (u.subscriberText.empty())
+            for (Value row : vm.path("metadata", "contentMetadataViewModel", "metadataRows").items())
+                for (Value p : row["metadataParts"].items()) {
+                    if (!p["text"]) continue;
+                    const std::string txt = p.path("text", "content").string();
+                    if (u.subscriberText.empty() && gj::containsCI(txt, "subscriber")) u.subscriberText = txt;
+                    else if (u.handle.empty() && !txt.empty() && txt[0] == '@') u.handle = txt;
+                    else if (u.videoCountText.empty() && gj::containsCI(txt, "video")) u.videoCountText = txt;
                 }
-            }
-        }
     }
     // Fill gaps from channelMetadataRenderer (id/description/avatar).
-    if (root.metadata && root.metadata->channelMetadataRenderer) {
-        const rj::ChannelMeta &m = *root.metadata->channelMetadataRenderer;
-        if (u.id.empty())       u.id = str(m.externalId);
-        if (u.name.empty()) u.name = str(m.title);
-        u.description = str(m.description);
-        if (u.avatarUrl.empty() && m.avatar)
-            u.avatarUrl = str(lastThumbUrl(*m.avatar));
+    if (const Value m = response.path("metadata", "channelMetadataRenderer")) {
+        if (u.id.empty()) u.id = m["externalId"].string();
+        if (u.name.empty()) u.name = m["title"].string();
+        u.description = m["description"].string();
+        if (u.avatarUrl.empty()) u.avatarUrl = lastThumbUrl(m["avatar"]);
     }
     return u;
 }
 
-Channel parseChannel(std::string_view response)
-{
-    rj::ChannelRoot root{};
-    readJson(root, response);
-    return channelFromRoot(root);
-}
-
-// Whole-document overload: response is *r.body (NUL-terminated), so read via the
-// sentinel path (kInDoc). Behavior-identical to the string_view form.
-Channel parseChannel(const std::string &response)
-{
-    rj::ChannelRoot root{};
-    readJsonDoc(root, response);
-    return channelFromRoot(root);
-}
-
-struct UserCollector : CollectorBase {
-    std::vector<Channel> *out;
-    scan::Action what(std::string_view key, int)
-    {
-        if (consumed()) return scan::Action::Skip;
-        // tileRenderer: the authed TVHTML5 FEchannels grid ships channel TILES, not
-        // gridChannelRenderer (the WEB shape). Capture both.
-        if (key == "channelRenderer" || key == "gridChannelRenderer" || key == "tileRenderer") {
-            consume();
-            return scan::Action::Capture;
-        }
-        return scan::Action::Descend;
-    }
-    void capture(std::string_view key, std::string_view value, int)
-    {
-        if (key == "tileRenderer") {
-            rj::Tile t{};
-            readJson(t, value);
-            // Only channel tiles become channels; a video tile in the grid is ignored.
-            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_CHANNEL")
-                out->push_back(fromTileChannel(t));
-            return;
-        }
-        rj::UserR r{};
-        readJson(r, value);
-        out->push_back(fromUserRenderer(r));
-    }
-};
-
-std::vector<Channel> parseUserList(std::string_view response, std::string *nextToken)
+std::vector<Channel> parseUserList(Value response, std::string *nextToken)
 {
     std::vector<Channel> out;
-    if (nextToken) {
-        WithToken<UserCollector> w;
-        w.inner.out = &out;
-        scan::document(response, w);
-        *nextToken = std::string(w.token);
-    } else {
-        UserCollector c;
-        c.out = &out;
-        scan::document(response, c);
-    }
+    std::string token;
+    // tileRenderer: the authed TVHTML5 FEchannels grid ships channel TILES, not
+    // gridChannelRenderer (the WEB shape). Both are leaves.
+    response.find({"channelRenderer", "gridChannelRenderer", "tileRenderer", "continuationCommand",
+                   "nextContinuationData", "reloadContinuationData"},
+                  [&](std::string_view key, Value v) {
+        if (isTokenKey(key)) {
+            if (token.empty()) token = tokenOf(key, v);
+            return Visit::Skip;
+        }
+        if (key != "tileRenderer") out.push_back(fromUserRenderer(v));
+        // Only channel tiles become channels; a video tile in the grid is ignored.
+        else if (v["contentType"].str() == "TILE_CONTENT_TYPE_CHANNEL") out.push_back(fromTileChannel(v));
+        return Visit::SkipRest;
+    });
+    if (nextToken) *nextToken = token;
     return out;
 }
 
-Channel parseUserRenderer(std::string_view r)
-{
-    rj::UserR s{};
-    readJson(s, r);
-    return fromUserRenderer(s);
-}
+Channel parseUserRenderer(Value r) { return fromUserRenderer(r); }
 
-bool parseChannelSubscribed(std::string_view response)
+bool parseChannelSubscribed(Value response)
 {
-    const std::string_view sb = findExtent(response, "subscribeButtonRenderer");
-    if (sb.empty()) return false;
-    rj::SubscribeButtonR r{};
-    readJson(r, sb);
-    return r.subscribed && *r.subscribed;
+    return gj::first(response, "subscribeButtonRenderer")["subscribed"].boolean();
 }
 
-std::string parseResolvedBrowseId(std::string_view response)
+std::string parseResolvedBrowseId(Value response)
 {
-    const std::string_view ep = scan::topLevelValue(response, "endpoint");
-    if (ep.empty() || ep.front() != '{') return std::string();
-    const std::string_view be = scan::topLevelValue(ep, "browseEndpoint");
-    if (be.empty() || be.front() != '{') return std::string();
-    rj::BrowseEP b{};
-    readJson(b, be);
-    return str(b.browseId);
+    return response.path("endpoint", "browseEndpoint", "browseId").string();
 }
 
 }
diff --git a/src/parsers/commentparser.cpp b/src/parsers/commentparser.cpp
index d298d0d..dec8f0e 100644
--- a/src/parsers/commentparser.cpp
+++ b/src/parsers/commentparser.cpp
@@ -1,90 +1,34 @@
-#include "rendererinternal.h"
+// A comments page: the threads (commentEntityPayload mutations), the next page's token and
+// the create-comment box's createCommentParams (rendererparser.h).
 #include "continuation.h"
+#include "rendererinternal.h"
 
 namespace yt {
 
-using namespace gj;
-
-// CommentScanner — single-pass: collects commentEntityPayload (consume
-// semantics, same as the old CommentCollector) AND captures the first
-// top-level onResponseReceivedEndpoints extent (for the token mini-scan)
-// AND records the first continuation token found OUTSIDE that extent.
-//
-// Token precedence after the scan reproduces the old two-call logic exactly:
-//   if (token inside onRRE extent)   → use it          (same: findContinuationTokenUnder)
-//   else if (token outside extent)   → use it          (same: findContinuationToken on whole doc)
-struct CommentScanner : CollectorBase {
-    std::vector<CommentThread> *out;
-    std::string_view onRRE;          // first top-level onResponseReceivedEndpoints extent
-    std::string outsideToken;        // first token found outside the onRRE extent
-    std::string createParams;        // first createCommentParams string found (R4)
-    bool haveCreateParams = false;
-
-    scan::Action what(std::string_view key, int d)
-    {
-        if (consumed()) return scan::Action::Skip;
-        if (key == "commentEntityPayload") { consume(); return scan::Action::Capture; }
-        // R4 best-effort: the create-comment box's submit token. It rides in the
-        // comments-section create-comment command (commentSimpleboxRenderer /
-        // ...createCommentEndpoint) as a plain string field named createCommentParams.
-        // The exact host renderer is unverified on-device — scrape the first field of
-        // that name anywhere in the response. It sits OUTSIDE any commentEntityPayload
-        // subtree, so the leaf guard above never masks it.
-        if (!haveCreateParams && key == "createCommentParams") return scan::Action::Capture;
-        // Capture the top-level onRRE extent once (depth 0 = immediate child of root).
-        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty())
-            return scan::Action::Capture;
-        // Track first token outside the onRRE extent (it cannot be inside since
-        // we captured onRRE above — once captured, the scanner does not descend into it).
-        if (outsideToken.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
-        return scan::Action::Descend;
-    }
-    void capture(std::string_view key, std::string_view value, int d)
-    {
-        if (key == "commentEntityPayload") {
-            rj::CommentPayload p{};
-            readJson(p, value);
-            const CommentThread t = fromCommentPayload(p);
-            if (!t.comment.text.empty()) out->push_back(t);
-            return;
-        }
-        if (!haveCreateParams && key == "createCommentParams") {
-            // The captured extent is the raw JSON string literal (with quotes);
-            // Glaze reads+unescapes it into std::string.
-            std::string s;
-            readJson(s, value);
-            if (!s.empty()) { createParams = s; haveCreateParams = true; }
-            return;
-        }
-        if (d == 0 && key == "onResponseReceivedEndpoints" && onRRE.empty()) {
-            onRRE = value;
-            return;
-        }
-        if (gj::isTokenKey(key)) {
-            gj::captureToken(key, value, &outsideToken);
-            return;
-        }
-    }
-};
-
-std::vector<CommentThread> parseComments(std::string_view response, std::string *nextToken,
-                                 std::string *createCommentParams)
+std::vector<CommentThread> parseComments(Value response, std::string *nextToken, std::string *createCommentParams)
 {
     std::vector<CommentThread> out;
-    CommentScanner s;
-    s.out = &out;
-    scan::document(response, s);
+    std::string params;
+    // R4 best-effort: the create-comment box's submit token rides in the comments-section
+    // create-comment command (commentSimpleboxRenderer / …createCommentEndpoint) as a
+    // plain string field named createCommentParams. The exact host renderer is unverified
+    // on-device — take the first field of that name anywhere outside a comment payload.
+    response.find({"commentEntityPayload", "createCommentParams"}, [&](std::string_view key, Value v) {
+        if (key == "createCommentParams") {
+            if (params.empty()) params = v.string();
+            return Visit::Skip;
+        }
+        const CommentThread t = fromCommentPayload(v);
+        if (!t.comment.text.empty()) out.push_back(t);
+        return Visit::SkipRest;
+    });
+    // The page's own token sits under onResponseReceivedEndpoints; elsewhere a reply
+    // thread's token could come first.
     if (nextToken) {
-        // Prefer token inside onResponseReceivedEndpoints (mini-scan), else
-        // the first token found outside it during the main scan.
-        std::string t;
-        if (!s.onRRE.empty()) t = findContinuationToken(s.onRRE);
-        if (t.empty() && !s.outsideToken.empty())
-            t = std::string(s.outsideToken);
-        *nextToken = t;
+        *nextToken = findContinuationTokenUnder(response, "onResponseReceivedEndpoints");
+        if (nextToken->empty()) *nextToken = findContinuationToken(response);
     }
-    if (createCommentParams)   // R4: empty when the create-comment box wasn't present
-        *createCommentParams = std::string(s.createParams);
+    if (createCommentParams) *createCommentParams = params;   // R4: empty when the box wasn't present
     return out;
 }
 
diff --git a/src/parsers/continuation.cpp b/src/parsers/continuation.cpp
index 1f01529..68a3915 100644
--- a/src/parsers/continuation.cpp
+++ b/src/parsers/continuation.cpp
@@ -1,40 +1,35 @@
+// Continuation tokens (continuation.h). A wrapper whose token is missing or empty does
+// not stop the search: the next one in document order is tried.
 #include "continuation.h"
-#include "json/tokenscan.h"
-#include "json/jsonscan.h"
 namespace yt {
 
-using namespace gj;
+bool isTokenKey(std::string_view k)
+{
+    return k == "continuationCommand" || k == "nextContinuationData" || k == "reloadContinuationData";
+}
 
-// DFS-first-match over the whole tree, single pass. A wrapper whose token
-// string is missing/empty does not stop the search (the old fall-through).
-// Delegates to captureToken (from tokenscan.h) which encodes the same logic.
-struct TokenFinder {
-    std::string found;
-    void enter(int) {}
-    void leave(int) {}
-    scan::Action what(std::string_view key, int)
-    {
-        if (!found.empty()) return scan::Action::Skip;
-        if (isTokenKey(key)) return scan::Action::Capture;
-        return scan::Action::Descend;
-    }
-    void capture(std::string_view key, std::string_view value, int)
-    {
-        captureToken(key, value, &found);
-    }
-};
+std::string tokenOf(std::string_view key, json::Value w)
+{
+    if (key != "continuationCommand") return w["continuation"].string();
+    if (const std::string_view t = w["token"].str(); !t.empty()) return std::string(t);
+    // continuationItemViewModel (2026 playlist pages): one level deeper.
+    return w.path("innertubeCommand", "continuationCommand", "token").string();
+}
 
-std::string findContinuationToken(std::string_view json)
+std::string findContinuationToken(json::Value v)
 {
-    TokenFinder f;
-    scan::document(json, f);
-    return std::string(f.found);
+    std::string found;
+    v.find({"continuationCommand", "nextContinuationData", "reloadContinuationData"},
+           [&](std::string_view key, json::Value w) {
+        found = tokenOf(key, w);
+        return found.empty() ? json::Visit::Skip : json::Visit::Stop;
+    });
+    return found;
 }
 
-std::string findContinuationTokenUnder(std::string_view json, const char *rootKey)
+std::string findContinuationTokenUnder(json::Value v, std::string_view key)
 {
-    const std::string_view sub = scan::topLevelValue(json, rootKey);
-    if (sub.empty()) return std::string();
-    return findContinuationToken(sub);
+    return findContinuationToken(v[key]);
 }
+
 }
diff --git a/src/parsers/continuation.h b/src/parsers/continuation.h
index 600ba47..8a8e0e8 100644
--- a/src/parsers/continuation.h
+++ b/src/parsers/continuation.h
@@ -1,13 +1,21 @@
 #ifndef YT_CONTINUATION_H
 #define YT_CONTINUATION_H
+// Continuation tokens: the cursor of every InnerTube list, hidden under three wrapper
+// keys whose shapes differ (parsers/continuation.cpp).
+#include "json/json.h"
 #include <string>
 #include <string_view>
 namespace yt {
-// First continuation token found in a DFS of the (raw JSON) document — the
-// modern continuationCommand.token plus the legacy next/reload shapes.
-std::string findContinuationToken(std::string_view json);
-// Same, but scoped to the value of a top-level key ("" when the key is absent) —
-// e.g. the comments-section token lives under "engagementPanels" only.
-std::string findContinuationTokenUnder(std::string_view json, const char *rootKey);
+// The token a token-bearing wrapper carries, "" when it carries none: `key` is the member
+// name the wrapper sits under (one of isTokenKey's).
+std::string tokenOf(std::string_view key, json::Value wrapper);
+// The three member names whose value is a token-bearing wrapper.
+bool isTokenKey(std::string_view key);
+// The first non-empty continuation token below v, in document order — the modern
+// continuationCommand.token plus the legacy next/reload shapes.
+std::string findContinuationToken(json::Value v);
+// Same, scoped to the value of v's member `key` ("" when absent) — e.g. the
+// comments-section token lives under "engagementPanels" only.
+std::string findContinuationTokenUnder(json::Value v, std::string_view key);
 }
 #endif
diff --git a/src/parsers/playlistparser.cpp b/src/parsers/playlistparser.cpp
index afea5a3..e618dba 100644
--- a/src/parsers/playlistparser.cpp
+++ b/src/parsers/playlistparser.cpp
@@ -1,101 +1,62 @@
+// Playlist lists (every renderer that is a playlist) and the playlist page header
+// (rendererparser.h).
+#include "continuation.h"
 #include "rendererinternal.h"
-#include "json/shapes/playlist.h"
 
 namespace yt {
 
-using namespace gj;
-
-struct PlaylistCollector : CollectorBase {
-    std::vector<Playlist> *out;
-    scan::Action what(std::string_view key, int)
-    {
-        if (consumed()) return scan::Action::Skip;
-        if (key == "lockupViewModel" || key == "tileRenderer" || isPlaylistKind(key)) {
-            consume();
-            return scan::Action::Capture;
+std::vector<Playlist> parsePlaylistList(Value response, std::string *nextToken)
+{
+    std::vector<Playlist> out;
+    std::string token;
+    response.find({"lockupViewModel", "tileRenderer", "playlistRenderer", "gridPlaylistRenderer",
+                   "compactPlaylistRenderer", "continuationCommand", "nextContinuationData",
+                   "reloadContinuationData"},
+                  [&](std::string_view key, Value v) {
+        if (isTokenKey(key)) {
+            if (token.empty()) token = tokenOf(key, v);
+            return Visit::Skip;
         }
-        return scan::Action::Descend;
-    }
-    void capture(std::string_view key, std::string_view value, int)
-    {
         // lockupViewModel is a self-contained leaf; only the PLAYLIST content type
-        // becomes a Playlist (video/other lockups are skipped).
+        // becomes a Playlist (video/other lockups are skipped). tileRenderer is the
+        // TVHTML5 leaf (signed-in FElibrary); only PLAYLIST tiles become playlists — the
+        // sibling video/channel tiles in the library are skipped.
         if (key == "lockupViewModel") {
-            rj::Lockup lm{};
-            readJson(lm, value);
-            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_PLAYLIST")
-                out->push_back(fromPlaylistLockup(lm));
+            if (v["contentType"].str() == "LOCKUP_CONTENT_TYPE_PLAYLIST") out.push_back(fromPlaylistLockup(v));
+        } else if (key == "tileRenderer") {
+            if (v["contentType"].str() == "TILE_CONTENT_TYPE_PLAYLIST") out.push_back(fromTilePlaylist(v));
+        } else {
+            out.push_back(fromPlaylistRenderer(v));
         }
-        // tileRenderer is the TVHTML5 leaf (signed-in FElibrary); only PLAYLIST tiles
-        // become playlists — the sibling video/channel tiles in the library are skipped.
-        else if (key == "tileRenderer") {
-            rj::Tile t{};
-            readJson(t, value);
-            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_PLAYLIST")
-                out->push_back(fromTilePlaylist(t));
-        }
-        else {
-            rj::PlaylistR r{};
-            readJson(r, value);
-            out->push_back(fromPlaylistRenderer(r));
-        }
-    }
-};
-
-std::vector<Playlist> parsePlaylistList(std::string_view response, std::string *nextToken)
-{
-    std::vector<Playlist> out;
-    if (nextToken) {
-        WithToken<PlaylistCollector> w;
-        w.inner.out = &out;
-        scan::document(response, w);
-        *nextToken = std::string(w.token);
-    } else {
-        PlaylistCollector c;
-        c.out = &out;
-        scan::document(response, c);
-    }
+        return Visit::SkipRest;
+    });
+    if (nextToken) *nextToken = token;
     return out;
 }
 
-Playlist parsePlaylistRenderer(std::string_view r)
-{
-    rj::PlaylistR s{};
-    readJson(s, r);
-    return fromPlaylistRenderer(s);
-}
+Playlist parsePlaylistRenderer(Value r) { return fromPlaylistRenderer(r); }
 
-Playlist parsePlaylistPage(const std::string &response)
+Playlist parsePlaylistPage(Value response)
 {
     Playlist p;
-    rj::PlaylistRoot root{};
-    readJsonDoc(root, response);
-    if (root.sidebar && root.sidebar->playlistSidebarRenderer && root.sidebar->playlistSidebarRenderer->items) {
-        for (const rj::PlaylistSidebarItem &it : *root.sidebar->playlistSidebarRenderer->items) {
-            if (const auto &pri = it.playlistSidebarPrimaryInfoRenderer) {
-                p.title = str(textOf(pri->title));
-                if (pri->description) p.description = str(textOf(*pri->description));
-                // stats[0] is the video count; the label is localized, its number is not.
-                if (pri->stats && !pri->stats->empty()) p.videoCount = digitsOf(str(textOf(pri->stats->front())));
-                if (pri->thumbnailRenderer && pri->thumbnailRenderer->playlistVideoThumbnailRenderer
-                    && pri->thumbnailRenderer->playlistVideoThumbnailRenderer->thumbnail)
-                    p.thumbnailUrl = str(lastThumbUrl(*pri->thumbnailRenderer->playlistVideoThumbnailRenderer->thumbnail));
-            }
-            if (const auto &sec = it.playlistSidebarSecondaryInfoRenderer) {
-                if (!sec->videoOwner || !sec->videoOwner->videoOwnerRenderer) continue;
-                const rj::OwnerR &o = *sec->videoOwner->videoOwnerRenderer;
-                p.owner.name = str(textOf(o.title));
-                if (o.thumbnail) p.owner.avatarUrl = str(lastThumbUrl(*o.thumbnail));
-                if (o.navigationEndpoint && o.navigationEndpoint->browseEndpoint)
-                    p.owner.id = str(o.navigationEndpoint->browseEndpoint->browseId);
-            }
+    for (Value it : response.path("sidebar", "playlistSidebarRenderer", "items").items()) {
+        if (const Value pri = it["playlistSidebarPrimaryInfoRenderer"]) {
+            p.title = textOf(pri["title"]);
+            if (const Value d = pri["description"]) p.description = textOf(d);
+            // stats[0] is the video count; the label is localized, its number is not.
+            if (const Value s = pri["stats"][0]) p.videoCount = digitsOf(textOf(s));
+            if (const Value t = pri.path("thumbnailRenderer", "playlistVideoThumbnailRenderer", "thumbnail"))
+                p.thumbnailUrl = lastThumbUrl(t);
+        }
+        if (const Value o = it.path("playlistSidebarSecondaryInfoRenderer", "videoOwner", "videoOwnerRenderer")) {
+            p.owner.name = textOf(o["title"]);
+            if (const Value t = o["thumbnail"]) p.owner.avatarUrl = lastThumbUrl(t);
+            if (const Value id = o.path("navigationEndpoint", "browseEndpoint", "browseId")) p.owner.id = id.string();
         }
     }
-    if (root.metadata && root.metadata->playlistMetadataRenderer) {
-        const rj::PlaylistMeta &m = *root.metadata->playlistMetadataRenderer;
-        if (p.title.empty()) p.title = str(m.title);
-        if (p.description.empty()) p.description = str(m.description);
-    }
+    const Value m = response.path("metadata", "playlistMetadataRenderer");
+    if (p.title.empty()) p.title = m["title"].string();
+    if (p.description.empty()) p.description = m["description"].string();
     return p;
 }
 
diff --git a/src/parsers/rendererinternal.h b/src/parsers/rendererinternal.h
index e81e05e..c816d2c 100644
--- a/src/parsers/rendererinternal.h
+++ b/src/parsers/rendererinternal.h
@@ -1,146 +1,122 @@
 #ifndef YT_RENDERERINTERNAL_H
 #define YT_RENDERERINTERNAL_H
-// Internal (non-public) shared layer for the per-domain renderer parsers.
-// Holds the struct→CT converters over the renderer shapes (json/shapes/, namespace
-// rj), and the scanner-visitor scaffolding shared across the six
-// videolist/watch/playlist/channel/comment/account TUs that rendererparser.cpp
-// was split into. Everything is `inline` so the six TUs share one definition
-// (COMDAT-folded by the linker) with no duplicate-symbol / unused-static churn.
+// Internal (non-public) shared layer for the per-domain renderer parsers: the converters
+// from one renderer object (a json::Value) to a yt:: value type, shared by the
+// videolist/watch/playlist/channel/comment/account TUs. Paths are written out where they
+// are read — a renderer's shape lives in exactly one converter. Everything is `inline`
+// so the TUs share one definition (COMDAT-folded by the linker).
 //
 #include "rendererparser.h"
-#include "json/shapes/account.h"
-#include "json/shapes/channel.h"
-#include "json/shapes/comments.h"
-#include "json/shapes/renderers.h"
-#include "json/shapes/watch.h"
-#include "json/tokenscan.h"
-#include "json/jsonscan.h"
+#include "json/json.h"
+#include "json/ytjson.h"
 #include "textmatch.h"
-#include <map>
+#include <string>
+#include <string_view>
 #include <vector>
 
 namespace yt {
 
-using namespace gj;
-
+using gj::digitsOf;
+using gj::firstSourceUrl;
+using gj::lastSourceUrl;
+using gj::lastThumbUrl;
+using gj::textOf;
+using json::Value;
+using json::Visit;
 
-// ---------------------------------------------------------------------------
-// Glaze field → std::string: identity for plain strings, "" for an absent optional.
-// ---------------------------------------------------------------------------
-inline const std::string &str(const std::string &s) { return s; }
-inline std::string str(const std::optional<std::string> &s) { return s ? str(*s) : std::string(); }
+// The leaf renderer keys of the list collectors: a matched renderer is converted and NOT
+// looked into, and the rest of the object holding it is skipped (Visit::SkipRest) — the
+// old walkers' early `return`.
+inline bool isVideoKind(std::string_view key)
+{
+    return key == "videoRenderer" || key == "compactVideoRenderer"
+        || key == "gridVideoRenderer" || key == "playlistVideoRenderer";
+}
 
-// "digits only" → number (view counts arrive as "1,234 views"): gj::digitsOf.
-using gj::digitsOf;
+inline bool isPlaylistKind(std::string_view key)
+{
+    return key == "playlistRenderer" || key == "gridPlaylistRenderer"
+        || key == "compactPlaylistRenderer";
+}
 
-// ---------------------------------------------------------------------------
-// struct → CT conversions (the only Qt-aware part of the parse).
-// ---------------------------------------------------------------------------
-inline VideoItem fromVideoRenderer(const rj::VideoR &r)
+// videoRenderer / compactVideoRenderer / gridVideoRenderer / playlistVideoRenderer.
+inline VideoItem fromVideoRenderer(Value r)
 {
     VideoItem v;
-    v.id = str(r.videoId);
-    v.title = str(textOf(r.title));
+    v.id = r["videoId"].string();
+    v.title = textOf(r["title"]);
     // Presence-based fallback (not value-based): mirrors the old ternary on contains().
-    if (r.ownerText)            v.channel.name = str(textOf(*r.ownerText));
-    else if (r.longBylineText)  v.channel.name = str(textOf(*r.longBylineText));
-    v.durationText = str(textOf(r.lengthText));
-    v.thumbnailUrl = str(lastThumbUrl(r.thumbnail));   // native (WebP); decoded by the qwebp plugin
+    if (r["ownerText"]) v.channel.name = textOf(r["ownerText"]);
+    else if (r["longBylineText"]) v.channel.name = textOf(r["longBylineText"]);
+    v.durationText = textOf(r["lengthText"]);
+    v.thumbnailUrl = lastThumbUrl(r["thumbnail"]);   // native (WebP); decoded by the qwebp plugin
     if (v.thumbnailUrl.empty() && !v.id.empty())
         v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
-    // Channel avatar: videoRenderer.channelThumbnailSupportedRenderers
-    //   .channelThumbnailWithLinkRenderer.thumbnail.thumbnails[].url
-    if (r.channelThumbnailSupportedRenderers
-        && r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer)
-        v.channel.avatarUrl = str(lastThumbUrl(
-            r.channelThumbnailSupportedRenderers->channelThumbnailWithLinkRenderer->thumbnail));
-    if (r.viewCountText) {                      // the exact count ("1,234 views"), unlike lockups
-        v.viewText = str(textOf(*r.viewCountText));
+    v.channel.avatarUrl = lastThumbUrl(
+        r.path("channelThumbnailSupportedRenderers", "channelThumbnailWithLinkRenderer", "thumbnail"));
+    if (const Value vc = r["viewCountText"]) {       // the exact count ("1,234 views"), unlike lockups
+        v.viewText = textOf(vc);
         v.viewCount = digitsOf(v.viewText);
     }
-    if (r.publishedTimeText)                    // "2 days ago" — shown on the delegate
-        v.publishedText = str(textOf(*r.publishedTimeText));
+    v.publishedText = textOf(r["publishedTimeText"]);   // "2 days ago" — shown on the delegate
     v.durationSecs = gj::clockSecs(v.durationText);
     return v;
 }
 
 // The channel a lockup metadata part links to (its commandRuns' browseEndpoint), "" if none.
-inline std::string linkedChannel(const rj::MetaPart &p)
+inline std::string linkedChannel(Value part)
 {
-    if (!p.text || !p.text->commandRuns) return std::string();
-    for (const rj::CmdCtx &run : *p.text->commandRuns)
-        if (run.onTap && run.onTap->innertubeCommand && run.onTap->innertubeCommand->browseEndpoint) {
-            std::string id = str(run.onTap->innertubeCommand->browseEndpoint->browseId);
-            if (id.starts_with("UC")) return id;
-        }
+    for (Value run : part.path("text", "commandRuns").items()) {
+        const std::string_view id = run.path("onTap", "innertubeCommand", "browseEndpoint", "browseId").str();
+        if (id.substr(0, 2) == "UC") return std::string(id);
+    }
     return std::string();
 }
 
-inline VideoItem fromLockupVideo(const rj::Lockup &lm)
+// lockupViewModel with LOCKUP_CONTENT_TYPE_VIDEO.
+inline VideoItem fromLockupVideo(Value lm)
 {
     VideoItem v;
-    v.id = str(lm.contentId);
+    v.id = lm["contentId"].string();
     // Thumbnail + the duration badge overlay.
-    if (lm.contentImage && lm.contentImage->thumbnailViewModel) {
-        const rj::ThumbVM &tvm = *lm.contentImage->thumbnailViewModel;
-        v.thumbnailUrl = str(lastSourceUrl(tvm.image));
-        if (tvm.overlays)
-            for (const rj::OverlayW &ov : *tvm.overlays) {
-                if (!ov.thumbnailBottomOverlayViewModel) continue;
-                const rj::BottomOverlay &bo = *ov.thumbnailBottomOverlayViewModel;
-                if (!bo.badges) continue;
-                for (const rj::BadgeW &b : *bo.badges) {
-                    if (!b.thumbnailBadgeViewModel) continue;
-                    const std::string t = str(b.thumbnailBadgeViewModel->text);
-                    // Only the time-shaped badge ("4:56"/"1:02:03") is the duration.
-                    if (v.durationText.empty() && gj::isClockText(t))
-                        v.durationText = t;
-                }
-            }
-    }
+    const Value tvm = lm.path("contentImage", "thumbnailViewModel");
+    v.thumbnailUrl = lastSourceUrl(tvm["image"]);
+    for (Value ov : tvm["overlays"].items())
+        for (Value b : ov.path("thumbnailBottomOverlayViewModel", "badges").items()) {
+            const std::string_view t = b.path("thumbnailBadgeViewModel", "text").str();
+            // Only the time-shaped badge ("4:56"/"1:02:03") is the duration.
+            if (v.durationText.empty() && gj::isClockText(t)) v.durationText = std::string(t);
+        }
     if (v.thumbnailUrl.empty() && !v.id.empty())
         v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
-    if (lm.metadata && lm.metadata->lockupMetadataViewModel) {
-        const rj::LockupMetaVM &md = *lm.metadata->lockupMetadataViewModel;
-        if (md.title) v.title = str(contentOf(md.title));
-        // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
-        if (md.image && md.image->decoratedAvatarViewModel) {
-            const rj::DecoratedAvatar &dav = *md.image->decoratedAvatarViewModel;
-            if (dav.avatar && dav.avatar->avatarViewModel)
-                v.channel.avatarUrl = str(lastSourceUrl(dav.avatar->avatarViewModel->image));
-            if (dav.rendererContext && dav.rendererContext->commandContext) {
-                const rj::CmdCtx &cc = *dav.rendererContext->commandContext;
-                if (cc.onTap && cc.onTap->innertubeCommand && cc.onTap->innertubeCommand->browseEndpoint)
-                    v.channel.id = str(cc.onTap->innertubeCommand->browseEndpoint->browseId);
-            }
-        }
-        // metadataRows: [channel] [views, date] (feeds, related) or [channel, views, date]
-        // in one row (search) — but a channel's own tab omits the channel: [views, date].
-        // The first part is the channel when it links to one, or when more parts follow
-        // than views and date.
-        if (md.metadata && md.metadata->contentMetadataViewModel
-            && md.metadata->contentMetadataViewModel->metadataRows) {
-            const std::vector<rj::MetaRow> &rows = *md.metadata->contentMetadataViewModel->metadataRows;
-            size_t parts = 0;
-            for (const rj::MetaRow &row : rows) if (row.metadataParts) parts += row.metadataParts->size();
-            bool first = true;
-            for (const rj::MetaRow &row : rows) {
-                if (!row.metadataParts) continue;
-                for (const rj::MetaPart &p : *row.metadataParts) {
-                    const std::string txt = p.text ? str(p.text->content) : std::string();
-                    if (txt.empty()) continue;
-                    const std::string link = linkedChannel(p);
-                    if (first && (!link.empty() || rows.size() > 1 || parts >= 3)) {
-                        v.channel.name = txt;
-                        if (!link.empty()) v.channel.id = link;
-                    }
-                    else if (v.viewText.empty() && gj::containsCI(txt, "view")) v.viewText = txt;
-                    else if (v.publishedText.empty() && txt.contains("ago"))    v.publishedText = txt;
-                    first = false;
-                }
+    const Value md = lm.path("metadata", "lockupMetadataViewModel");
+    v.title = md.path("title", "content").string();
+    // Channel avatar + id from the decorated avatar's onTap browseEndpoint.
+    const Value dav = md.path("image", "decoratedAvatarViewModel");
+    v.channel.avatarUrl = lastSourceUrl(dav.path("avatar", "avatarViewModel", "image"));
+    v.channel.id = dav.path("rendererContext", "commandContext", "onTap", "innertubeCommand", "browseEndpoint",
+                            "browseId").string();
+    // metadataRows: [channel] [views, date] (feeds, related) or [channel, views, date]
+    // in one row (search) — but a channel's own tab omits the channel: [views, date].
+    // The first part is the channel when it links to one, or when more parts follow
+    // than views and date.
+    const Value rows = md.path("metadata", "contentMetadataViewModel", "metadataRows");
+    size_t parts = 0;
+    for (Value row : rows.items()) parts += row["metadataParts"].size();
+    bool first = true;
+    for (Value row : rows.items())
+        for (Value p : row["metadataParts"].items()) {
+            const std::string txt = p.path("text", "content").string();
+            if (txt.empty()) continue;
+            const std::string link = linkedChannel(p);
+            if (first && (!link.empty() || rows.size() > 1 || parts >= 3)) {
+                v.channel.name = txt;
+                if (!link.empty()) v.channel.id = link;
             }
+            else if (v.viewText.empty() && gj::containsCI(txt, "view")) v.viewText = txt;
+            else if (v.publishedText.empty() && txt.find("ago") != std::string::npos) v.publishedText = txt;
+            first = false;
         }
-    }
     v.durationSecs = gj::clockSecs(v.durationText);
     return v;
 }
@@ -149,48 +125,35 @@ inline VideoItem fromLockupVideo(const rj::Lockup &lm)
 // TILE_CONTENT_TYPE_VIDEO becomes a VideoItem. metadata.tileMetadataRenderer carries
 // title + lines (line 0 = channel, line 1 = views [+ date]); the duration rides in a
 // thumbnailOverlayTimeStatusRenderer.
-inline VideoItem fromTile(const rj::Tile &t)
+inline VideoItem fromTile(Value t)
 {
     VideoItem v;
-    v.id = str(t.contentId);
-    if (t.metadata && t.metadata->tileMetadataRenderer) {
-        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
-        if (md.title) v.title = str(textOf(*md.title));
-        if (md.lines) {
-            for (size_t li = 0; li < md.lines->size(); ++li) {
-                const rj::LineW &line = (*md.lines)[li];
-                if (!line.lineRenderer || !line.lineRenderer->items) continue;
-                for (const rj::LineItemW &item : *line.lineRenderer->items) {
-                    if (!item.lineItemRenderer || !item.lineItemRenderer->text) continue;
-                    const std::string text = str(textOf(*item.lineItemRenderer->text));
-                    if (text.empty()) continue;
-                    if (li == 0 && v.channel.name.empty()) v.channel.name = text;
-                    else if (v.viewText.empty() && text.contains("view")) v.viewText = text;
-                    // Date lives on a later line; pick the DATED item ("3 days ago" /
-                    // "Streamed 2 days ago" / "Sep 5, 2024"), never the "•" separator or
-                    // other bullets that sit between the view count and the date.
-                    else if (v.publishedText.empty() && li > 0
-                             && (text.contains("ago")
-                                 || gj::hasYear(text)))
-                        v.publishedText = text;
-                }
-            }
-        }
-    }
-    if (t.header && t.header->tileHeaderRenderer) {
-        const rj::TileHeader &h = *t.header->tileHeaderRenderer;
-        if (h.thumbnail && h.thumbnail->thumbnails && !h.thumbnail->thumbnails->empty()) {
-            v.thumbnailUrl = str(lastThumbUrl(*h.thumbnail));   // last = largest
+    v.id = t["contentId"].string();
+    const Value md = t.path("metadata", "tileMetadataRenderer");
+    v.title = textOf(md["title"]);
+    size_t li = 0;
+    for (Value line : md["lines"].items()) {
+        for (Value item : line.path("lineRenderer", "items").items()) {
+            const std::string text = textOf(item.path("lineItemRenderer", "text"));
+            if (text.empty()) continue;
+            if (li == 0 && v.channel.name.empty()) v.channel.name = text;
+            else if (v.viewText.empty() && text.find("view") != std::string::npos) v.viewText = text;
+            // Date lives on a later line; pick the DATED item ("3 days ago" /
+            // "Streamed 2 days ago" / "Sep 5, 2024"), never the "•" separator or
+            // other bullets that sit between the view count and the date.
+            else if (v.publishedText.empty() && li > 0
+                     && (text.find("ago") != std::string::npos || gj::hasYear(text)))
+                v.publishedText = text;
         }
-        if (h.thumbnailOverlays)
-            for (const rj::TileOverlay &ov : *h.thumbnailOverlays)
-                if (ov.thumbnailOverlayTimeStatusRenderer
-                    && ov.thumbnailOverlayTimeStatusRenderer->text)
-                    v.durationText = str(textOf(*ov.thumbnailOverlayTimeStatusRenderer->text));
+        ++li;
     }
-    if (v.thumbnailUrl.empty() && !v.id.empty()) {
+    const Value h = t.path("header", "tileHeaderRenderer");
+    v.thumbnailUrl = lastThumbUrl(h["thumbnail"]);   // last = largest
+    for (Value ov : h["thumbnailOverlays"].items())
+        if (const Value text = ov.path("thumbnailOverlayTimeStatusRenderer", "text"))
+            v.durationText = textOf(text);
+    if (v.thumbnailUrl.empty() && !v.id.empty())
         v.thumbnailUrl = "https://i.ytimg.com/vi/" + v.id + "/hqdefault.jpg";
-    }
     // Channel id from the long-press "Go to channel" menu (the video tile carries no
     // channel field of its own). Populating userId makes tapping the author/avatar open
     // the channel page. NOTE: the TV home tile has NO avatar, and YouTube avatar URLs
@@ -198,17 +161,12 @@ inline VideoItem fromTile(const rj::Tile &t)
     // rate-limited and fetching each channel is too heavy for a feed, so the home grid
     // keeps the placeholder squircle. The author avatar still shows on the watch page (parsed
     // from the /next owner).
-    if (t.onLongPressCommand && t.onLongPressCommand->showMenuCommand
-        && t.onLongPressCommand->showMenuCommand->menu
-        && t.onLongPressCommand->showMenuCommand->menu->menuRenderer
-        && t.onLongPressCommand->showMenuCommand->menu->menuRenderer->items) {
-        for (const rj::MenuItemW &mi : *t.onLongPressCommand->showMenuCommand->menu->menuRenderer->items) {
-            if (mi.menuNavigationItemRenderer && mi.menuNavigationItemRenderer->navigationEndpoint
-                && mi.menuNavigationItemRenderer->navigationEndpoint->browseEndpoint
-                && mi.menuNavigationItemRenderer->navigationEndpoint->browseEndpoint->browseId) {
-                const std::string bid = str(*mi.menuNavigationItemRenderer->navigationEndpoint->browseEndpoint->browseId);
-                if (bid.starts_with("UC")) { v.channel.id = bid; break; }
-            }
+    for (Value mi : t.path("onLongPressCommand", "showMenuCommand", "menu", "menuRenderer", "items").items()) {
+        const std::string_view bid =
+            mi.path("menuNavigationItemRenderer", "navigationEndpoint", "browseEndpoint", "browseId").str();
+        if (bid.substr(0, 2) == "UC") {
+            v.channel.id = std::string(bid);
+            break;
         }
     }
     v.durationSecs = gj::clockSecs(v.durationText);
@@ -219,31 +177,21 @@ inline VideoItem fromTile(const rj::Tile &t)
 // FEchannels (manage-subscriptions) grid ships these instead of gridChannelRenderer:
 // contentId = channelId, metadata title = name, line 0 = @handle, a "N subscribers"
 // line, and the header thumbnail = the channel avatar.
-inline Channel fromTileChannel(const rj::Tile &t)
+inline Channel fromTileChannel(Value t)
 {
     Channel u;
-    u.id = str(t.contentId);
-    if (t.metadata && t.metadata->tileMetadataRenderer) {
-        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
-        if (md.title) u.name = str(textOf(*md.title));
-        if (md.lines)
-            for (const rj::LineW &line : *md.lines) {
-                if (!line.lineRenderer || !line.lineRenderer->items) continue;
-                for (const rj::LineItemW &item : *line.lineRenderer->items) {
-                    if (!item.lineItemRenderer || !item.lineItemRenderer->text) continue;
-                    const std::string text = str(textOf(*item.lineItemRenderer->text));
-                    if (text.empty()) continue;
-                    if (u.handle.empty() && text.starts_with('@')) u.handle = text;
-                    else if (u.subscriberText.empty()
-                             && text.contains("subscriber")) u.subscriberText = text;
-                }
-            }
-    }
-    if (t.header && t.header->tileHeaderRenderer && t.header->tileHeaderRenderer->thumbnail) {
-        std::string url = str(lastThumbUrl(*t.header->tileHeaderRenderer->thumbnail));
-        if (url.starts_with("//")) url.insert(0, "https:");   // protocol-relative
-        u.avatarUrl = url;
-    }
+    u.id = t["contentId"].string();
+    const Value md = t.path("metadata", "tileMetadataRenderer");
+    u.name = textOf(md["title"]);
+    for (Value line : md["lines"].items())
+        for (Value item : line.path("lineRenderer", "items").items()) {
+            const std::string text = textOf(item.path("lineItemRenderer", "text"));
+            if (text.empty()) continue;
+            if (u.handle.empty() && text[0] == '@') u.handle = text;
+            else if (u.subscriberText.empty() && text.find("subscriber") != std::string::npos)
+                u.subscriberText = text;
+        }
+    u.avatarUrl = lastThumbUrl(t.path("header", "tileHeaderRenderer", "thumbnail"));
     return u;
 }
 
@@ -253,53 +201,39 @@ inline Channel fromTileChannel(const rj::Tile &t)
 // lockupViewModel: contentId = playlistId (LL / WL / PL…), metadata title = name,
 // the header thumbnail = cover, and "N videos" rides a thumbnailOverlayTimeStatus-
 // Renderer (the same overlay a video tile uses for its duration).
-inline Playlist fromTilePlaylist(const rj::Tile &t)
+inline Playlist fromTilePlaylist(Value t)
 {
     Playlist p;
-    p.id = str(t.contentId);
-    if (t.metadata && t.metadata->tileMetadataRenderer) {
-        const rj::TileMeta &md = *t.metadata->tileMetadataRenderer;
-        if (md.title) p.title = str(textOf(*md.title));
-    }
-    if (t.header && t.header->tileHeaderRenderer) {
-        const rj::TileHeader &h = *t.header->tileHeaderRenderer;
-        if (h.thumbnail && h.thumbnail->thumbnails && !h.thumbnail->thumbnails->empty()) {
-            std::string url = str(lastThumbUrl(*h.thumbnail));   // last = largest
-            if (url.starts_with("//")) url.insert(0, "https:");   // protocol-relative
-            p.thumbnailUrl = url;
+    p.id = t["contentId"].string();
+    p.title = textOf(t.path("metadata", "tileMetadataRenderer", "title"));
+    const Value h = t.path("header", "tileHeaderRenderer");
+    p.thumbnailUrl = lastThumbUrl(h["thumbnail"]);   // last = largest
+    // "N videos" is the only text overlay on a playlist tile; take its digits
+    // directly (no "video" word-match — the label is localized).
+    for (Value ov : h["thumbnailOverlays"].items())
+        if (const Value text = ov.path("thumbnailOverlayTimeStatusRenderer", "text")) {
+            const int64_t n = digitsOf(textOf(text));
+            if (n > 0) p.videoCount = n;
         }
-        // "N videos" is the only text overlay on a playlist tile; take its digits
-        // directly (no "video" word-match — the label is localized).
-        if (h.thumbnailOverlays)
-            for (const rj::TileOverlay &ov : *h.thumbnailOverlays)
-                if (ov.thumbnailOverlayTimeStatusRenderer
-                    && ov.thumbnailOverlayTimeStatusRenderer->text) {
-                    const int64_t n = digitsOf(str(textOf(*ov.thumbnailOverlayTimeStatusRenderer->text)));
-                    if (n > 0) p.videoCount = n;
-                }
-    }
     return p;
 }
 
-inline Playlist fromPlaylistRenderer(const rj::PlaylistR &r)
+// playlistRenderer / gridPlaylistRenderer / compactPlaylistRenderer.
+inline Playlist fromPlaylistRenderer(Value r)
 {
     Playlist p;
-    p.id = str(r.playlistId);
-    if (r.title) {
-        if (const gj::Text *t = std::get_if<gj::Text>(&*r.title)) p.title = str(textOf(*t));
-        else if (const std::string *s = std::get_if<std::string>(&*r.title)) p.title = str(*s);
-    }
-    if (r.thumbnail)
-        p.thumbnailUrl = str(lastThumbUrl(*r.thumbnail));
-    else if (r.thumbnails && !r.thumbnails->empty())
-        p.thumbnailUrl = str(lastThumbUrl(r.thumbnails->front()));
-    if (r.shortBylineText)     p.owner.name = str(textOf(*r.shortBylineText));
-    else if (r.longBylineText) p.owner.name = str(textOf(*r.longBylineText));
-    std::string vc;
-    if (r.videoCount)
-        if (const std::string *s = std::get_if<std::string>(&*r.videoCount)) vc = str(*s);
-    if (vc.empty() && r.videoCountText)      vc = str(textOf(*r.videoCountText));
-    if (vc.empty() && r.videoCountShortText) vc = str(textOf(*r.videoCountShortText));
+    p.id = r["playlistId"].string();
+    // Some shapes carry a plain string title, others a text object.
+    const Value title = r["title"];
+    p.title = title.isString() ? title.string() : textOf(title);
+    if (const Value t = r["thumbnail"]) p.thumbnailUrl = lastThumbUrl(t);
+    else p.thumbnailUrl = lastThumbUrl(r["thumbnails"][0]);
+    if (r["shortBylineText"]) p.owner.name = textOf(r["shortBylineText"]);
+    else if (r["longBylineText"]) p.owner.name = textOf(r["longBylineText"]);
+    // Only a string videoCount counts; a numeric one falls through to the text forms.
+    std::string vc = r["videoCount"].string();
+    if (vc.empty()) vc = textOf(r["videoCountText"]);
+    if (vc.empty()) vc = textOf(r["videoCountShortText"]);
     p.videoCount = digitsOf(vc);
     return p;
 }
@@ -307,146 +241,51 @@ inline Playlist fromPlaylistRenderer(const rj::PlaylistR &r)
 // lockupViewModel with LOCKUP_CONTENT_TYPE_PLAYLIST — the 2024+ channel Playlists
 // tab ships these instead of gridPlaylistRenderer. The thumbnail hides under
 // collectionThumbnailViewModel.primaryThumbnail and "N videos" is a thumbnail badge.
-inline Playlist fromPlaylistLockup(const rj::Lockup &lm)
+inline Playlist fromPlaylistLockup(Value lm)
 {
     Playlist p;
-    p.id = str(lm.contentId);
-    if (lm.contentImage && lm.contentImage->collectionThumbnailViewModel
-        && lm.contentImage->collectionThumbnailViewModel->primaryThumbnail
-        && lm.contentImage->collectionThumbnailViewModel->primaryThumbnail->thumbnailViewModel) {
-        const rj::ThumbVM &tvm =
-            *lm.contentImage->collectionThumbnailViewModel->primaryThumbnail->thumbnailViewModel;
-        p.thumbnailUrl = str(lastSourceUrl(tvm.image));
-        if (tvm.overlays)
-            for (const rj::OverlayW &ov : *tvm.overlays) {
-                if (!ov.thumbnailOverlayBadgeViewModel) continue;
-                const rj::OverlayBadgeVM &b = *ov.thumbnailOverlayBadgeViewModel;
-                if (!b.thumbnailBadges) continue;
-                for (const rj::BadgeW &bb : *b.thumbnailBadges) {
-                    if (!bb.thumbnailBadgeViewModel) continue;
-                    const std::string t = str(bb.thumbnailBadgeViewModel->text);
-                    if (p.videoCount == 0 && gj::containsCI(t, "video"))
-                        p.videoCount = digitsOf(t);
-                }
-            }
-    }
-    if (lm.metadata && lm.metadata->lockupMetadataViewModel
-        && lm.metadata->lockupMetadataViewModel->title)
-        p.title = str(contentOf(lm.metadata->lockupMetadataViewModel->title));
+    p.id = lm["contentId"].string();
+    const Value tvm = lm.path("contentImage", "collectionThumbnailViewModel", "primaryThumbnail", "thumbnailViewModel");
+    p.thumbnailUrl = lastSourceUrl(tvm["image"]);
+    for (Value ov : tvm["overlays"].items())
+        for (Value b : ov.path("thumbnailOverlayBadgeViewModel", "thumbnailBadges").items()) {
+            const std::string_view t = b.path("thumbnailBadgeViewModel", "text").str();
+            if (p.videoCount == 0 && gj::containsCI(t, "video")) p.videoCount = digitsOf(t);
+        }
+    p.title = lm.path("metadata", "lockupMetadataViewModel", "title", "content").string();
     return p;
 }
 
-inline Channel fromUserRenderer(const rj::UserR &r)
+// channelRenderer / gridChannelRenderer.
+inline Channel fromUserRenderer(Value r)
 {
     Channel u;
-    u.id = str(r.channelId);
-    u.name = str(textOf(r.title));
-    if (r.thumbnail)           u.avatarUrl = str(lastThumbUrl(*r.thumbnail));
-    if (r.subscriberCountText) u.subscriberText = str(textOf(*r.subscriberCountText));
-    if (r.descriptionSnippet)  u.description = str(textOf(*r.descriptionSnippet));
+    u.id = r["channelId"].string();
+    u.name = textOf(r["title"]);
+    u.avatarUrl = lastThumbUrl(r["thumbnail"]);
+    u.subscriberText = textOf(r["subscriberCountText"]);
+    u.description = textOf(r["descriptionSnippet"]);
     return u;
 }
 
-inline CommentThread fromCommentPayload(const rj::CommentPayload &p)
+// commentEntityPayload (frameworkUpdates mutations).
+inline CommentThread fromCommentPayload(Value p)
 {
     CommentThread t;
     Comment &c = t.comment;
-    if (p.properties) {
-        c.id = str(p.properties->commentId);
-        c.text = str(contentOf(p.properties->content));
-        c.publishedText = str(p.properties->publishedTime);
-    }
-    if (p.author) {
-        c.author.id = str(p.author->channelId);
-        c.author.name = str(p.author->displayName);
-        c.author.avatarUrl = str(p.author->avatarThumbnailUrl);
-        c.mine = p.author->isCurrentUser.value_or(false);
-    }
-    if (c.author.avatarUrl.empty() && p.avatar) c.author.avatarUrl = str(firstSourceUrl(p.avatar->image));
-    if (p.toolbar) {
-        c.likeText = str(p.toolbar->likeCountNotliked);
-        t.replyText = str(p.toolbar->replyCount);
-    }
+    const Value props = p["properties"], author = p["author"], toolbar = p["toolbar"];
+    c.id = props["commentId"].string();
+    c.text = props.path("content", "content").string();
+    c.publishedText = props["publishedTime"].string();
+    c.author.id = author["channelId"].string();
+    c.author.name = author["displayName"].string();
+    c.author.avatarUrl = author["avatarThumbnailUrl"].string();
+    c.mine = author["isCurrentUser"].boolean();
+    if (c.author.avatarUrl.empty()) c.author.avatarUrl = firstSourceUrl(p.path("avatar", "image"));
+    c.likeText = toolbar["likeCountNotliked"].string();
+    t.replyText = toolbar["replyCount"].string();
     return t;
 }
 
-// ---------------------------------------------------------------------------
-// The recursive collectors — single-pass scanner visitors. Leaf semantics
-// mirror the old walkers: a matched renderer subtree is typed-parsed and NOT
-// descended into, and no further keys of that object are considered.
-// ---------------------------------------------------------------------------
-
-// Per-open-object leaf flag shared by the collector visitors: set when a
-// renderer key was captured at this object, so its remaining members are
-// skipped (the old walkers' early `return`).
-struct CollectorBase {
-    std::vector<char> open;
-    void enter(int) { open.push_back(0); }
-    void leave(int) { open.pop_back(); }
-    bool consumed() const { return !open.empty() && open.back(); }
-    void consume() { if (!open.empty()) open.back() = 1; }
-};
-
-// ---------------------------------------------------------------------------
-// WithToken<Inner> — wraps a renderer collector and additionally captures the
-// FIRST continuation token found in document order. Equivalent to a separate
-// findContinuationToken pass because token wrappers never occur inside a
-// captured (consumed) renderer subtree (verified across the fixture corpus;
-// the golden dump is the regression gate).
-// ---------------------------------------------------------------------------
-template <class Inner>
-struct WithToken {
-    Inner inner;
-    std::string token;
-    void enter(int d) { inner.enter(d); }
-    void leave(int d) { inner.leave(d); }
-    scan::Action what(std::string_view key, int d)
-    {
-        if (token.empty() && gj::isTokenKey(key)) return scan::Action::Capture;
-        return inner.what(key, d);
-    }
-    void capture(std::string_view key, std::string_view value, int d)
-    {
-        if (gj::isTokenKey(key)) { gj::captureToken(key, value, &token); return; }
-        inner.capture(key, value, d);
-    }
-};
-
-// Video renderer kinds (all share the VideoR shape). playlistVideoRenderer lets
-// a VLxxxx browse (a playlist's contents) populate a VideoModel directly.
-inline bool isVideoKind(std::string_view key)
-{
-    return key == "videoRenderer" || key == "compactVideoRenderer"
-        || key == "gridVideoRenderer" || key == "playlistVideoRenderer";
-}
-
-inline bool isPlaylistKind(std::string_view key)
-{
-    return key == "playlistRenderer" || key == "gridPlaylistRenderer"
-        || key == "compactPlaylistRenderer";
-}
-
-// First occurrence (document-order DFS) of `key` anywhere in the tree → its
-// value extent. Empty when absent.
-struct KeyFinder {
-    std::string_view want;
-    std::string_view got;
-    void enter(int) {}
-    void leave(int) {}
-    scan::Action what(std::string_view key, int)
-    {
-        if (!got.empty()) return scan::Action::Skip;
-        return key == want ? scan::Action::Capture : scan::Action::Descend;
-    }
-    void capture(std::string_view, std::string_view v, int) { if (got.empty()) got = v; }
-};
-
-inline std::string_view findExtent(std::string_view json, std::string_view key)
-{
-    KeyFinder f{key, {}};
-    scan::document(json, f);
-    return f.got;
-}
-
 } // namespace yt
 #endif
diff --git a/src/parsers/rendererparser.h b/src/parsers/rendererparser.h
index 61a3dc3..22c669a 100644
--- a/src/parsers/rendererparser.h
+++ b/src/parsers/rendererparser.h
@@ -1,53 +1,50 @@
 #ifndef YT_RENDERERPARSER_H
 #define YT_RENDERERPARSER_H
-#include <string>
-#include <string>
-#include <string_view>
 #include <yt/types.h>
+#include "json/json.h"
+#include <string>
+#include <vector>
 namespace yt {
-// Every function takes raw JSON bytes: either a whole InnerTube response
-// (parse*List / parseChannel / parseAccountsList / parseWatchPage) or the inner
-// renderer object itself (parseVideoRenderer & co, parseText). The buffer only
-// needs to stay alive for the duration of the call.
-std::string parseText(std::string_view field);
-VideoItem parseVideoRenderer(std::string_view r);
+// Every function takes a json::Value: either a whole InnerTube response (a Reply's
+// json(): parse*List / parseChannel / parseAccountsList / parseWatchPage) or the inner
+// renderer object itself (parseVideoRenderer & co, parseText). Parsers never fail: an
+// absent or unexpected value yields empty fields / an empty list.
+std::string parseText(json::Value field);
+VideoItem parseVideoRenderer(json::Value r);
 // lockupViewModel (2024+ YouTube surfaces — watch-page related, some feeds/search —
 // replaced compactVideoRenderer/gridVideoRenderer). Caller passes the inner object.
-VideoItem parseLockupViewModel(std::string_view lm);
+VideoItem parseLockupViewModel(json::Value lm);
 // tileRenderer (TVHTML5 surfaces): the authed feeds (history/subscriptions/library)
 // arrive TV-shaped because the OAuth bearer is only honored by the TV client.
-VideoItem parseTileRenderer(std::string_view t);
-std::vector<VideoItem> parseVideoList(std::string_view response, std::string *nextToken);
+VideoItem parseTileRenderer(json::Value t);
+std::vector<VideoItem> parseVideoList(json::Value response, std::string *nextToken);
 // createCommentParams (out, nullable): the create-comment box's submit token, scraped
 // best-effort from the comments-section create-comment command (R4 — exact source
 // unverified; left empty when absent). core::postComment reads it to post a comment.
-std::vector<CommentThread> parseComments(std::string_view response, std::string *nextToken,
-                                 std::string *createCommentParams = 0);
-Playlist parsePlaylistRenderer(std::string_view r);
-std::vector<Playlist> parsePlaylistList(std::string_view response, std::string *nextToken);
+std::vector<CommentThread> parseComments(json::Value response, std::string *nextToken,
+                                 std::string *createCommentParams = nullptr);
+Playlist parsePlaylistRenderer(json::Value r);
+std::vector<Playlist> parsePlaylistList(json::Value response, std::string *nextToken);
 // A playlist page (browse of VL<id>): the header — title, description, owner, video count,
 // cover. Its entries are parseVideoList of the same response.
-Playlist parsePlaylistPage(const std::string &response);
+Playlist parsePlaylistPage(json::Value response);
 // Channel header (c4TabbedHeaderRenderer / pageHeaderRenderer) → Channel.
-// The response is a whole document; the const std::string & overload reads via
-// the NUL-terminated sentinel path (kInDoc) — production passes *r.body.
-Channel parseChannel(std::string_view response);
-Channel parseChannel(const std::string &response);
+Channel parseChannel(json::Value response);
 // The viewer's subscribe state on a channel — the first subscribeButtonRenderer.subscribed
 // anywhere (works for the WEB c4 header AND the TVHTML5 channelHeaderRenderer). Only an
 // AUTHED (bearer/TV) browse carries it; anonymous responses omit it → false.
-bool parseChannelSubscribed(std::string_view response);
-Channel parseUserRenderer(std::string_view r);
-std::vector<Channel> parseUserList(std::string_view response, std::string *nextToken);
+bool parseChannelSubscribed(json::Value response);
+Channel parseUserRenderer(json::Value r);
+std::vector<Channel> parseUserList(json::Value response, std::string *nextToken);
 // account/accounts_list (authed) → the active account's identity. The channel id is
 // reconstructed as "UC" + offlineCacheKeyToken.clientCacheKey (the youtubei.js trick —
 // the response carries no plain channelId).
-AccountInfo parseAccountsList(std::string_view response);
+AccountInfo parseAccountsList(json::Value response);
 // Watch page (/next): the primary video's details (title/description/views/likes +
 // channel name/avatar/id) and the related-videos list, in one response. relatedToken:
 // the related list's continuation (empty when there is none).
-void parseWatchPage(std::string_view response, Video *video, std::string *relatedToken = nullptr);
+void parseWatchPage(json::Value response, Video *video, std::string *relatedToken = nullptr);
 // navigation/resolve_url response → endpoint.browseEndpoint.browseId ("" when absent).
-std::string parseResolvedBrowseId(std::string_view response);
+std::string parseResolvedBrowseId(json::Value response);
 }
 #endif
diff --git a/src/parsers/videolistparser.cpp b/src/parsers/videolistparser.cpp
index e204745..245e847 100644
--- a/src/parsers/videolistparser.cpp
+++ b/src/parsers/videolistparser.cpp
@@ -1,90 +1,44 @@
+// Video lists: every renderer that is a video, anywhere in a browse / search / next
+// answer, plus the first continuation token in document order (rendererparser.h).
+#include "continuation.h"
 #include "rendererinternal.h"
 
 namespace yt {
 
-using namespace gj;
-
-std::string parseText(std::string_view field)
+std::string parseText(Value field)
 {
-    Text t{};
-    readJson(t, field);
-    return str(textOf(t));
+    return textOf(field);
 }
 
-struct VideoCollector : CollectorBase {
-    std::vector<VideoItem> *out;
-    scan::Action what(std::string_view key, int)
-    {
-        if (consumed()) return scan::Action::Skip;
-        if (key == "lockupViewModel" || key == "tileRenderer" || isVideoKind(key)) {
-            consume();
-            return scan::Action::Capture;
+std::vector<VideoItem> parseVideoList(Value response, std::string *nextToken)
+{
+    std::vector<VideoItem> out;
+    std::string token;
+    response.find({"lockupViewModel", "tileRenderer", "videoRenderer", "compactVideoRenderer", "gridVideoRenderer",
+                   "playlistVideoRenderer", "continuationCommand", "nextContinuationData", "reloadContinuationData"},
+                  [&](std::string_view key, Value v) {
+        if (isTokenKey(key)) {
+            if (token.empty()) token = tokenOf(key, v);
+            return Visit::Skip;
         }
-        return scan::Action::Descend;
-    }
-    void capture(std::string_view key, std::string_view value, int)
-    {
-        // lockupViewModel is a self-contained leaf (no nested video renderers); only
-        // the VIDEO content type becomes a Video (playlist/other lockups skipped).
+        // lockupViewModel and tileRenderer are self-contained leaves; only the VIDEO
+        // content type becomes a video (playlist/channel ones are skipped).
         if (key == "lockupViewModel") {
-            rj::Lockup lm{};
-            readJson(lm, value);
-            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_VIDEO")
-                out->push_back(fromLockupVideo(lm));
-        }
-        // tileRenderer is likewise a self-contained leaf (TVHTML5 feeds).
-        else if (key == "tileRenderer") {
-            rj::Tile t{};
-            readJson(t, value);
-            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_VIDEO")
-                out->push_back(fromTile(t));
+            if (v["contentType"].str() == "LOCKUP_CONTENT_TYPE_VIDEO") out.push_back(fromLockupVideo(v));
+        } else if (key == "tileRenderer") {
+            if (v["contentType"].str() == "TILE_CONTENT_TYPE_VIDEO") out.push_back(fromTile(v));
+        } else {
+            out.push_back(fromVideoRenderer(v));
         }
-        else {
-            rj::VideoR r{};
-            readJson(r, value);
-            out->push_back(fromVideoRenderer(r));
-        }
-    }
-};
-
-std::vector<VideoItem> parseVideoList(std::string_view response, std::string *nextToken)
-{
-    std::vector<VideoItem> out;
-    if (nextToken) {
-        WithToken<VideoCollector> w;
-        w.inner.out = &out;
-        scan::document(response, w);
-        *nextToken = std::string(w.token);
-    } else {
-        VideoCollector c;
-        c.out = &out;
-        scan::document(response, c);
-    }
+        return Visit::SkipRest;
+    });
+    if (nextToken) *nextToken = token;
     return out;
 }
 
-// ---------------------------------------------------------------------------
 // Direct single-renderer entry points (tests + callers holding the inner object).
-// ---------------------------------------------------------------------------
-VideoItem parseVideoRenderer(std::string_view r)
-{
-    rj::VideoR s{};
-    readJson(s, r);
-    return fromVideoRenderer(s);
-}
-
-VideoItem parseLockupViewModel(std::string_view lm)
-{
-    rj::Lockup s{};
-    readJson(s, lm);
-    return fromLockupVideo(s);
-}
-
-VideoItem parseTileRenderer(std::string_view t)
-{
-    rj::Tile s{};
-    readJson(s, t);
-    return fromTile(s);
-}
+VideoItem parseVideoRenderer(Value r) { return fromVideoRenderer(r); }
+VideoItem parseLockupViewModel(Value lm) { return fromLockupVideo(lm); }
+VideoItem parseTileRenderer(Value t) { return fromTile(t); }
 
 }
diff --git a/src/parsers/watchparser.cpp b/src/parsers/watchparser.cpp
index 85dbf04..6c468c7 100644
--- a/src/parsers/watchparser.cpp
+++ b/src/parsers/watchparser.cpp
@@ -1,46 +1,46 @@
+// The watch page (/next): the video's own header (title, views, date, likes, owner,
+// description) in its WEB and signed-in TVHTML5 shapes, and the related list
+// (rendererparser.h).
 #include "continuation.h"
 #include "rendererinternal.h"
 
 namespace yt {
 
-using namespace gj;
+using gj::first;
+
+namespace {
 
 // Best-effort like count (FRAGILE — YouTube reshapes this often): the legacy
 // toggleButtonRenderer.defaultText, else the modern likeButtonViewModel button title.
-static std::string findLikeText(std::string_view primaryInfo)
+std::string findLikeText(Value primaryInfo)
 {
-    std::string_view v = findExtent(primaryInfo, "toggleButtonRenderer");
-    if (!v.empty()) {
-        rj::ToggleButton tb{};
-        readJson(tb, v);
-        if (tb.defaultText) {
-            const std::string t = str(textOf(*tb.defaultText));
-            if (!t.empty()) return t;
-        }
-    }
-    v = findExtent(primaryInfo, "likeButtonViewModel");
-    if (!v.empty()) {
-        const std::string_view bvm = findExtent(v, "buttonViewModel");
-        if (!bvm.empty()) {
-            rj::ButtonVM b{};
-            readJson(b, bvm);
-            const std::string t = str(b.title);
-            // Skip the bare "Like"/"Dislike" labels — only a count-ish value is useful.
-            if (!t.empty() && t != "Like" && t != "Dislike") return t;
-        }
+    if (const Value tb = first(primaryInfo, "toggleButtonRenderer")) {
+        const std::string t = textOf(tb["defaultText"]);
+        if (!t.empty()) return t;
     }
+    const Value bvm = first(first(primaryInfo, "likeButtonViewModel"), "buttonViewModel");
+    const std::string t = bvm["title"].string();
+    // Skip the bare "Like"/"Dislike" labels — only a count-ish value is useful.
+    if (!t.empty() && t != "Like" && t != "Dislike") return t;
     return std::string();
 }
 
 // A button title is a real count only when it is a plain formatted integer
 // ("1,234" / "1 234" / "1234"); abbreviated forms ("1.2K") stay in likeText and
 // leave likeCount unknown. Returns -1 when the text carries no numeric count.
-static int64_t countFromTitle(const std::string &title)
+int64_t countFromTitle(const std::string &title)
 {
     if (title.empty()) return -1;
-    if (!gj::hasDigit(title)) return -1;      // pure label ("Like")
-    if (gj::hasAsciiLetter(title)) return -1;    // abbreviated ("1.2K")
-    return digitsOf(title);                                   // strips ',' / spaces
+    if (!gj::hasDigit(title)) return -1;          // pure label ("Like")
+    if (gj::hasAsciiLetter(title)) return -1;     // abbreviated ("1.2K")
+    return digitsOf(title);                       // strips ',' / spaces
+}
+
+// The engaged flag of a like/dislike view-model: its toggleButtonViewModel's, else its own.
+bool toggled(Value buttonViewModel)
+{
+    const Value tvm = first(buttonViewModel, "toggleButtonViewModel");
+    return (tvm ? tvm : buttonViewModel)["isToggled"].boolean();
 }
 
 // Account-tied engagement state (authed /next only): the like/dislike toggle.
@@ -49,245 +49,135 @@ static int64_t countFromTitle(const std::string &title)
 // like side's button title carries the count. Legacy shape — a toggleButtonRenderer
 // pair (first=like, second=dislike) each with `isToggled` + `defaultText`.
 // Defensive: unfound shapes leave likeStatus=0 / likeCount=-1 (Risk R1).
-static void findLikeState(std::string_view primaryInfo, int *likeStatus, int64_t *likeCount)
+void findLikeState(Value primaryInfo, int *likeStatus, int64_t *likeCount)
 {
     *likeStatus = 0;
     *likeCount = -1;
 
     // -- Modern: like/dislikeButtonViewModel (under segmentedLikeDislikeButtonViewModel).
-    const std::string_view likeVM = findExtent(primaryInfo, "likeButtonViewModel");
-    const std::string_view dislikeVM = findExtent(primaryInfo, "dislikeButtonViewModel");
-    if (!likeVM.empty() || !dislikeVM.empty()) {
-        bool disliked = false, liked = false;
-        if (!dislikeVM.empty()) {
-            const std::string_view tvm = findExtent(dislikeVM, "toggleButtonViewModel");
-            rj::ToggleStateVM s{};
-            readJson(s, tvm.empty() ? dislikeVM : tvm);
-            disliked = s.isToggled && *s.isToggled;
-        }
-        if (!likeVM.empty()) {
-            const std::string_view tvm = findExtent(likeVM, "toggleButtonViewModel");
-            rj::ToggleStateVM s{};
-            readJson(s, tvm.empty() ? likeVM : tvm);
-            liked = s.isToggled && *s.isToggled;
-            const std::string_view bvm = findExtent(likeVM, "buttonViewModel");
-            if (!bvm.empty()) {
-                rj::ButtonVM b{};
-                readJson(b, bvm);
-                *likeCount = countFromTitle(str(b.title));
-            }
-        }
+    const Value likeVM = first(primaryInfo, "likeButtonViewModel");
+    const Value dislikeVM = first(primaryInfo, "dislikeButtonViewModel");
+    if (likeVM || dislikeVM) {
+        const bool disliked = dislikeVM && toggled(dislikeVM);
+        const bool liked = likeVM && toggled(likeVM);
+        if (const Value bvm = first(likeVM, "buttonViewModel")) *likeCount = countFromTitle(bvm["title"].string());
         *likeStatus = disliked ? 2 : (liked ? 1 : 0);
         return;
     }
 
     // -- Legacy: the toggleButtonRenderer pair. First = like, second = dislike.
-    const std::string_view menu = findExtent(primaryInfo, "menuRenderer");
-    if (menu.empty()) return;
-    rj::TogglePair mr{};
-    readJson(mr, menu);
-    if (!mr.topLevelButtons) return;
     int idx = 0;
-    for (const std::map<std::string, rj::ToggleButton> &btn : *mr.topLevelButtons) {
-        std::map<std::string, rj::ToggleButton>::const_iterator it = btn.find("toggleButtonRenderer");
-        if (it == btn.end()) { ++idx; continue; }
-        const rj::ToggleButton &tb = it->second;
-        const bool on = tb.isToggled && *tb.isToggled;
-        if (idx == 0) {                                    // like button
-            if (on) *likeStatus = 1;
-            if (tb.defaultText) *likeCount = countFromTitle(str(textOf(*tb.defaultText)));
-        } else if (idx == 1 && on) {                       // dislike button
-            *likeStatus = 2;
+    for (Value btn : first(primaryInfo, "menuRenderer")["topLevelButtons"].items()) {
+        const Value tb = btn["toggleButtonRenderer"];
+        if (tb) {
+            const bool on = tb["isToggled"].boolean();
+            if (idx == 0) {                               // like button
+                if (on) *likeStatus = 1;
+                if (tb["defaultText"]) *likeCount = countFromTitle(textOf(tb["defaultText"]));
+            } else if (idx == 1 && on) {                  // dislike button
+                *likeStatus = 2;
+            }
         }
         ++idx;
     }
 }
 
-// WatchScanner — single-pass scan for parseWatchPage:
-//   (a) VideoCollector logic (consume semantics) → related list
-//   (b) first videoPrimaryInfoRenderer extent captured (no consume — siblings
-//       must still be visited so the related list is fully collected)
-//   (c) first videoSecondaryInfoRenderer extent captured (same — no consume)
-struct WatchScanner : CollectorBase {
-    std::vector<VideoItem> *related;
-    std::string_view primary;
-    std::string_view secondary;
-    std::string_view metadata;   // TVHTML5 singleColumnWatchNextResults (authed /next)
+// The owner block (videoOwnerRenderer) into v.channel; only empty fields are filled.
+void ownerInto(Value owner, ChannelRef &c)
+{
+    if (!owner) return;
+    if (c.name.empty()) c.name = textOf(owner["title"]);
+    if (c.avatarUrl.empty()) c.avatarUrl = lastThumbUrl(owner["thumbnail"]);
+    if (c.id.empty()) c.id = owner.path("navigationEndpoint", "browseEndpoint", "browseId").string();
+    // The viewer's subscribe state (authed /next only).
+    if (const Value s = owner.path("subscribeButton", "subscribeButtonRenderer", "subscribed")) c.subscribed = s.boolean();
+}
 
-    scan::Action what(std::string_view key, int)
-    {
-        if (consumed()) return scan::Action::Skip;
-        // Capture the info renderer extents without marking the parent as consumed,
-        // so sibling renderer keys are still visited (the related list collection).
-        if (primary.empty() && key == "videoPrimaryInfoRenderer")
-            return scan::Action::Capture;
-        if (secondary.empty() && key == "videoSecondaryInfoRenderer")
-            return scan::Action::Capture;
-        if (metadata.empty() && key == "videoMetadataRenderer")
-            return scan::Action::Capture;
-        // VideoCollector logic for related videos (consume semantics).
-        if (related && (key == "lockupViewModel" || key == "tileRenderer" || isVideoKind(key))) {
-            consume();
-            return scan::Action::Capture;
-        }
-        return scan::Action::Descend;
-    }
-    void capture(std::string_view key, std::string_view value, int)
-    {
-        if (key == "videoPrimaryInfoRenderer" && primary.empty()) {
-            primary = value; return;
-        }
-        if (key == "videoSecondaryInfoRenderer" && secondary.empty()) {
-            secondary = value; return;
-        }
-        if (key == "videoMetadataRenderer" && metadata.empty()) {
-            metadata = value; return;
+} // namespace
+
+void parseWatchPage(Value response, Video *video, std::string *relatedToken)
+{
+    Video v;
+    // One walk: the related list (renderer leaves, as in parseVideoList) and the first
+    // primary/secondary/metadata info renderers. The info renderers are taken without
+    // skipping the rest of their object, so sibling renderers are still collected.
+    Value pri, sec, meta;
+    response.find({"videoPrimaryInfoRenderer", "videoSecondaryInfoRenderer", "videoMetadataRenderer",
+                   "lockupViewModel", "tileRenderer", "videoRenderer", "compactVideoRenderer",
+                   "gridVideoRenderer", "playlistVideoRenderer"},
+                  [&](std::string_view key, Value r) {
+        if (key == "videoPrimaryInfoRenderer") {
+            if (!pri) pri = r;
+            return Visit::Skip;
+        }
+        if (key == "videoSecondaryInfoRenderer") {
+            if (!sec) sec = r;
+            return Visit::Skip;
+        }
+        if (key == "videoMetadataRenderer") {
+            if (!meta) meta = r;
+            return Visit::Skip;
         }
-        // Video renderer capture (same logic as VideoCollector).
-        if (!related) return;
         if (key == "lockupViewModel") {
-            rj::Lockup lm{};
-            readJson(lm, value);
-            if (lm.contentType && *lm.contentType == "LOCKUP_CONTENT_TYPE_VIDEO")
-                related->push_back(fromLockupVideo(lm));
+            if (r["contentType"].str() == "LOCKUP_CONTENT_TYPE_VIDEO") v.related.items.push_back(fromLockupVideo(r));
         } else if (key == "tileRenderer") {
-            rj::Tile t{};
-            readJson(t, value);
-            if (t.contentType && *t.contentType == "TILE_CONTENT_TYPE_VIDEO")
-                related->push_back(fromTile(t));
+            if (r["contentType"].str() == "TILE_CONTENT_TYPE_VIDEO") v.related.items.push_back(fromTile(r));
         } else {
-            rj::VideoR r{};
-            readJson(r, value);
-            related->push_back(fromVideoRenderer(r));
+            v.related.items.push_back(fromVideoRenderer(r));
         }
-    }
-};
-
-void parseWatchPage(std::string_view response, Video *video, std::string *relatedToken)
-{
-    Video v;
-    WatchScanner ws;
-    ws.related = &v.related.items;
-    scan::document(response, ws);
+        return Visit::SkipRest;
+    });
     // The related list's continuation sits at its end, under secondaryResults (WEB); a
     // whole-document search would find the comments section's token first.
-    if (relatedToken) *relatedToken = findContinuationToken(findExtent(response, "secondaryResults"));
+    if (relatedToken) *relatedToken = findContinuationToken(first(response, "secondaryResults"));
 
-    const std::string_view pri = ws.primary;
-    if (!pri.empty()) {
-        rj::PrimaryInfo info{};
-        readJson(info, pri);
-        v.title = str(textOf(info.title));
-        if (info.dateText) v.publishedText = str(textOf(*info.dateText));
-        const std::string_view vcr = findExtent(pri, "videoViewCountRenderer");
-        if (!vcr.empty()) {
-            rj::ViewCountR r{};
-            readJson(r, vcr);
-            if (r.viewCount) {
-                v.viewText = str(textOf(*r.viewCount));
-                v.viewCount = digitsOf(v.viewText);  // the WEB page shows the exact count
-            }
+    if (pri) {
+        v.title = textOf(pri["title"]);
+        v.publishedText = textOf(pri["dateText"]);
+        if (const Value vc = first(pri, "videoViewCountRenderer")["viewCount"]) {
+            v.viewText = textOf(vc);
+            v.viewCount = digitsOf(v.viewText);           // the WEB page shows the exact count
         }
         v.likeText = findLikeText(pri);
         int likeStatus = 0;
-        { int64_t n = -1; findLikeState(pri, &likeStatus, &n); if (n >= 0) v.likeCount = n; }
+        int64_t n = -1;
+        findLikeState(pri, &likeStatus, &n);
+        if (n >= 0) v.likeCount = n;
         v.myRating = likeStatus == 1 ? Rating::Like : likeStatus == 2 ? Rating::Dislike : Rating::None;
     }
-    const std::string_view sec = ws.secondary;
-    if (!sec.empty()) {
-        const std::string_view owner = findExtent(sec, "videoOwnerRenderer");
-        if (!owner.empty()) {
-            rj::OwnerR r{};
-            readJson(r, owner);
-            v.channel.name = str(textOf(r.title));
-            if (r.thumbnail) v.channel.avatarUrl = str(lastThumbUrl(*r.thumbnail));
-            if (r.navigationEndpoint && r.navigationEndpoint->browseEndpoint)
-                v.channel.id = str(r.navigationEndpoint->browseEndpoint->browseId);
-            // The viewer's subscribe state (authed /next only).
-            if (r.subscribeButton && r.subscribeButton->subscribeButtonRenderer
-                && r.subscribeButton->subscribeButtonRenderer->subscribed)
-                v.channel.subscribed = *r.subscribeButton->subscribeButtonRenderer->subscribed;
-        }
+    if (sec) {
+        ownerInto(first(sec, "videoOwnerRenderer"), v.channel);
         // Description: modern attributedDescription.content (plain string) or legacy runs.
-        const std::string_view ad = scan::topLevelValue(sec, "attributedDescription");
-        if (!ad.empty() && ad.front() == '{') {
-            ContentText c{};
-            readJson(c, ad);
-            v.description = str(contentOf(std::optional<ContentText>(c)));
-        }
-        if (v.description.empty()) {
-            const std::string_view d = scan::topLevelValue(sec, "description");
-            if (!d.empty()) {
-                Text t{};
-                readJson(t, d);
-                v.description = str(textOf(t));
-            }
-        }
+        v.description = sec.path("attributedDescription", "content").string();
+        if (v.description.empty()) v.description = textOf(sec["description"]);
     }
     // --- TVHTML5 shape (authed singleColumnWatchNextResults) --------------------
     // Signed-in /next carries a videoMetadataRenderer instead of videoPrimaryInfo/
     // videoSecondaryInfoRenderer: title/views/date live in it, owner→avatar/channel in
     // its videoOwnerRenderer, the like button outside it. Fill whatever the WEB path
     // left empty (a WEB response has meta empty, so this is a no-op there).
-    const std::string_view meta = ws.metadata;
-    if (!meta.empty()) {
-        if (v.title.empty()) {
-            const std::string_view t = scan::topLevelValue(meta, "title");
-            if (!t.empty() && t.front() == '{') { Text tt{}; readJson(tt, t); v.title = str(textOf(tt)); }
-        }
-        if (v.viewText.empty()) {
-            const std::string_view vcr = findExtent(meta, "videoViewCountRenderer");
-            if (!vcr.empty()) { rj::ViewCountR r{}; readJson(r, vcr); if (r.viewCount) v.viewText = str(textOf(*r.viewCount)); }
-        }
-        if (v.publishedText.empty()) {
-            const std::string_view pt = scan::topLevelValue(meta, "publishedTimeText");
-            if (!pt.empty() && pt.front() == '{') { Text tt{}; readJson(tt, pt); v.publishedText = str(textOf(tt)); }
-        }
-        const std::string_view owner = findExtent(meta, "videoOwnerRenderer");
-        if (!owner.empty()) {
-            rj::OwnerR r{}; readJson(r, owner);
-            if (v.channel.name.empty()) v.channel.name = str(textOf(r.title));
-            if (v.channel.avatarUrl.empty() && r.thumbnail) v.channel.avatarUrl = str(lastThumbUrl(*r.thumbnail));
-            if (v.channel.id.empty() && r.navigationEndpoint && r.navigationEndpoint->browseEndpoint)
-                v.channel.id = str(r.navigationEndpoint->browseEndpoint->browseId);
-            // The viewer's subscribe state (authed /next only).
-            if (r.subscribeButton && r.subscribeButton->subscribeButtonRenderer
-                && r.subscribeButton->subscribeButtonRenderer->subscribed)
-                v.channel.subscribed = *r.subscribeButton->subscribeButtonRenderer->subscribed;
-        }
+    if (meta) {
+        if (v.title.empty()) v.title = textOf(meta["title"]);
+        if (v.viewText.empty()) v.viewText = textOf(first(meta, "videoViewCountRenderer")["viewCount"]);
+        if (v.publishedText.empty()) v.publishedText = textOf(meta["publishedTimeText"]);
+        ownerInto(first(meta, "videoOwnerRenderer"), v.channel);
         // Description: the TV /next carries it in the structured-description engagement
         // panel (expandableVideoDescriptionBodyRenderer.descriptionBodyText), not in a
         // secondary renderer.
-        if (v.description.empty()) {
-            const std::string_view db = findExtent(response, "expandableVideoDescriptionBodyRenderer");
-            if (!db.empty()) {
-                const std::string_view dbt = scan::topLevelValue(db, "descriptionBodyText");
-                if (!dbt.empty() && dbt.front() == '{') { Text t{}; readJson(t, dbt); v.description = str(textOf(t)); }
-            }
-        }
+        if (v.description.empty())
+            v.description = textOf(first(response, "expandableVideoDescriptionBodyRenderer")["descriptionBodyText"]);
         // Like COUNT — YouTube's OWN displayed count (e.g. "8.8K"), from the like
         // control's likeCountText. (The TV toggleButtons are Captions/Surround; the like
         // count is on likeButtonRenderer, elsewhere in the tree — scan the whole response.
         // This replaces the RYD like fallback: RYD is dislikes-only now.)
-        if (v.likeText.empty()) {
-            const std::string_view lb = findExtent(response, "likeButtonRenderer");
-            if (!lb.empty()) {
-                const std::string_view lct = scan::topLevelValue(lb, "likeCountText");
-                if (!lct.empty() && lct.front() == '{') { Text t{}; readJson(t, lct); v.likeText = str(textOf(t)); }
-            }
-        }
+        if (v.likeText.empty()) v.likeText = textOf(first(response, "likeButtonRenderer")["likeCountText"]);
         // Like STATE — the viewer's like/dislike, from the authed likeStatusEntity in
         // frameworkUpdates. This is how the TV /next carries it (there is no like
         // toggleButton), so it restores the like/dislike button on every (re)entry.
         if (v.myRating == Rating::None) {
-            const std::string_view lse = findExtent(response, "likeStatusEntity");
-            if (!lse.empty()) {
-                rj::LikeStatusEntity e{}; readJson(e, lse);
-                if (e.likeStatus) {
-                    if      (*e.likeStatus == "LIKE")    v.myRating = Rating::Like;
-                    else if (*e.likeStatus == "DISLIKE") v.myRating = Rating::Dislike;
-                }
-            }
+            const std::string_view s = first(response, "likeStatusEntity")["likeStatus"].str();
+            if (s == "LIKE") v.myRating = Rating::Like;
+            else if (s == "DISLIKE") v.myRating = Rating::Dislike;
         }
     }
     if (video) *video = std::move(v);
diff --git a/tests/parserpayloads.h b/tests/parserpayloads.h
index 04ed360..1dbb283 100644
--- a/tests/parserpayloads.h
+++ b/tests/parserpayloads.h
@@ -108,7 +108,7 @@ inline const char *kNextLikedWeb = R"({"contents":{"twoColumnWatchNextResults":{
             }}
         ]}}
     }}
-]}}}}}})";
+]}}}}})";
 
 // Authed TVHTML5 /next: the owner block carries the viewer's subscribe state in
 // videoOwnerRenderer.subscribeButton.subscribeButtonRenderer.subscribed (a video the
@@ -122,7 +122,7 @@ inline const char *kNextSubscribedTv = R"({"contents":{"singleColumnWatchNextRes
             "subscribeButton": {"subscribeButtonRenderer": {"subscribed": true, "enabled": true}}
         }}
     }}
-]}}}}}})";
+]}}}}})";
 
 // Authed TVHTML5 /next: the like COUNT rides likeButtonRenderer.likeCountText (YouTube's
 // own "8.8K", NOT RYD), and the viewer's like STATE rides frameworkUpdates ->
diff --git a/tests/tst_parsers.cpp b/tests/tst_parsers.cpp
index a660fde..c83ce5a 100644
--- a/tests/tst_parsers.cpp
+++ b/tests/tst_parsers.cpp
@@ -1,4 +1,4 @@
-// The Glaze-backed InnerTube parsers on std types (port of MeeTube's
+// The InnerTube parsers (over yt::json) on std types (port of MeeTube's
 // tst_meetube_parsers: same fixtures, same expected values; CT:: field renames per
 // include/yt/types.h).
 #include <doctest/doctest.h>
@@ -61,24 +61,24 @@ TEST_CASE("flexIntParses") {
     CHECK(gj::flexInt(J("3.9")) == 3);
 }
 TEST_CASE("parseTextBehavior") {
-    CHECK(parseText(payloads::kSimpleText) == "hi");
-    CHECK(parseText(payloads::kRunsText) == "ab");
-    CHECK(parseText(payloads::kBareString).empty());
-    CHECK(parseText("5").empty());
-    CHECK(parseText("{}").empty());
+    CHECK(parseText(J(payloads::kSimpleText)) == "hi");
+    CHECK(parseText(J(payloads::kRunsText)) == "ab");
+    CHECK(parseText(J(payloads::kBareString)).empty());
+    CHECK(parseText(J("5")).empty());
+    CHECK(parseText(J("{}")).empty());
 }
 TEST_CASE("continuationFound") {
-    CHECK(findContinuationToken(std::string(payloads::kContinuationModern)) == "CTOKEN123");
+    CHECK(findContinuationToken(J(std::string(payloads::kContinuationModern))) == "CTOKEN123");
 }
 TEST_CASE("continuationLegacy") {
-    CHECK(findContinuationToken(std::string(payloads::kContinuationLegacy)) == "LEG456");
+    CHECK(findContinuationToken(J(std::string(payloads::kContinuationLegacy))) == "LEG456");
 }
 TEST_CASE("videoRendererAvatar") {
-    VideoItem v = parseVideoRenderer(std::string(payloads::kVideoRendererAvatar));
+    VideoItem v = parseVideoRenderer(J(std::string(payloads::kVideoRendererAvatar)));
     CHECK(v.channel.avatarUrl == "https://big/a.jpg");
 }
 TEST_CASE("playlistRendererParses") {
-    Playlist p = parsePlaylistRenderer(std::string(payloads::kPlaylistRenderer));
+    Playlist p = parsePlaylistRenderer(J(std::string(payloads::kPlaylistRenderer)));
     CHECK(p.id == "PL123");
     CHECK(p.title == "My List");
     CHECK(p.videoCount == 42);
@@ -86,39 +86,39 @@ TEST_CASE("playlistRendererParses") {
 }
 TEST_CASE("playlistVideosCollected") {
     std::string next;
-    std::vector<VideoItem> v = parseVideoList(std::string(payloads::kPlaylistVideos), &next);
+    std::vector<VideoItem> v = parseVideoList(J(std::string(payloads::kPlaylistVideos)), &next);
     REQUIRE(v.size() == 2);
     CHECK(v[0].id == "v1");
 }
 TEST_CASE("tilePlaylistParses") {
     std::string token;
-    std::vector<Playlist> ps = parsePlaylistList(std::string(payloads::kTilePlaylistLibrary), &token);
+    std::vector<Playlist> ps = parsePlaylistList(J(std::string(payloads::kTilePlaylistLibrary)), &token);
     REQUIRE(ps.size() == 1);
     CHECK(ps[0].id == "PLtest123");
     CHECK(ps[0].title == "My Mix");
     CHECK(ps[0].videoCount == 1709);
 }
 TEST_CASE("channelHeaderParses") {
-    Channel u = parseChannel(std::string(payloads::kC4Header));
+    Channel u = parseChannel(J(std::string(payloads::kC4Header)));
     CHECK(u.id == "UCxyz");
     CHECK(u.name == "Cool Channel");
     CHECK(u.subscriberText == "1.2M subscribers");
     CHECK(u.avatarUrl == "https://a/l.jpg");
-    Channel u2 = parseChannel(std::string(payloads::kC4HeaderBanner));
+    Channel u2 = parseChannel(J(std::string(payloads::kC4HeaderBanner)));
     CHECK(u2.bannerUrl == "http://b/c4big.jpg");
 }
 TEST_CASE("channel_extracts_subscribed") {
-    Channel u = parseChannel(std::string_view(payloads::kChannelSubscribed));
+    Channel u = parseChannel(J(std::string_view(payloads::kChannelSubscribed)));
     CHECK(u.subscribed);
 }
 TEST_CASE("channelSubscribedFlagParses") {
-    CHECK(parseChannelSubscribed(std::string_view(payloads::kChannelSubscribed)));
-    CHECK_FALSE(parseChannelSubscribed(std::string_view(payloads::kC4Header)));
+    CHECK(parseChannelSubscribed(J(std::string_view(payloads::kChannelSubscribed))));
+    CHECK_FALSE(parseChannelSubscribed(J(std::string_view(payloads::kC4Header))));
 }
 TEST_CASE("channelPageHeaderParses") {
     const std::string resp = loadFixtureRaw("channel_pageheader.json");
     REQUIRE(!resp.empty());
-    Channel u = parseChannel(resp);
+    Channel u = parseChannel(J(resp));
     CHECK(u.name == "Google for Developers");
     CHECK(u.subscriberText == "2.66M subscribers");
     CHECK(u.avatarUrl == "http://a/s160.jpg");
@@ -130,7 +130,7 @@ TEST_CASE("channelPageHeaderParses") {
 TEST_CASE("watchPageParses") {
     const std::string next = loadFixtureRaw("watch_next.json");
     Video primary;
-    parseWatchPage(next, &primary);
+    parseWatchPage(J(next), &primary);
     CHECK(primary.title == "The Title");
     CHECK(primary.description == "Hello description");
     CHECK(primary.channel.name == "Creator");
@@ -143,19 +143,19 @@ TEST_CASE("watchPageParses") {
 }
 TEST_CASE("watch_extracts_subscribe_state") {
     Video primary;
-    parseWatchPage(std::string_view(payloads::kNextSubscribedTv), &primary);
+    parseWatchPage(J(std::string_view(payloads::kNextSubscribedTv)), &primary);
     CHECK(primary.channel.subscribed);
     CHECK(primary.channel.id == "UCowner");
 }
 TEST_CASE("watch_tv_like_count_and_state") {
     Video primary;
-    parseWatchPage(std::string_view(payloads::kNextTvLikeState), &primary);
+    parseWatchPage(J(std::string_view(payloads::kNextTvLikeState)), &primary);
     CHECK(primary.likeText == "8.8K");
     CHECK(primary.myRating == Rating::Like);
 }
 TEST_CASE("watch_extracts_like_state") {
     Video primary;
-    parseWatchPage(std::string_view(payloads::kNextLikedWeb), &primary);
+    parseWatchPage(J(std::string_view(payloads::kNextLikedWeb)), &primary);
     CHECK(primary.myRating == Rating::Like);
     REQUIRE(primary.likeCount.has_value());
     CHECK(*primary.likeCount == 1234);
@@ -165,18 +165,18 @@ TEST_CASE("recursionDepthGuarded") {
     for (int i = 0; i < 500; ++i) deep += "{\"child\":";
     deep += "{\"continuationCommand\":{\"token\":\"DEEP\"}}";
     for (int i = 0; i < 500; ++i) deep += "}";
-    (void)findContinuationToken(deep);
+    (void)findContinuationToken(J(deep));
     std::string next;
-    std::vector<VideoItem> v = parseVideoList(deep, &next);
+    std::vector<VideoItem> v = parseVideoList(J(deep), &next);
     CHECK(v.empty());
 }
 TEST_CASE("continuationAbsent") {
-    CHECK(findContinuationToken(std::string(payloads::kContinuationAbsent)).empty());
+    CHECK(findContinuationToken(J(std::string(payloads::kContinuationAbsent))).empty());
 }
 TEST_CASE("videoList") {
     const std::string resp = loadFixtureRaw("search_videos.json");
     std::string token;
-    std::vector<VideoItem> v = parseVideoList(resp, &token);
+    std::vector<VideoItem> v = parseVideoList(J(resp), &token);
     REQUIRE(v.size() == 2);
     CHECK(v[0].id == "aaa11111111");
     CHECK(v[0].title == "First Video");
@@ -192,7 +192,7 @@ TEST_CASE("videoListBrowseFeed") {
     const std::string resp = loadFixtureRaw("browse_feed.json");
     REQUIRE(!resp.empty());
     std::string token;
-    std::vector<VideoItem> v = parseVideoList(resp, &token);
+    std::vector<VideoItem> v = parseVideoList(J(resp), &token);
     REQUIRE(v.size() == 2);
     CHECK(v[0].id == "ccc33333333");
     CHECK(v[0].title == "Feed One");
@@ -203,7 +203,7 @@ TEST_CASE("lockupRelatedParses") {
     const std::string resp = loadFixtureRaw("next_lockup.json");
     REQUIRE(!resp.empty());
     std::string token;
-    std::vector<VideoItem> v = parseVideoList(resp, &token);
+    std::vector<VideoItem> v = parseVideoList(J(resp), &token);
     REQUIRE(v.size() == 2);
     CHECK(v[0].id == "Zi_XLOBDo_Y");
     CHECK(v[0].title == "Michael Jackson - Billie Jean (Official Video)");
@@ -221,7 +221,7 @@ TEST_CASE("lockupRelatedParses") {
 TEST_CASE("a channel tab lockup has no channel row") {
     const std::string resp = loadFixtureRaw("channel_videos_lockup.json");
     std::string token;
-    std::vector<VideoItem> v = parseVideoList(resp, &token);
+    std::vector<VideoItem> v = parseVideoList(J(resp), &token);
     REQUIRE(v.size() == 1);
     CHECK(v[0].id == "5b3n-u_0QAI");
     CHECK(v[0].channel.name.empty());
@@ -231,7 +231,7 @@ TEST_CASE("a channel tab lockup has no channel row") {
 TEST_CASE("comments") {
     const std::string p = loadFixtureRaw("comments_page.json");
     std::string token;
-    std::vector<CommentThread> c = parseComments(p, &token);
+    std::vector<CommentThread> c = parseComments(J(p), &token);
     REQUIRE(c.size() == 2);
     CHECK(c[0].comment.author.name == "Alice");
     CHECK(c[0].comment.text == "Nice video");
@@ -242,7 +242,7 @@ TEST_CASE("parsesPlaylistLockups") {
     const std::string j = loadFixtureRaw("browse_playlists_lockup.json");
     REQUIRE(!j.empty());
     std::string token;
-    const std::vector<Playlist> p = parsePlaylistList(j, &token);
+    const std::vector<Playlist> p = parsePlaylistList(J(j), &token);
     REQUIRE(p.size() == 1);
     CHECK(p[0].id == "PLOU2XLYxmsIJ78oALT6XEKLHNFEggOI75");
     CHECK(p[0].title == "GDG Inspiring Stories");
@@ -252,19 +252,19 @@ TEST_CASE("parsesPlaylistLockups") {
 TEST_CASE("parsesAccountsList") {
     const std::string j = loadFixtureRaw("accounts_list.json");
     REQUIRE(!j.empty());
-    const AccountInfo a = parseAccountsList(j);
+    const AccountInfo a = parseAccountsList(J(j));
     CHECK(a.name == "Ivan Petrov");
     CHECK(a.handle == "@ivanpetrov");
     CHECK(a.avatarUrl == "https://yt3.example/big.jpg");
     CHECK(a.channelId == "UCabc123def");
-    CHECK(parseAccountsList("{}").name.empty());
-    CHECK(parseAccountsList("not json at all").name.empty());
+    CHECK(parseAccountsList(J("{}")).name.empty());
+    CHECK(parseAccountsList(J("not json at all")).name.empty());
 }
 TEST_CASE("parsesTileRenderers") {
     const std::string j = loadFixtureRaw("tiles_history.json");
     REQUIRE(!j.empty());
     std::string token;
-    const std::vector<VideoItem> v = parseVideoList(j, &token);
+    const std::vector<VideoItem> v = parseVideoList(J(j), &token);
     REQUIRE(v.size() == 2);
     CHECK(v[0].id == "u7OQ7kKBEHs");
     CHECK(v[0].title == "What if You Encounter an ENEMY");
@@ -287,7 +287,7 @@ TEST_CASE("accountsListNestedItemsDepthBounded") {
     for (int i = 0; i < n; ++i) deep += "{\"accountItem\":";
     deep += "{}";
     for (int i = 0; i < n; ++i) deep += "}";
-    const AccountInfo a = parseAccountsList(deep);   // must return, not overflow the stack
+    const AccountInfo a = parseAccountsList(J(deep));   // must return, not overflow the stack
     CHECK(a.name.empty());
 }
 
@@ -525,7 +525,7 @@ TEST_CASE("a comment carries its id, author channel, likes, replies and mine") {
         "\"isCurrentUser\":true},"
         "\"toolbar\":{\"likeCountNotliked\":\"291K\",\"replyCount\":\"675\"}}}}]}}}";
     std::string token;
-    const std::vector<CommentThread> c = parseComments(page, &token);
+    const std::vector<CommentThread> c = parseComments(J(page), &token);
     REQUIRE(c.size() == 1);
     CHECK(c[0].comment.id == "Ugxzbv");
     CHECK(c[0].comment.text == "Hi");
@@ -551,7 +551,7 @@ TEST_CASE("a watch page: date, exact views and the related list's continuation")
         "{\"continuationItemRenderer\":{\"continuationEndpoint\":{\"continuationCommand\":{\"token\":\"RELATED\"}}}}]}}}}}";
     Video v;
     std::string related;
-    parseWatchPage(next, &v, &related);
+    parseWatchPage(J(next), &v, &related);
     CHECK(v.title == "T");
     CHECK(v.publishedText == "Oct 25, 2009");
     CHECK(v.viewText == "1,234,567 views");
@@ -568,14 +568,14 @@ TEST_CASE("a token nested in innertubeCommand is found") {
         "{\"contents\":[{\"lockupViewModel\":{\"contentId\":\"aaa11111111\",\"contentType\":\"LOCKUP_CONTENT_TYPE_VIDEO\"}},"
         "{\"continuationItemViewModel\":{\"continuationCommand\":{\"innertubeCommand\":{"
         "\"continuationCommand\":{\"token\":\"NESTED\"}}}}}]}";
-    CHECK(findContinuationToken(page) == "NESTED");
+    CHECK(findContinuationToken(J(page)) == "NESTED");
     std::string token;
-    const std::vector<VideoItem> v = parseVideoList(page, &token);
+    const std::vector<VideoItem> v = parseVideoList(J(page), &token);
     CHECK(v.size() == 1);
     CHECK(token == "NESTED");
 }
 TEST_CASE("a playlist page's header comes from its sidebar") {
-    const Playlist p = parsePlaylistPage(loadFixtureRaw("playlist_page.json"));
+    const Playlist p = parsePlaylistPage(J(loadFixtureRaw("playlist_page.json")));
     CHECK(p.title == "Popular Music Videos");
     CHECK(p.description == "The hits.");
     CHECK(p.videoCount == 182);
@@ -598,7 +598,7 @@ TEST_CASE("a search lockup's single row still starts with the channel") {
         "\"metadataRows\":[{\"metadataParts\":[{\"text\":{\"content\":\"Rick Astley\"}},"
         "{\"text\":{\"content\":\"207M views\"}},{\"text\":{\"content\":\"16 years ago\"}}]}]}}}}}}]}";
     std::string token;
-    const std::vector<VideoItem> v = parseVideoList(page, &token);
+    const std::vector<VideoItem> v = parseVideoList(J(page), &token);
     REQUIRE(v.size() == 2);
     CHECK(v[0].channel.name == "Rick Astley");
     CHECK(v[0].channel.id == "UCuAXFkgsw1L7xaCfnd5JJOw");
@@ -607,3 +607,16 @@ TEST_CASE("a search lockup's single row still starts with the channel") {
     CHECK(v[1].channel.name == "Rick Astley");
     CHECK(v[1].viewText == "207M views");
 }
+
+// Signed in, the comments page's create box sits under onResponseReceivedEndpoints; the old
+// scanner took that subtree whole (for the page token) and never found createCommentParams.
+TEST_CASE("createCommentParamsUnderResponseEndpoints") {
+    std::string tok, params;
+    parseComments(J(R"({"onResponseReceivedEndpoints":[{"reloadContinuationItemsCommand":{"continuationItems":[
+        {"commentsHeaderRenderer":{"createRenderer":{"commentSimpleboxRenderer":{"submitButton":{"buttonRenderer":
+            {"serviceEndpoint":{"createCommentEndpoint":{"createCommentParams":"CP"}}}}}}}},
+        {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"NEXT"}}}}]}}]})"),
+        &tok, &params);
+    CHECK(params == "CP");
+    CHECK(tok == "NEXT");
+}
````

---

### Task 4: request bodies and the context through `json::Writer`

**Files:**
- Modify: `src/innertube/bodies.{h,cpp}`, `src/innertube/contextbuilder.cpp`, `tests/tst_bodies.cpp`

**Interfaces:**
- Consumes: `json::Writer` (plan 1).
- Produces: unchanged signatures in `bodies.h` / `contextbuilder.h`; output byte-identical to Glaze's for every existing `tst_bodies`/`tst_context` case, except that control bytes are now escaped.

- [ ] **Step 1: Apply the test and see it fail**

Save as `/tmp/t4.patch`; `git apply --include='tests/*' /tmp/t4.patch`; `cmake --build build -j && ./build/tests/tst_bodies`
Expected: `control bytes are escaped` fails — Glaze wrote `\x01` and `\x1f` raw.

- [ ] **Step 2: Implement**

`git apply --exclude='tests/*' /tmp/t4.patch`

- [ ] **Step 3: Run**

`cmake --build build -j && (cd build && ctest -j8)` — expected all green; every earlier body and context string unchanged byte for byte.

- [ ] **Step 4: Commit**

```bash
git add src/innertube/bodies.h src/innertube/bodies.cpp src/innertube/contextbuilder.cpp tests/tst_bodies.cpp
git commit -m "fix(lib): request bodies escape control bytes (json::Writer replaces Glaze)" -m "Glaze wrote control bytes other than \\b\\f\\n\\r\\t raw — invalid JSON that YouTube
refuses — so a comment or query carrying one (pasted text) could not be sent. Bodies
and the context are now written by json::Writer, which escapes them as \\u00XX; every
other byte of every body is unchanged." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

````diff
diff --git a/src/innertube/bodies.cpp b/src/innertube/bodies.cpp
index 3cafc35..73315e1 100644
--- a/src/innertube/bodies.cpp
+++ b/src/innertube/bodies.cpp
@@ -1,203 +1,117 @@
 #include "bodies.h"
 #include "innertube/catalog.h"
-#include "json/ytjson.h"
+#include "json/json.h"
 
 namespace yt {
 namespace bodies {
 
-// Body shapes (Glaze reflection; std::nullopt members are omitted on write —
-// glz's default skip_null_members — which is how the conditional fields work).
-namespace bj {
-
-struct Browse {
-    std::string browseId;
-    std::optional<std::string> params;
-};
-struct Search {
-    std::string query;
-    std::optional<std::string> params;
-};
-struct Next {
-    std::optional<std::string> videoId;
-    std::optional<std::string> continuation;
-};
-struct ContentPlaybackContext {
-    std::string html5Preference = "HTML5_PREF_WANTS";
-    // From base.js (innertube/playerjs.*); omitted while unknown.
-    std::optional<int> signatureTimestamp;
-};
-struct PlaybackContext {
-    ContentPlaybackContext contentPlaybackContext;
-};
-struct Player {
-    std::string videoId;
-    bool contentCheckOk = true;
-    bool racyCheckOk = true;
-    std::optional<std::string> cpn;
-    std::optional<PlaybackContext> playbackContext;   // TVHTML5 only (else "reloaded")
-};
-struct Resolve {
-    std::string url;
-};
-struct ReadMask {
-    bool returnOwner = true;
-};
-struct AccountsList {
-    ReadMask accountReadMask;
-};
-struct Subscribe {
-    std::vector<std::string> channelIds;
-};
-struct Target {
-    std::string videoId;
-};
-struct Like {
-    Target target;
-};
-struct CreateComment {
-    std::string createCommentParams;
-    std::string commentText;
-};
-struct EditAction {
-    std::string action;
-    std::optional<std::string> addedVideoId;   // omitted when nullopt (add only)
-    std::optional<std::string> removedVideoId; // omitted when nullopt (remove only)
-};
-struct EditPlaylist {
-    std::string playlistId;
-    std::vector<EditAction> actions;
-};
-
-struct RapidQr {
-    std::string qrPresetStyle = Catalog::kQrPresetStyle;
-    std::string userCode;
-    std::string rapidQrFeature = "RAPID_QR_FEATURE_DEFAULT";
-};
-struct HandoffQr {
-    RapidQr rapidQrParams;
-};
-struct Mdx {
-    HandoffQr handoffQrParams;
-};
-
-} // namespace bj
-
-template <class T>
-static std::string dump(const T &body)
-{
-    return glz::write_json(body).value_or(std::string("{}"));
-}
+// Every body is one small object written field by field; members that are absent for a
+// call are simply not written (the old Glaze shapes' nullopt fields).
+namespace {
 
-static std::optional<std::string> opt(std::string_view s)
+// {"<key>": "<value>"} — most bodies are one string member.
+std::string one(std::string_view key, std::string_view value)
 {
-    if (s.empty()) return std::nullopt;
-    return std::string(s);
+    return json::Writer().beginObject().member(key, value).endObject().take();
 }
 
+} // namespace
+
 std::string browse(std::string_view browseId, std::string_view params)
 {
-    bj::Browse b;
-    b.browseId = std::string(browseId);
-    b.params = opt(params);                          // tab selector, e.g. a channel's Videos tab
-    return dump(b);
+    json::Writer w;
+    w.beginObject().member("browseId", browseId);
+    if (!params.empty()) w.member("params", params);   // tab selector, e.g. a channel's Videos tab
+    return w.endObject().take();
 }
 
 std::string search(std::string_view query, std::string_view params)
 {
-    bj::Search b;
-    b.query = std::string(query);
-    if (!params.empty()) b.params = std::string(params);
-    return dump(b);
+    json::Writer w;
+    w.beginObject().member("query", query);
+    if (!params.empty()) w.member("params", params);
+    return w.endObject().take();
 }
 
-std::string nextVideo(std::string_view videoId)
-{
-    bj::Next b;
-    b.videoId = std::string(videoId);
-    return dump(b);
-}
+std::string nextVideo(std::string_view videoId) { return one("videoId", videoId); }
 
-std::string continuation(std::string_view token)
-{
-    bj::Next b;
-    b.continuation = std::string(token);
-    return dump(b);
-}
+std::string continuation(std::string_view token) { return one("continuation", token); }
 
 std::string player(std::string_view videoId, const PlayerBody &pb)
 {
-    bj::Player b;
-    b.videoId = std::string(videoId);
+    json::Writer w;
+    w.beginObject().member("videoId", videoId).member("contentCheckOk", true).member("racyCheckOk", true);
     if (pb.tv) {
-        b.cpn = pb.cpn;
-        b.playbackContext = bj::PlaybackContext{};
-        if (pb.signatureTimestamp > 0)
-            b.playbackContext->contentPlaybackContext.signatureTimestamp = pb.signatureTimestamp;
+        w.member("cpn", pb.cpn)
+            .key("playbackContext").beginObject()
+            .key("contentPlaybackContext").beginObject().member("html5Preference", "HTML5_PREF_WANTS");
+        // From base.js (innertube/playerjs.*); omitted while unknown.
+        if (pb.signatureTimestamp > 0) w.member("signatureTimestamp", pb.signatureTimestamp);
+        w.endObject().endObject();
     }
-    return dump(b);
+    return w.endObject().take();
 }
 
-std::string resolveUrl(std::string_view url)
-{
-    bj::Resolve b;
-    b.url = std::string(url);
-    return dump(b);
-}
+std::string resolveUrl(std::string_view url) { return one("url", url); }
 
 std::string accountsList()
 {
-    return dump(bj::AccountsList{});
+    return json::Writer().beginObject()
+        .key("accountReadMask").beginObject().member("returnOwner", true).endObject()
+        .endObject().take();
 }
 
 std::string subscribeChannels(std::string_view channelId)
 {
-    bj::Subscribe b;
-    b.channelIds.push_back(std::string(channelId));
-    return dump(b);
+    return json::Writer().beginObject().key("channelIds").beginArray().str(channelId).endArray().endObject().take();
 }
 
 std::string likeTarget(std::string_view videoId)
 {
-    bj::Like b;
-    b.target.videoId = std::string(videoId);
-    return dump(b);
+    return json::Writer().beginObject()
+        .key("target").beginObject().member("videoId", videoId).endObject()
+        .endObject().take();
 }
 
 std::string createComment(std::string_view createCommentParams, std::string_view text)
 {
-    bj::CreateComment b;
-    b.createCommentParams = std::string(createCommentParams);
-    b.commentText = std::string(text);
-    return dump(b);
+    return json::Writer().beginObject()
+        .member("createCommentParams", createCommentParams).member("commentText", text)
+        .endObject().take();
 }
 
+namespace {
+
+// One edit_playlist action: {"playlistId", "actions": [{"action", "<idKey>": videoId}]}.
+std::string editPlaylist(std::string_view playlistId, const char *action, const char *idKey, std::string_view videoId)
+{
+    return json::Writer().beginObject()
+        .member("playlistId", playlistId)
+        .key("actions").beginArray().beginObject().member("action", action).member(idKey, videoId).endObject().endArray()
+        .endObject().take();
+}
+
+} // namespace
+
 std::string playlistAddVideo(std::string_view playlistId, std::string_view videoId)
 {
-    bj::EditAction a;
-    a.action = "ACTION_ADD_VIDEO";
-    a.addedVideoId = std::string(videoId);
-    bj::EditPlaylist b;
-    b.playlistId = std::string(playlistId);
-    b.actions.push_back(a);
-    return dump(b);
+    return editPlaylist(playlistId, "ACTION_ADD_VIDEO", "addedVideoId", videoId);
 }
 
 std::string playlistRemoveVideo(std::string_view playlistId, std::string_view videoId)
 {
-    bj::EditAction a;
-    a.action = "ACTION_REMOVE_VIDEO_BY_VIDEO_ID";
-    a.removedVideoId = std::string(videoId);
-    bj::EditPlaylist b;
-    b.playlistId = std::string(playlistId);
-    b.actions.push_back(a);
-    return dump(b);
+    return editPlaylist(playlistId, "ACTION_REMOVE_VIDEO_BY_VIDEO_ID", "removedVideoId", videoId);
 }
 
 std::string mdxHandoff(std::string_view userCode)
 {
-    bj::Mdx b;
-    b.handoffQrParams.rapidQrParams.userCode = std::string(userCode);
-    return dump(b);
+    return json::Writer().beginObject()
+        .key("handoffQrParams").beginObject()
+        .key("rapidQrParams").beginObject()
+        .member("qrPresetStyle", Catalog::kQrPresetStyle).member("userCode", userCode)
+        .member("rapidQrFeature", "RAPID_QR_FEATURE_DEFAULT")
+        .endObject().endObject()
+        .endObject().take();
 }
 
 }
diff --git a/src/innertube/bodies.h b/src/innertube/bodies.h
index 5c2e4f3..658b3db 100644
--- a/src/innertube/bodies.h
+++ b/src/innertube/bodies.h
@@ -1,9 +1,9 @@
 #ifndef YT_BODIES_H
 #define YT_BODIES_H
 // Serialized InnerTube POST bodies (WITHOUT the context block — the transport
-// splices that in; see core::Http::post). One function per endpoint body
-// shape so the Glaze include cost is paid once, in bodies.cpp: the requests see
-// only std::string_view -> std::string.
+// splices that in; see core::Http::post). One function per endpoint body shape,
+// written with json::Writer in bodies.cpp: the requests see only
+// std::string_view -> std::string.
 //
 // Every builder returns a minified JSON OBJECT ("{...}") — the transport's
 // context splice relies on that.
diff --git a/src/innertube/contextbuilder.cpp b/src/innertube/contextbuilder.cpp
index 0df6573..787e403 100644
--- a/src/innertube/contextbuilder.cpp
+++ b/src/innertube/contextbuilder.cpp
@@ -1,40 +1,8 @@
 #include "contextbuilder.h"
 #include "catalog.h"
-#include "json/ytjson.h"
+#include "json/json.h"
 namespace yt {
 
-// The context block, typed (Glaze reflection; nullopt fields are omitted).
-namespace cj {
-
-struct Client {
-    std::string clientName;
-    std::string clientVersion;
-    std::string hl;
-    std::string gl;
-    std::optional<std::string> deviceMake;
-    std::optional<std::string> deviceModel;
-    std::optional<std::string> osName;
-    std::optional<std::string> osVersion;
-    std::optional<int> androidSdkVersion;
-    std::optional<std::string> platform;
-    std::optional<std::string> visitorData;
-    std::optional<std::string> userAgent;          // VISIONOS repeats its UA in the context
-};
-struct User {
-    bool lockedSafetyMode = false;
-};
-struct Request {
-    bool useSsl = true;
-    std::vector<int> internalExperimentFlags;   // always the empty array
-};
-struct Context {
-    Client client;
-    User user;
-    Request request;
-};
-
-} // namespace cj
-
 // IOS is used ONLY for the anonymous /player and must stay "clean" (no visitorData /
 // X-Goog-Visitor-Id) or YouTube returns a SABR-only player response without the
 // hlsManifestUrl we play (device-verified). ANDROID_VR is the OPPOSITE since the
@@ -49,34 +17,40 @@ static bool omitVisitor(ClientId id) {
 
 std::string ContextBuilder::contextJson(ClientId id, const Session &s) {
     const ClientInfo &ci = s.clients[(int) id];
-    cj::Context ctx;
-    ctx.client.clientName = ci.name;
-    ctx.client.clientVersion = ci.version;
-    ctx.client.hl = s.hl;
-    ctx.client.gl = s.gl;
+    // The device fields each client identity sends; absent ones are omitted.
+    const char *make = nullptr, *model = nullptr, *os = nullptr, *osVersion = nullptr, *platform = nullptr;
+    int sdk = 0;
     if (id == ClientId::IOS) {
-        ctx.client.deviceMake = "Apple"; ctx.client.deviceModel = "iPhone16,2";
-        ctx.client.osName = "iOS"; ctx.client.osVersion = "18.0";
+        make = "Apple"; model = "iPhone16,2"; os = "iOS"; osVersion = "18.0";
     } else if (id == ClientId::ANDROID) {
-        ctx.client.androidSdkVersion = 34; ctx.client.osName = "Android"; ctx.client.osVersion = "14";
-        ctx.client.platform = "MOBILE";
+        sdk = 34; os = "Android"; osVersion = "14"; platform = "MOBILE";
     } else if (id == ClientId::ANDROID_VR) {
-        ctx.client.androidSdkVersion = 32; ctx.client.osName = "Android"; ctx.client.osVersion = "12L";
-        ctx.client.deviceMake = "Oculus"; ctx.client.deviceModel = "Quest 3"; ctx.client.platform = "MOBILE";
+        sdk = 32; os = "Android"; osVersion = "12L"; make = "Oculus"; model = "Quest 3"; platform = "MOBILE";
     } else if (id == ClientId::VISIONOS) {
-        ctx.client.deviceMake = "Apple"; ctx.client.deviceModel = "RealityDevice14,1";
-        ctx.client.osName = "visionOS"; ctx.client.osVersion = "1.0.2.21O209";
-        ctx.client.userAgent = ci.userAgent;
+        make = "Apple"; model = "RealityDevice14,1"; os = "visionOS"; osVersion = "1.0.2.21O209";
     }
+    json::Writer w;
+    w.beginObject().key("client").beginObject()
+        .member("clientName", ci.name).member("clientVersion", ci.version).member("hl", s.hl).member("gl", s.gl);
+    if (make) w.member("deviceMake", make);
+    if (model) w.member("deviceModel", model);
+    if (os) w.member("osName", os).member("osVersion", osVersion);
+    if (sdk) w.member("androidSdkVersion", sdk);
+    if (platform) w.member("platform", platform);
     // Omit visitorData ONLY on IOS (see omitVisitor above): there a cross-client
     // visitorData is a SABR "distrust" signal that strips the hlsManifestUrl we play,
     // while on ANDROID_VR the visitorData is what CLEARS the anti-bot LOGIN_REQUIRED.
-    if (!omitVisitor(id) && !s.visitorData.empty())
-        ctx.client.visitorData = s.visitorData;
+    if (!omitVisitor(id) && !s.visitorData.empty()) w.member("visitorData", s.visitorData);
+    if (id == ClientId::VISIONOS) w.member("userAgent", ci.userAgent);   // VISIONOS repeats its UA
     // user + request: minimum-viable shape real clients send; harmless when
     // unneeded, but several endpoints behave better with it present (see
     // docs/INNERTUBE_API.md §5).
-    return glz::write_json(ctx).value_or(std::string("{}"));
+    w.endObject()
+        .key("user").beginObject().member("lockedSafetyMode", false).endObject()
+        .key("request").beginObject().member("useSsl", true)
+            .key("internalExperimentFlags").beginArray().endArray().endObject()
+        .endObject();
+    return w.take();
 }
 
 std::vector<Header> ContextBuilder::headers(ClientId id, const Session &s) {
diff --git a/tests/tst_bodies.cpp b/tests/tst_bodies.cpp
index 84a5955..bd7c7a3 100644
--- a/tests/tst_bodies.cpp
+++ b/tests/tst_bodies.cpp
@@ -48,3 +48,9 @@ TEST_CASE("playlist edits") {
     CHECK(bodies::playlistRemoveVideo("PLabc", "vid42") ==
           "{\"playlistId\":\"PLabc\",\"actions\":[{\"action\":\"ACTION_REMOVE_VIDEO_BY_VIDEO_ID\",\"removedVideoId\":\"vid42\"}]}");
 }
+// Glaze wrote a control byte raw — invalid JSON that YouTube refuses — so a comment or a
+// query carrying one (pasted text) failed to send. It must go out escaped.
+TEST_CASE("control bytes are escaped") {
+    CHECK(bodies::createComment("P", "a\x01" "b") == "{\"createCommentParams\":\"P\",\"commentText\":\"a\\u0001b\"}");
+    CHECK(bodies::search("x\ty\x1f", "") == "{\"query\":\"x\\ty\\u001f\"}");
+}
````

---

### Task 5: Glaze, the shapes and the scanner leave; docs; full verification

**Files:**
- Delete: `src/json/jsonscan.h`, `src/json/tokenscan.h`, `src/json/shapes/` (11 headers), the `third_party/glaze` submodule
- Modify: `src/json/ytjson.h` (the Glaze half removed), `CMakeLists.txt`, `tests/CMakeLists.txt`, `examples/CMakeLists.txt`, `examples/yt-cli/main.cpp` (state as key=value), `.gitignore`, `AGENTS.md`, `README.md`

**Interfaces:**
- Consumes: Tasks 1–4 (nothing uses the deleted files any more).
- Produces: a library whose only JSON code is `src/json/json.*`, `ytjson.h`, `json3.*`; CMake option `INNERTUBE_GLAZE_DIR` removed; yt-cli reads/writes `./yt-cli-state.txt`.

- [ ] **Step 1: Prove nothing uses what is deleted**

Run: `grep -rln 'glz\|glaze\|readJson\|shapes/\|jsonscan\|tokenscan\|FlexInt\|toInt64\|kScanMaxDepth\|scan::' src tests examples conformance backends include`
Expected: only `src/json/jsonscan.h`, `src/json/tokenscan.h`, `src/json/shapes/*`, `src/json/ytjson.h`, `examples/yt-cli/main.cpp` (the files this task changes or deletes).

- [ ] **Step 2: Delete**

```bash
git rm -rq src/json/jsonscan.h src/json/tokenscan.h src/json/shapes
git submodule deinit -f third_party/glaze && git rm -f third_party/glaze && rm -rf .git/modules/third_party/glaze
```

- [ ] **Step 3: Apply the rest**

Save the patch as `/tmp/t5.patch`; `git apply /tmp/t5.patch` (it touches no test sources except `tests/CMakeLists.txt`). Then the ignore entry for yt-cli's renamed state file:
`sed -i 's/^yt-cli-state\.json$/yt-cli-state.txt/' .gitignore`

- [ ] **Step 4: Build all four trees from scratch**

```bash
for t in build build-asan build-tsan build-none; do cmake --build $t --target clean; done
cmake -S . -B build -G Ninja && cmake --build build -j && (cd build && ctest -j8)
for t in build-asan build-tsan build-none; do cmake --build $t -j && (cd $t && ctest -j8); done
http_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl
```
Expected: warning-free; `build` 22/22, `build-asan` 22/22, `build-tsan` 22/22, `build-none` 20/20; conformance `SUCCESS`. (If a tree's cache still names `INNERTUBE_GLAZE_DIR`, it is ignored; `cmake -S . -B <tree> -U INNERTUBE_GLAZE_DIR` drops it.)

- [ ] **Step 5: The differential dump — parsers' output unchanged**

```bash
g++ -std=c++23 -O1 -Isrc -Iinclude /tmp/jd/dump.cpp build/libinnertube.a -lcurl -o /tmp/jd/dump_new
/tmp/jd/dump_new /tmp/jd/corpus/* tests/fixtures/* > /tmp/jd/new.txt
diff /tmp/jd/old.txt /tmp/jd/new.txt && echo identical
```
Expected: `identical` (the session's run: 2 364 lines, no difference). A difference is a regression unless it is the documented `createCommentParams` change — the live corpus has none.

- [ ] **Step 6: Live**

```bash
./build/examples/yt-cli search "rick astley" | head -3
./build/examples/yt-cli streams https://youtu.be/dQw4w9WgXcQ | head -3
./build/examples/yt-cli channel @NASA | head -3
./build/examples/yt-cli comments dQw4w9WgXcQ | head -2
./build/examples/yt-cli playlist PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI | head -2
./build/examples/yt-watch dQw4w9WgXcQ | head -4
./build/examples/yt-suggest lofi | head -3
./build/examples/yt-cli captions dQw4w9WgXcQ | head -3
```
Expected: results in every command (the session got search hits, VISIONOS formats, the NASA header and videos, comments, the playlist header, the watch page with RYD votes, suggestions, caption cues).

- [ ] **Step 7: Measure what the spec promised**

```bash
size -t build-rel/CMakeFiles/innertube.dir/src/parsers/*.o build-rel/CMakeFiles/innertube.dir/src/json/*.o | tail -1
```
(with a Release tree `build-rel`, before and after this plan). The session measured parsers + json objects 2.66 MB → 0.47 MB, bodies + context 211 KB → 33 KB, stripped `yt-cli` text 2.88 MB → 1.43 MB, a `-j1` library build 157 s → 56 s, and the library's parse of the corpus 16.4 ms → 7.3 ms. Record the numbers in the spec's §9 ("library-level numbers after migration").

- [ ] **Step 8: Commit**

```bash
git add -A src tests examples CMakeLists.txt .gitignore .gitmodules AGENTS.md README.md docs/specs/2026-10-16-json-design.md
git commit -m "lib: drop Glaze — the JSON layer is yt::json alone" -m "Deletes json/shapes/ (132 structs), the scanner and its token visitor, Glaze's read
and write helpers and the third_party/glaze submodule; the build loses
INNERTUBE_GLAZE_DIR. yt-cli keeps its state as key=value lines in yt-cli-state.txt
(the old yt-cli-state.json is not read: sign in again once). AGENTS.md and the README
describe the new layer and the parser recipe." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

````diff
diff --git a/AGENTS.md b/AGENTS.md
index 9d0c07e..8cd7e6c 100644
--- a/AGENTS.md
+++ b/AGENTS.md
@@ -11,9 +11,10 @@ files are the reference style; copy them, not your habits.
 
 libinnertube (`innertube-cpp`) is a Qt-free C++ client for YouTube's private
 InnerTube API, split out of MeeTube (a YouTube client for the Nokia N9 / MeeGo
-Harmattan). It must stay fast and lean on a 1 GB-RAM ARM phone: the parsers are a
-single-pass scanner plus partial Glaze reads, not a DOM; a malloc per JSON node is a
-regression. Design history lives in `docs/specs/` and `docs/plans/`.
+Harmattan). It must stay fast and lean on a 1 GB-RAM ARM phone: every JSON reply is
+indexed once by `yt::json` (one validating pass, 4 bytes per token) and read lazily by
+path, not a DOM; a malloc per JSON node is a regression. Design history lives in
+`docs/specs/` and `docs/plans/`.
 
 ## 2. Build, test, definition of done
 
@@ -44,7 +45,7 @@ restore it), and the docs/comments that describe the touched contract are update
 ## 3. Layout and layering
 
 ```
-include/yt/        public API — C++17, std types only, no Glaze, no curl, no Qt
+include/yt/        public API — C++17, std types only, no curl, no Qt
 src/               the implementation — C++23, private
   client.cpp       yt::Client + Engine (boot, delivery, Job, future forms of more)
   groups.cpp       the resource groups of yt::Client (thin wrappers over core/chains)
@@ -55,9 +56,8 @@ src/               the implementation — C++23, private
   core/            request chains over the IHttp seam, JobToken, logging, pct-encoding
   net/             Loop (tasks + timers), HttpClient (transport policy), URL helpers
   innertube/       client identities, context/header builder, POST body builders, player-JS scans (playerjs)
-  json/            Glaze read options, single-pass scanner, token scan, json3 captions
-    shapes/        every JSON shape the library reads, one header per API family (§7)
-  parsers/         response → yt:: value types (Glaze partial views + scanner visitors)
+  json/            yt::json (index, views, find, writer), YouTube helpers (ytjson.h), json3
+  parsers/         response → yt:: value types (paths over json::Value)
 backends/curl/     the default HttpTransport (the only code that includes <curl/curl.h>)
 backends/none/     no-backend build
 conformance/       backend conformance suite + loopback HTTP server (no test framework)
@@ -88,9 +88,8 @@ new directory or file for one function.
 - Public headers compile as C++17 (`tst_types` builds at C++17 to prove it). No
   `std::expected`, `<format>`, `std::ranges`, designated initializers or other C++20+
   in `include/`.
-- Glaze is a PRIVATE, header-only dependency; it never appears in a public header.
-- No new third-party dependency. Use the standard library, Glaze or what is
-  already in `src/`.
+- No third-party dependency beyond libcurl (default backend) and doctest (tests). Use
+  the standard library or what is already in `src/` — JSON is `yt::json`.
 - Language standards are set through `CXX_STANDARD` target properties, never raw
   `-std=` flags (MSVC). New targets: `set_target_properties(x PROPERTIES CXX_STANDARD 23)`.
 
@@ -115,14 +114,14 @@ new directory or file for one function.
 - URLs are byte-exact: never decode or re-encode a server-issued URL.
 - Credentials (`Authorization`, `Cookie`, `Set-Cookie`, `Proxy-Authorization`,
   refresh tokens) never reach a log. Files holding tokens are written owner-only.
-- Recursive JSON walks are bounded by `gj::kScanMaxDepth`, and the bound is absolute
-  across re-scans (see `AccountItemCollector`).
+- A JSON document nested deeper than `json::kMaxDepth` is refused whole, and every walk
+  over one (`Value::find`) is iterative: no recursion over untrusted input.
 
 **Error model**
 - No exceptions in the library, except `Client`'s constructor (no transport).
   Asynchronous outcomes are `Result<T>` with an `ErrorCode`; keep `httpStatus`.
 - Parsers never fail: malformed or unexpected JSON degrades to "fields absent" /
-  an empty list (`gj::readJson` ignores errors on purpose). Transport and envelope
+  an empty list (an absent `json::Value` reads as ""/0/false). Transport and envelope
   errors are decided once, in `HttpClient`, never in a parser.
 - A non-2xx reply, a truncated body or an `"error"` envelope is a failed `Reply`,
   never cached and never parsed as an empty success.
@@ -159,10 +158,9 @@ void HttpClient::onTimeout(const std::shared_ptr<Exchange> &ex)
 | types, structs, enums | `PascalCase` | `HttpClient`, `TokenGrant`, `ErrorCode` |
 | functions, methods, locals, struct fields | `camelCase` | `fetchPlayer`, `bearerExpiry` |
 | private members of classes | `m_` + camelCase | `m_loop`, `m_inflightByKey` |
-| constants | `k` + PascalCase | `kMaxCacheEntries`, `kScanMaxDepth` |
+| constants | `k` + PascalCase | `kMaxCacheEntries`, `kMaxDepth` |
 | enumerators | `PascalCase`, `enum class` | `ErrorCode::InvalidResponse` |
 | macros (rare) | `UPPER_CASE` | `YTLOG` |
-| Glaze shapes | field name == JSON key, even snake_case | `access_token`, `simpleText` |
 | test files | `tst_<area>.cpp` | `tst_http.cpp` |
 
 **Idioms in use — reuse them, don't invent alternatives**
@@ -175,10 +173,11 @@ void HttpClient::onTimeout(const std::shared_ptr<Exchange> &ex)
   only where the engine provably outlives the loop.
 - Numeric conversions use a C-style cast with a space: `(int) kind`, `(size_t)(e - p)`.
   `nullptr` for pointers in new code.
-- JSON shapes live only in `src/json/shapes/`, never in a parser or chain `.cpp`.
-- Optional JSON fields are `std::optional<…>`; flexible numbers are `gj::FlexInt` →
-  `gj::toInt64`; text is `gj::Text` → `gj::textOf`. Read subtrees with `gj::readJson`,
-  whole `std::string` bodies with `gj::readJsonDoc`.
+- JSON is read from a `Reply`'s `json()` (the transport indexed it once) by path:
+  `r.path("metadata", "lockupMetadataViewModel", "title", "content").str()` — absence
+  propagates, so a path needs no checks between steps. Text is `gj::textOf`, thumbnails
+  `gj::lastThumbUrl`/`lastSourceUrl`, numbers-or-strings `gj::flexInt`, "anywhere below"
+  `gj::first` or `Value::find`. JSON is written with `json::Writer`.
 - Diagnostics only through `YTLOG(log, "category") << a << b;` (zero cost when off).
 - State that changes identity (sign-in/out, locale) goes through `IHttp::clearCache()`
   and the generation counters (`authGen`, `bearerGen`, `m_gen`); never mutate
@@ -221,30 +220,14 @@ Comment density is moderate and purposeful. Match it.
    parsed result, error mapping); add the call to "every group method reaches its chain"
    in `tst_client.cpp`.
 
-**New parser / JSON shape**: namespace-scope aggregates of `std::optional` fields
-named after the JSON keys (partial views; unknown keys skipped); a scanner visitor
-(`what/capture/enter/leave`) when the location is not fixed. Fixture-based test in
-`tst_parsers.cpp` from a real, trimmed payload under `tests/fixtures/`.
-
-Shapes go into the `src/json/shapes/` header of their API family; the namespace says
-which family a shape belongs to. Reuse a `gj` piece before declaring a new one (a
-`{thumbnails:[{url}]}` is `gj::ThumbSet`, a `{runs|simpleText}` is `gj::Text`).
-
-| Header | Namespace | Holds |
-|---|---|---|
-| `common.h` | `gj` | text runs, thumbnails, view-model sources, `{content}`, continuation tokens, the error envelope |
-| `renderers.h` | `rj` | list items (video/lockup/tile/playlist/channel renderers) + shared navigation, image, subscribe pieces |
-| `channel.h` | `rj` | channel headers and metadata |
-| `comments.h` | `rj` | `commentEntityPayload` |
-| `watch.h` | `rj` | `/next` primary info, like/dislike toggles, owner, like-status entity |
-| `account.h` | `rj` | `accounts_list` items, the `mdx/handoff` sign-in QR |
-| `playlist.h` | `rj` | the playlist page (`VL<id>` browse) sidebar: title, stats, owner |
-| `player.h` | `pj` | the `/player` answer |
-| `timedtext.h` | `tj` | json3 captions |
-| `oauth.h`, `ryd.h` | `oj` | services outside InnerTube: Google OAuth, Return YouTube Dislike |
-
-Request bodies are written, not read: their shapes stay beside their builders
-(`innertube/bodies.cpp`, `innertube/contextbuilder.cpp`).
+**New parser**: a function from a `json::Value` (the response root, or one renderer) to a
+`yt::` value type, the paths written out where they are read (`parsers/rendererinternal.h`
+holds the converters shared by several lists). Renderers whose location is not fixed are
+found with `Value::find` (`Visit::SkipRest` for a leaf renderer, the walkers' old early
+`return`). Reuse a `gj` helper before writing a new one (`textOf`, `lastThumbUrl`,
+`flexInt`, `first`). Fixture-based test in `tst_parsers.cpp` from a real, trimmed payload
+under `tests/fixtures/`; fixtures must be well-formed JSON (`json::Doc` refuses anything
+else, like the transport).
 
 **Transport / backend change**: the policy goes into `net::HttpClient`, never into a
 backend. A new `TransportConfig` hint must be honoured by both curl backends
@@ -292,7 +275,7 @@ TEST_CASE("non2xxWithoutErrorMemberFailsAndIsNotCached") {
 ## 10. Commits
 
 Commit only when the human asks. One logical change per commit; never commit build
-trees, `compile_commands.json` or `yt-cli-state.json`.
+trees, `compile_commands.json` or `yt-cli-state.txt`.
 
 ```
 <scope>: <what, lowercase, no trailing period>
diff --git a/CMakeLists.txt b/CMakeLists.txt
index b690340..cb234c2 100644
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -12,8 +12,6 @@ endif()
 option(INNERTUBE_BUILD_TESTS "Build the innertube test suites" ${_it_top})
 option(INNERTUBE_BUILD_EXAMPLES "Build the examples (and their conformance tests)" ${_it_top})
 option(INNERTUBE_INSTALL "Generate install rules (find_package config + pkg-config file)" ${_it_top})
-set(INNERTUBE_GLAZE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glaze" CACHE PATH
-    "Glaze source root (contains include/glaze)")
 
 add_library(innertube STATIC
     src/version.cpp
@@ -45,21 +43,13 @@ add_library(innertube STATIC
     src/groups.cpp
     src/auth.cpp)
 add_library(innertube::innertube ALIAS innertube)
-# The impl needs C++23 (Glaze v7); consumers only see the C++17 floor (the PUBLIC feature).
+# The impl is C++23; consumers only see the C++17 floor (the PUBLIC feature).
 set_target_properties(innertube PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 23)
 target_compile_features(innertube PUBLIC cxx_std_17)
 target_include_directories(innertube
     PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
 
-# Glaze: PRIVATE, header-only. Reuse a consumer's `glaze` target if present (build-only:
-# an installed innertube needs nothing of it).
-if(TARGET glaze)
-    target_link_libraries(innertube PRIVATE $<BUILD_INTERFACE:glaze>)
-else()
-    target_include_directories(innertube SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
-endif()
-
 # HTTP backend (include/yt/transport.h, docs/backends.md):
 #   curl    backends/curl — libcurl multi on the engine thread (default). A consumer
 #           may pre-define CURL::libcurl (MeeTube's N9 build points it at the static
diff --git a/README.md b/README.md
index 096533b..1a4517e 100644
--- a/README.md
+++ b/README.md
@@ -5,8 +5,8 @@ private API the YouTube apps and website use.
 
 The library was extracted from [MeeTube](https://github.com/IarChep/MeeTube), a YouTube client
 for the Nokia N9 (MeeGo Harmattan), and is built to run well on that phone: 1 GB of
-RAM, an ARM CPU, no DOM. Responses are parsed in a single pass with
-[Glaze](https://github.com/stephenberry/glaze), and nothing ties it to the N9. The
+RAM, an ARM CPU, no DOM. Every response is indexed in one validating pass (4 bytes per
+token, no copies) and read lazily by path, and nothing ties it to the N9. The
 public API is plain C++17 with `std` types only, so it fits any desktop, mobile or
 embedded C++ project.
 
@@ -50,7 +50,7 @@ embedded C++ project.
 ## Building
 
 You need CMake ≥ 3.20, a C++23 compiler for the library itself (recent GCC or Clang),
-and libcurl ≥ 7.68 for the default backend. Glaze and doctest are git submodules.
+and libcurl ≥ 7.68 for the default backend. doctest is a git submodule.
 
 ```sh
 git clone --recursive https://github.com/IarChep/innertube-cpp.git
@@ -67,7 +67,6 @@ ctest --test-dir build
 | `INNERTUBE_HTTP_BACKEND_LIBS` | — | extra link libraries for a custom backend |
 | `INNERTUBE_BUILD_TESTS` | top-level only | the doctest suites |
 | `INNERTUBE_BUILD_EXAMPLES` | top-level only | `yt-cli`, `yt-watch`, `yt-channel`, `yt-suggest` |
-| `INNERTUBE_GLAZE_DIR` | `third_party/glaze` | use another Glaze checkout (a `glaze` target is reused if present) |
 | `INNERTUBE_INSTALL` | top-level only | install rules: headers, `libinnertube.a`, the CMake package and `innertube.pc` |
 
 ## Using it in your project
@@ -83,7 +82,7 @@ target_link_libraries(myapp PRIVATE innertube::innertube)
 ```
 
 A `git submodule` + `add_subdirectory(innertube-cpp)` setup works the same way. Your
-code only needs C++17, because Glaze and libcurl stay private to the library.
+code only needs C++17, because libcurl stays private to the library.
 
 Or install it and find it like any other package:
 
diff --git a/examples/CMakeLists.txt b/examples/CMakeLists.txt
index 0c452c2..cd586e1 100644
--- a/examples/CMakeLists.txt
+++ b/examples/CMakeLists.txt
@@ -18,11 +18,6 @@ endif()
 add_executable(yt-cli yt-cli/main.cpp)
 target_link_libraries(yt-cli PRIVATE innertube)
 set_target_properties(yt-cli PROPERTIES CXX_STANDARD 23)
-if(TARGET glaze)
-    target_link_libraries(yt-cli PRIVATE glaze)
-else()
-    target_include_directories(yt-cli SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
-endif()
 
 # Single-file public-API examples (live network; not ctests). All drive the Client
 # from main() with ownThread=false + runOnce().
diff --git a/examples/yt-cli/main.cpp b/examples/yt-cli/main.cpp
index ae30e8f..7c8f327 100644
--- a/examples/yt-cli/main.cpp
+++ b/examples/yt-cli/main.cpp
@@ -10,9 +10,8 @@
 //   yt-cli playlist <link>         e.g. PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI
 //   yt-cli comments <link>
 //   yt-cli raw <endpoint> [json]   e.g. raw browse '{"browseId":"FEwhat_to_watch"}' (unstable)
-//   yt-cli signin                  device-code login; state saved to ./yt-cli-state.json
+//   yt-cli signin                  device-code login; state saved to ./yt-cli-state.txt
 #include <yt/yt.h>
-#include <glaze/glaze.hpp>
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <unistd.h>
@@ -24,19 +23,23 @@
 
 namespace {
 
-struct SavedState { std::string hl = "en", gl = "US", visitorData, refreshToken, playerId; int signatureTimestamp = 0; };
-const char *kStateFile = "yt-cli-state.json";
+// One "key=value" per line: the values (tokens, ids, hl/gl) never contain a newline.
+const char *kStateFile = "yt-cli-state.txt";
 
 yt::SessionState load()
 {
     yt::SessionState s;
     std::ifstream f(kStateFile);
-    if (!f) return s;
-    std::stringstream ss; ss << f.rdbuf();
-    SavedState saved;
-    if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(saved, ss.str())) {
-        s.hl = saved.hl; s.gl = saved.gl; s.visitorData = saved.visitorData; s.refreshToken = saved.refreshToken;
-        s.playerId = saved.playerId; s.signatureTimestamp = saved.signatureTimestamp;
+    for (std::string line; std::getline(f, line);) {
+        const size_t eq = line.find('=');
+        if (eq == std::string::npos) continue;
+        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
+        if (key == "hl") s.hl = value;
+        else if (key == "gl") s.gl = value;
+        else if (key == "visitorData") s.visitorData = value;
+        else if (key == "refreshToken") s.refreshToken = value;
+        else if (key == "playerId") s.playerId = value;
+        else if (key == "signatureTimestamp") s.signatureTimestamp = std::atoi(value.c_str());
     }
     return s;
 }
@@ -45,13 +48,14 @@ yt::SessionState load()
 // renamed so a crash never leaves a half-written state behind.
 void save(const yt::SessionState &s)
 {
-    SavedState out{s.hl, s.gl, s.visitorData, s.refreshToken, s.playerId, s.signatureTimestamp};
-    const std::string json = glz::write_json(out).value_or("{}");
+    const std::string text = "hl=" + s.hl + "\ngl=" + s.gl + "\nvisitorData=" + s.visitorData + "\nrefreshToken="
+                           + s.refreshToken + "\nplayerId=" + s.playerId + "\nsignatureTimestamp="
+                           + std::to_string(s.signatureTimestamp) + "\n";
     const std::string tmp = std::string(kStateFile) + ".tmp";
     const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
     if (fd < 0) return;
     ::fchmod(fd, 0600);                              // also when the file already existed
-    const bool ok = ::write(fd, json.data(), json.size()) == (ssize_t) json.size();
+    const bool ok = ::write(fd, text.data(), text.size()) == (ssize_t) text.size();
     ::close(fd);
     if (ok) std::rename(tmp.c_str(), kStateFile); else std::remove(tmp.c_str());
 }
diff --git a/src/json/ytjson.h b/src/json/ytjson.h
index ee1f4e3..a1dd54e 100644
--- a/src/json/ytjson.h
+++ b/src/json/ytjson.h
@@ -1,67 +1,20 @@
 #ifndef YT_YTJSON_H
 #define YT_YTJSON_H
-// Qt-free Glaze layer: the read options, flexible numbers, partial reads and base64 —
-// the machinery every parser shares. The JSON shapes themselves live in
-// json/shapes/ (one header per API family). Pure std types only.
+// The YouTube layer over yt::json: the pieces every InnerTube answer is built from (text
+// runs, thumbnail and view-model image sets, counts that are numbers or strings) and the
+// base64 the parsers and the chains share. Pure std types; the shapes themselves are
+// written out as paths where they are read (parsers/).
 //
 #include "json/json.h"
-#include <glaze/json.hpp>
 
 #include <charconv>
 #include <cstdint>
-#include <optional>
 #include <string>
 #include <string_view>
-#include <variant>
-#include <vector>
 
 namespace yt {
 namespace gj {
 
-// Every InnerTube payload is read with these options:
-//  - error_on_unknown_keys=false: our structs are deliberate partial views of
-//    huge responses — unknown keys are structurally skipped (the fast path).
-//  - null_terminated=false: parsers take arbitrary string_views (including
-//    subtree extents that end mid-buffer), so no sentinel byte is assumed.
-inline constexpr glz::opts kIn{.null_terminated = false, .error_on_unknown_keys = false};
-
-// ---- flexible integers ------------------------------------------------------
-// InnerTube counts arrive as JSON numbers (itag, width) or quoted strings
-// (videoDetails.viewCount). Mirrors the old jint(): int, float (truncated),
-// or a strictly-numeric string; anything else — including a partial numeric
-// prefix like "12a" — is 0.
-using FlexInt = std::variant<std::int64_t, double, std::string>;
-inline std::int64_t toInt64(const FlexInt &v)
-{
-    if (const std::int64_t *i = std::get_if<std::int64_t>(&v)) return *i;
-    if (const double *d = std::get_if<double>(&v)) return (std::int64_t)*d;
-    if (const std::string *s = std::get_if<std::string>(&v)) {
-        std::int64_t n = 0;
-        const char *b = s->data(), *e = s->data() + s->size();
-        auto res = std::from_chars(b, e, n, 10);
-        if (res.ec == std::errc() && res.ptr == e && b != e) return n;
-    }
-    return 0;
-}
-inline std::int64_t toInt64(const std::optional<FlexInt> &v) { return v ? toInt64(*v) : 0; }
-
-// Typed partial read of a subtree extent. Errors degrade to "fields absent":
-// the struct keeps whatever was parsed before the error point and the caller
-// carries on — a malformed renderer never aborts a listing (matches the old
-// per-field contains() tolerance).
-template <class T>
-inline void readJson(T &out, std::string_view sv)
-{
-    (void)glz::read<kIn>(out, sv);
-}
-
-// Whole-document variant: Reply.body is a std::string, whose data() is
-// guaranteed NUL-terminated — Glaze's sentinel scanning skips per-byte
-// end-pointer checks. Subtree extents MUST keep kIn (they end mid-buffer).
-inline constexpr glz::opts kInDoc{.null_terminated = true, .error_on_unknown_keys = false};
-template <class T>
-inline void readJsonDoc(T &out, const std::string &doc) { (void)glz::read<kInDoc>(out, doc); }
-
 // ---- base64 (xtags, data: URLs) ---------------------------------------------
 // YouTube fields come in both alphabets and usually without padding (xtags,
 // videoPlaybackUstreamerConfig are base64url; a QR data: URL is classic). Here, in
diff --git a/tests/CMakeLists.txt b/tests/CMakeLists.txt
index 67bc438..766d205 100644
--- a/tests/CMakeLists.txt
+++ b/tests/CMakeLists.txt
@@ -14,11 +14,6 @@ function(it_test name)
     target_link_libraries(${name} PRIVATE innertube innertube_doctest)
     set_target_properties(${name} PROPERTIES CXX_STANDARD ${_std})
     target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
-    if(TARGET glaze)
-        target_link_libraries(${name} PRIVATE glaze)
-    else()
-        target_include_directories(${name} SYSTEM PRIVATE ${INNERTUBE_GLAZE_DIR}/include)
-    endif()
     target_compile_definitions(${name} PRIVATE IT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
     add_test(NAME ${name} COMMAND ${name})
 endfunction()
````
