# Contributing to libinnertube — guide for coding agents

This file is for automated contributors (Claude Code, Codex, Gemini, Cursor, Aider, …)
and for humans who want their changes to look like the rest of the tree. Code written
here should be indistinguishable from the existing code: same layering, same idioms,
same comment voice, same tests, same commit messages. When this file and the code
disagree, the code in `src/net/`, `src/core/` and `src/client.cpp` wins — those
files are the reference style; copy them, not your habits.

## 1. What this is

libinnertube (`innertube-cpp`) is a Qt-free C++ client for YouTube's private
InnerTube API, split out of MeeTube (a YouTube client for the Nokia N9 / MeeGo
Harmattan). It must stay fast and lean on a 1 GB-RAM ARM phone: the parsers are a
single-pass scanner plus partial Glaze reads, not a DOM; a malloc per JSON node is a
regression. Design history lives in `docs/specs/` and `docs/plans/`.

## 2. Build, test, definition of done

```sh
cmake -S . -B build -G Ninja && cmake --build build -j && (cd build && ctest -j8)
```

Configured trees that exist locally and must stay green for library changes:

| Tree | Configure | Catches |
|---|---|---|
| `build` | default (curl backend) | everything |
| `build-asan` | `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"` | lifetime bugs |
| `build-tsan` | `-DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"` | engine/backend races |
| `build-none` | `-DINNERTUBE_HTTP_BACKEND=none` | code that assumes a backend |

Also run the transport suites with a dead proxy in the environment
(`http_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl`): loopback tests
must never depend on proxy settings.

The examples (`yt-cli`, `yt-watch`, `yt-channel`, `yt-suggest`) hit the live network
and are not ctests; run one by hand when you touch request/parse code.

A change is done when: it builds warning-free in all four trees, every ctest passes,
there is a test that **fails without the change** (revert your fix, watch it fail,
restore it), and the docs/comments that describe the touched contract are updated.

## 3. Layout and layering

```
include/yt/        public API — C++17, std types only, no Glaze, no curl, no Qt
src/               the implementation — C++23, private
  client.cpp       yt::Client facade + Engine (dispatch, delivery, Job)
  auth.cpp         OAuth device flow + bearer lifecycle (Engine methods)
  engine.h         Engine / JobHandle / Gate — the threading contract lives here
  core/            request chains over the IHttp seam, JobToken, logging, pct-encoding
  net/             Loop (tasks + timers), HttpClient (transport policy), URL helpers
  innertube/       client identities, context/header builder, POST body builders
  json/            Glaze read options + shared shapes, single-pass scanner, token scan
  parsers/         response → yt:: value types (Glaze partial views + scanner visitors)
backends/curl/     the default HttpTransport (the only code that includes <curl/curl.h>)
backends/none/     no-backend build
conformance/       backend conformance suite + loopback HTTP server (no test framework)
tests/             doctest suites, fakes, fixtures
examples/          consumers of the public API only
docs/              backends.md (backend author guide), specs/, plans/
```

Dependency direction (never the reverse): `parsers → json`, `core/chains → parsers,
innertube, json`, `net → json, innertube`, `client/auth → core, net`. Public headers
include only other public headers and the standard library.

Includes: project headers by path from `src/` (`"core/chains.h"`, `"json/ytjson.h"`),
public ones with angle brackets (`<yt/client.h>`). Order: the file's own header first,
then project headers alphabetically, then `<std>` headers alphabetically. A file's
sibling may be included by bare name (`"bodies.h"`, `"textmatch.h"`) — the older
parser files do; new code prefers the full path.

Put code where its peers are. A new JSON helper goes to `src/json/`, a new request to
`core/chains.cpp`, a new transport policy to `net/httpclient.cpp`. Do not create a
new directory or file for one function.

## 4. Invariants — never break these

**Portability and dependencies**
- No Qt anywhere in `include/`, `src/`, `backends/` (enforced by the `innertube_no_qt`
  ctest: no `#include <Q…>`, `Q_OBJECT`, `QString`, `qint64`, `qreal`).
- Public headers compile as C++17 (`tst_types` builds at C++17 to prove it). No
  `std::expected`, `<format>`, `std::ranges`, designated initializers or other C++20+
  in `include/`.
- Glaze is a PRIVATE, header-only dependency; it never appears in a public header.
- No new third-party dependency. Use the standard library, Glaze or what is
  already in `src/`.
- Language standards are set through `CXX_STANDARD` target properties, never raw
  `-std=` flags (MSVC). New targets: `set_target_properties(x PROPERTIES CXX_STANDARD 23)`.

**Threading contract** (header of `include/yt/client.h`; read it before touching
`client.cpp`, `auth.cpp`, `net/`)
- Every public method only posts work (`loop.post`). Network I/O, parsing and all
  Engine/HttpClient state live on the engine thread.
- A callback runs **exactly once or never**, and **never from inside the call that
  started it**. Every delivery path, including failures and cache hits, goes through
  `m_loop.post(...)`, never a direct call from `post()`/`get()`.
- Delivery is gated on the delivery thread (`Engine::deliver`, `Gate`); after
  `~Client` returns nothing runs. Anything that can outlive its owner holds a
  `weak_ptr` (`m_life`, `std::weak_ptr<Exchange>`) and checks it first.
- Multi-step chains check `live(job)` between steps and stop silently when canceled.
- Backend sink calls may arrive on any thread; the library marshals them to the loop
  and drops late or contract-breaking ones. A backend must never be trusted to behave.

**Security policy** (owned by `net::HttpClient`, so no backend can weaken it)
- Only `http`/`https`, also on every redirect hop. Only credential-free GETs follow
  redirects. URLs and headers with C0/DEL characters are refused before `start()`.
- Server-issued values (visitorData, tokens, Location) are vetted before reuse.
- URLs are byte-exact: never decode or re-encode a server-issued URL.
- Credentials (`Authorization`, `Cookie`, `Set-Cookie`, `Proxy-Authorization`,
  refresh tokens) never reach a log. Files holding tokens are written owner-only.
- Recursive JSON walks are bounded by `gj::kScanMaxDepth`, and the bound is absolute
  across re-scans (see `AccountItemCollector`).

**Error model**
- No exceptions in the library, except `Client`'s constructor (no transport).
  Asynchronous outcomes are `Result<T>` with an `ErrorCode`; keep `httpStatus`.
- Parsers never fail: malformed or unexpected JSON degrades to "fields absent" /
  an empty list (`gj::readJson` ignores errors on purpose). Transport and envelope
  errors are decided once, in `HttpClient`, never in a parser.
- A non-2xx reply, a truncated body or an `"error"` envelope is a failed `Reply`,
  never cached and never parsed as an empty success.

## 5. Code style

There is no `.clang-format`; match the surrounding code by hand.

**Layout**
- 4 spaces, no tabs. Lines usually ≤ 100 columns, 120 at most.
- Function definitions: opening brace on its own line. Short in-class or one-line
  helpers may stay on one line. Control flow keeps `{` on the same line; a
  single-statement `if` or `for` may go on one line without braces.
- `&` and `*` bind to the name: `const Reply &r`, `const char *p`.
- Namespaces are not indented. New code uses nested form `namespace yt::net {`;
  internals sit in an unnamed `namespace {` at the top of the `.cpp`. A closing brace
  may carry `// namespace detail` when the block is long.
- Header guards: `YT_<AREA>_<NAME>_H` (`YT_NET_URL_H`, `YT_CORE_LOG_H`); test-only
  headers use `IT_` (`IT_FAKEHTTP_H`). No `#pragma once`.

```cpp
void HttpClient::onTimeout(const std::shared_ptr<Exchange> &ex)
{
    if (ex->done) return;
    ex->timer = 0;
    for (const Waiter &w : expired) if (core::live(w.job)) w.fn(r);
}
```

**Naming**

| Kind | Style | Example |
|---|---|---|
| types, structs, enums | `PascalCase` | `HttpClient`, `TokenGrant`, `ErrorCode` |
| functions, methods, locals, struct fields | `camelCase` | `fetchPlayer`, `bearerExpiry` |
| private members of classes | `m_` + camelCase | `m_loop`, `m_inflightByKey` |
| constants | `k` + PascalCase | `kMaxCacheEntries`, `kScanMaxDepth` |
| enumerators | `PascalCase`, `enum class` | `ErrorCode::InvalidResponse` |
| macros (rare) | `UPPER_CASE` | `YTLOG` |
| Glaze shapes | field name == JSON key, even snake_case | `access_token`, `simpleText` |
| test files | `tst_<area>.cpp` | `tst_http.cpp` |

**Idioms in use — reuse them, don't invent alternatives**
- Plain aggregates with default member initializers for data (`struct Stream { … int
  width = 0; }`); classes only for things with invariants. Concrete classes are `final`
  and delete copy explicitly.
- Callbacks are `std::function`; public ones use `Callback<T>` = `void(const Result<T> &)`.
- Ownership: `std::unique_ptr` for owned parts, `std::shared_ptr` + `weak_ptr` for
  objects referenced by posted closures. Engine-thread code captures raw `Engine *`
  only where the engine provably outlives the loop.
- Numeric conversions use a C-style cast with a space: `(int) kind`, `(size_t)(e - p)`.
  `nullptr` for pointers in new code.
- Optional JSON fields are `std::optional<…>`; flexible numbers are `gj::FlexInt` →
  `gj::toInt64`; text is `gj::Text` → `gj::textOf`. Read subtrees with `gj::readJson`,
  whole `std::string` bodies with `gj::readJsonDoc`.
- Diagnostics only through `YTLOG(log, "category") << a << b;` (zero cost when off).
- State that changes identity (sign-in/out, locale) goes through `IHttp::clearCache()`
  and the generation counters (`authGen`, `bearerGen`, `m_gen`); never mutate
  `Session` without invalidating the memoized context/headers.
- Small, explicit code over abstraction: no interface with one implementation, no
  factory for one product, no options for values that never change.

## 6. Comments

Comment density is moderate and purposeful. Match it.

- **Every file starts with a header comment**: what the file is, its place in the
  layering, and the constraint that shaped it. Public headers end that comment with
  `C++17.` (e.g. `// Result<T> / Error: every asynchronous operation reports one of these. C++17.`).
- Comments explain *why* and *contract*, not *what*: invariants, thread affinity,
  ordering, the bug a line prevents. `// Local copy: a callback may start or abort
  other requests.` is the voice; `// loop over waiters` is not.
- Declarations in headers are the API documentation: put the contract right on the
  declaration (thread, ownership, when the callback runs, what empty means).
- Trailing comments are aligned in a column for field and parameter lists.
- Cite sources when behaviour follows one: `// RFC 8628 §3.5`, `spec §3.2`,
  "Inherited from MeeTube's src/core/…".
- Never leave commented-out code, `TODO` or `FIXME`; the tree has none. Deferred
  work goes into `docs/plans/`.
- When code exists to defeat a specific failure, say so plainly in the comment
  above it, e.g. "a crafted response overflowed the stack".

## 7. Recipes

**New Client operation**
1. Chain in `src/core/chains.cpp` (+ declaration with its contract in `chains.h`):
   build the body in `innertube/bodies.*`, post via `IHttp`, parse via `parsers/`,
   map `!r.ok` with `errorOf(r)`.
2. Public method in `include/yt/client.h` and a thin `m_engine->start<…>(…)` wrapper
   in `src/client.cpp`, the same shape as its neighbours.
3. Tests: chain behaviour in `tst_chains.cpp` (FakeHttp: sent body, client routing,
   parsed result, error mapping); add the call to "every endpoint reaches its chain"
   in `tst_client.cpp`.

**New parser / JSON shape**: namespace-scope aggregates of `std::optional` fields
named after the JSON keys (partial views; unknown keys skipped); a scanner visitor
(`what/capture/enter/leave`) when the location is not fixed. Fixture-based test in
`tst_parsers.cpp` from a real, trimmed payload under `tests/fixtures/`.

**Transport / backend change**: the policy goes into `net::HttpClient`, never into a
backend. A new `TransportConfig` hint must be honoured by both curl backends
(`backends/curl`, `examples/threaded-transport`), documented in `docs/backends.md`
and, if backends must obey it, checked by `conformance/`.

**New source file**: add it explicitly to the `add_library(innertube …)` list in
`CMakeLists.txt` (no globbing). New suite: `it_test(tst_<area>)` in
`tests/CMakeLists.txt` (`STD17` for public-header checks).

## 8. Tests

- doctest, one suite per area. Fakes by layer: `FakeHttp` (`tests/fakehttp.h`) for
  chains, Client and Auth; `FakeTransport` (`tests/faketransport.h`) for
  `HttpClient`; `conformance::Loopback` for real backends (guard with
  `REQUIRE_BACKEND()`); `yt::testing::makeClientForTesting` + `setClock` for time.
- Drive time and the loop explicitly (`pump()`, `runOnce(0)`, `flushOne(i)`, a fake
  clock). Real sleeps only for timeout behaviour, with wide margins.
- Test names: short camelCase for unit behaviour (`coalesces`, `bodyCap`) or a
  sentence for scenarios (`"a refresh completing after signOut does not reinstall the
  old bearer"`).
- Every bug fix ships a regression test with a comment that names the old failure:

```cpp
// A non-2xx JSON answer without an "error" member used to pass as ok (and be cached).
TEST_CASE("non2xxWithoutErrorMemberFailsAndIsNotCached") {
```

- Use `REQUIRE` for preconditions whose failure would make later checks meaningless,
  and `CHECK` for everything else. Assert observable behaviour (what was sent,
  delivered, cached, canceled), not private state. A tiny test-only accessor on an
  internal class (`Loop::timerCount()`) is acceptable when nothing else can observe
  the behaviour.
- Extend the fakes instead of special-casing production code for tests.

## 9. Docs

- `docs/specs/YYYY-MM-DD-<topic>-design.md` for designs and
  `docs/plans/YYYY-MM-DD-<topic>.md` for task plans; when a change moves something they
  name, update the reference.
- `docs/backends.md` whenever the `HttpTransport`/`TransportConfig` contract or the
  conformance checks change.
- Code and docs are written in English.

## 10. Commits

Commit only when the human asks. One logical change per commit; never commit build
trees, `compile_commands.json` or `yt-cli-state.json`.

```
<scope>: <what, lowercase, no trailing period>
<blank line>
<body>
<blank line>
<trailer>
```

| Prefix | Use |
|---|---|
| `lib: …` | a new capability in the library (`lib: catalog + runtime client-version overrides`) |
| `fix(lib): …` | a library bug fix (`fix(lib): bound accounts_list item re-scan depth (stack exhaustion)`) |
| `fix(examples): …` | a fix in `examples/` |
| `test: …` | test-only changes |
| `docs: …` | documentation only |

For a fix, the body says what broke, its concrete consequence and how it is fixed.
Use one `-` bullet per independent fix, wrapped at ~85 columns. Mention the tool
that proved it (ASan, TSan) and where an inherited bug came from. Then add the
trailer your tool requires (this repo uses `Co-Authored-By:`).

```
fix(lib): fence identity changes — stale refreshes, partial grants, in-flight cache

- An access-token refresh started for one identity is ignored when it completes
  after a sign-out or a new sign-in (authGen); it used to reinstall the old
  account's bearer on a signed-out client.
```

## 11. Agent workflow checklist

1. Read the files you will touch and the contracts in their headers. Grep every
   caller before changing a function, and fix the shared function, not one caller.
2. Find the existing pattern for what you are adding (neighbouring endpoint,
   parser, test) and copy its shape.
3. Make the smallest change that fully solves the problem, with no drive-by
   refactors, renames or reformatting of untouched code.
4. Write the regression test, see it fail without the fix, then see it pass.
5. Build and test all four trees (section 2). Run an example against the live service
   if requests or parsing changed.
6. Update header comments, `docs/backends.md` and specs/plans that describe what you
   changed.
7. Report honestly: what changed, what was verified and how, what was not verified,
   and any behaviour change visible to consumers (public types, CMake minimums,
   error semantics).
