# JSON layer redesign: one validating index pass + path access (`yt::json`)

**Status:** design (2026-10-16), measured on a live corpus; owner decisions taken (§10); next: plan.
Supersedes `2026-10-15-json-tape-design.md` (started over from raw material on the owner's
request: no conclusions were taken from it).

**Decision this serves.** What replaces the scanner + Glaze-shapes reading in `src/json/` and
`src/parsers/`, judged on four measured axes — speed, memory, code size, cost of change — on
real InnerTube answers, on x86-64 and on 32-bit ARM (the N9 target).

---

## 1. Problem, in numbers (baseline)

The current reader (`json/jsonscan.h` visitor + `glz::read` into `json/shapes/` aggregates),
replayed on the oracle workload (§3), Release, x86-64 (Ryzen 5 5600U):

| Axis | Current | Note |
|---|---|---|
| Speed | 9.2 ms / 9.8 MB corpus (0.9 GB/s) | each leaf is scanned twice: skipped once by the visitor, then re-parsed by Glaze |
| Allocations | 9 532 per corpus pass | one per `std::string` / `std::vector` element in the shapes |
| Code size (same workload) | 310 KiB x86 · 294 KiB ARMv7 (-O2) | Glaze instantiations; the library's 6 parser TUs are 300–600 KiB of objects each |
| Compile time | 157 s for the library (-j1); parser TUs 12–21 s each | 17.1 s for the bench TU vs 3.4 s for the proposal |
| Cost of change | 741 lines of shapes, 132 structs, 277 `std::optional` fields | a 7-level path is 7 structs + a 7-deep `&&` chain |
| Memory | 0.8× the body at peak | the one axis where it is good |

Profile (perf, cycles): 26 % `skipValue` (byte-at-a-time), ~30 % `memchr` per string, ~20 %
Glaze unknown-key skipping inside leaves.

**Base rate.** A live `search`/`next` takes ~1.1 s cold (TLS + network) on this machine; the
current parse is ~0.8 ms of it (<0.1 %). On a Cortex-A8 the parse is estimated 15–25× slower
(not measured, §9), i.e. ~2 % of a request. Speed is therefore the *least* important of the four
axes; memory, binary size and cost of change carry the decision. The proposal wins all four.

## 2. What the corpus looks like (why the design is what it is)

21 live answers, 9.8 MB, captured with `Client::raw()` (WEB/IOS/ANDROID_VR/TVHTML5/VISIONOS):
search ×3 + continuation, browse (home, channel home/videos + continuation, playlist +
continuation, TV home), next ×2 + related continuation + comments page, player ×5, a json3 track.

| Measured | Value | Consequence |
|---|---|---|
| bytes per token | ~15 (minified, `prettyPrint=false`) | a per-token index costs ~0.33 B per input byte |
| bytes inside strings | 83–91 % (98–100 % on TV/home) | the inner loop is "find the closing quote": vectorize that, nothing else matters as much |
| escapes | 0–258 per document | decode lazily, never copy unescaped strings |
| bytes inside the extracted leaves | 89–98 % (browse), 43–77 % (search), 15 % (comments page) | raw-filtering (Sparser) cannot skip much: leaves *are* the document |
| fields read per leaf | ~10 of hundreds | lazy access wins over building values |

## 3. Method

* **Oracle.** `bench/oracle.py` (Python `json`, the ground truth; the whole stand is `bench/`, see its README) prints a canonical dump of a
  fixed extraction per document: the envelope (`error.message`, `responseContext.visitorData`);
  for lists every `lockupViewModel` / `videoRenderer` family / `tileRenderer` /
  `commentEntityPayload` in document order (8 fields each, including deep paths, "last of"
  arrays, text runs) plus the first `continuationCommand.token`; for `/player` status, details,
  every format (10 fields), caption tracks, storyboard spec; for json3 every event. Every
  candidate must print the oracle's dump **byte for byte** on all 21 documents or it is not
  measured. All candidates passed.
* **Harness.** One TU per candidate, same harness: malloc/free wrapped (count, live, peak),
  `perf_event_open` cycles/instructions (user space), per-document median of ≥15 runs and ≥150 ms.
* **Tools.** perf 7.2 (profiles), bloaty + `size -A` (code size, stripped, `-ffunction-sections
  -Wl,--gc-sections`, delta against an empty `extract()`), libFuzzer + ASan/UBSan, JSONTestSuite,
  Bootlin armv7-eabihf GCC 15.3 (`-mcpu=cortex-a8 -mfpu=neon -mfloat-abi=hard`) + qemu-arm 11.1.
* **Candidates.** current (scanner + Glaze), yyjson (copying and in-situ), simdjson DOM and
  On-Demand (haswell and the fallback kernel 32-bit ARM gets), sonic-cpp, RapidJSON, and the
  proposal (`json.h`) in SSE2, NEON and SWAR builds.

## 4. Research: what the literature and the fastest libraries say

| Source | Idea | Fit for InnerTube |
|---|---|---|
| Langdale & Lemire, *Parsing Gigabytes of JSON per Second*, VLDB J. 2019 | stage 1: SIMD bitmaps of quotes/structurals, in-string mask by carry-less multiply; stage 2: tape | stage 1 alone measured 6.45 GB/s (AVX2), 4.98 (SSE4.2), **1.45 (fallback)**. At ~15 B/token stage 2 dominates; simdjson's fast kernels need 64-bit (`portability.h`: "Our fast kernels require 64-bit systems") — the N9 gets the fallback |
| Keiser & Lemire, *On-Demand JSON*, SP&E 2024 | forward-only lazy iteration over the stage-1 index | 4.4 ms haswell / 8.8 ms fallback on our workload; forward-only order makes extraction code harder (fields must be visited in document order or re-found) |
| Li et al., *Mison*, PVLDB 10(10) 2017; Jiang, Qiu, Zhao, *Pison*, PVLDB 14(4) 2021 | structural (colon/comma) bitmaps per level, jump to a field without tokenizing | built for many records × few fields; our records are one bulky document read once — the index must be cheap to build, not cheap to query a million times |
| Palkar et al., *Sparser*, PVLDB 11(11) 2018 | raw filtering before parsing | exact for `"key":` on valid JSON, but leaves cover 89–98 % of browse bytes and raw filtering cannot validate — rejected |
| Jiang & Zhao, *JSONSki*, ASPLOS 2022; Gienieczko, Murlak, Paperman, *rsonpath*, ASPLOS 2024 | fast-forward over irrelevant subtrees, descendant (`..key`) queries with depth automata | the descendant query is exactly the InnerTube collector (`$..lockupViewModel`); adopted as `Value::find` over the index instead of a recursive visitor |
| Lemire, *Number parsing at a gigabyte per second*, SP&E 2021 | fast float parsing | numbers are <3 % of tokens and read lazily: `std::from_chars` is enough |
| Seriot, *Parsing JSON is a Minefield* + JSONTestSuite | conformance corpus | used as the acceptance test (§8) |
| yyjson (source) | one allocation, 16 B/value DOM, in-situ | fastest scalar DOM (4.7 ms); 4.2× the body in memory; C, 124 KiB of code for our workload |
| sajson / simdjson tape | the whole DOM as one array of words with skip pointers | the shape adopted, minus copying strings and pre-parsing numbers |
| sonic-cpp | SIMD DOM, lazy get-by-pointer | 3.7 ms but x86/arm64 only, 10.5× the body |

## 5. Results (x86-64, same workload, all byte-identical to the oracle)

| Candidate | Total µs | MB/s | Allocs | Peak heap / body (>100 KB docs) | 32-bit ARM |
|---|---|---|---|---|---|
| **proposal `json.h`, SSE2 (NEON on ARM)** | **3 793** | **2 168** | 1 209 | **0.4–0.5** | yes — NEON kernel, oracle-checked under qemu |
| sonic-cpp | 3 933 | 2 091 | 1 215 | 10.5 | no 32-bit kernel |
| **proposal `json.h`, SWAR (no SIMD)** | **4 511** | **1 823** | 1 209 | 0.4–0.5 | yes |
| simdjson On-Demand (haswell) | 4 782 | 1 719 | 3 862 | 7.5 | fallback only (below) |
| simdjson DOM (haswell) | 4 896 | 1 679 | 971 | 15.2 | fallback only |
| yyjson in-situ (body copied) | 5 032 | 1 634 | 866 | 4.2 | yes |
| yyjson | 5 301 | 1 551 | 866 | 4.2 | yes |
| **current (scanner + Glaze)** | 9 199 | 894 | 9 532 | 0.8 | yes |
| simdjson On-Demand (fallback) | 9 457 | 869 | 3 862 | 7.5 | what ARMv7 gets |
| simdjson DOM (fallback) | 12 695 | 648 | 971 | 15.2 | what ARMv7 gets |
| RapidJSON | 17 052 | 482 | 1 391 | 2.4 | yes |
| Glaze `lazy_json` (already vendored) | 50 229 | 164 | 944 | 0.5 | yes |

Final clean run: one core pinned (`taskset -c 2`), nothing else running, per-document medians,
9.8 MB corpus. "Allocs" is dominated by the output strings the extraction builds (the same for
every candidate); the proposal's own index is one allocation. Peak 0.95× on `player_IOS` is the
output (long URLs), not the index (0.33–0.42× the body).

Glaze's own lazy reader is the zero-new-dependency option; it navigates without an index, so
every lookup re-skips the siblings before it, at every level — the O(depth) re-scan that
`jsonscan.h`'s header already warned about. It is the slowest candidate measured.

"Allocs" is dominated by the output strings the extraction builds (same for everyone); the
proposal's own index is one allocation. Peak 0.94× on `player_IOS` is the output (long URLs).

Index build alone (validating): **2.7 GB/s** SSE2, **2.1 GB/s** SWAR, vs yyjson 1.8, simdjson DOM
2.2 (AVX2), the current `skipValue` 3.2 (which only skips — it produces nothing).

**Code size** for the same workload (stripped delta over an empty extractor):

| | x86 -O2 | x86 -Os | ARMv7 -O2 | ARMv7 -Os |
|---|---|---|---|---|
| current (Glaze) | 310 KiB | 181 KiB | 294 KiB | 220 KiB |
| simdjson On-Demand | 351 KiB | 240 KiB | — | — |
| yyjson (reader only) | 124 KiB | 88 KiB | 145 KiB | 119 KiB |
| **proposal** | **34 KiB** | **16 KiB** | **31 KiB** | **17 KiB** |

**Compile time** of the workload TU (-O3): current 17.1 s, proposal 3.4 s, yyjson 3.3 s.

**Cost of change** (`lockupViewModel`, 8 fields, the 2025–26 renderer migration): Glaze shapes
21 structs + 31 lines of `&&` chains = 52 lines; proposal 23 lines, no declarations.

**ARMv7.** The proposal (NEON and SWAR builds), yyjson and the current reader were cross-built
for Cortex-A8 and run under qemu-arm: all 21 dumps byte-identical to the oracle. **Timing on
ARM was not measured** (qemu timing is meaningless; no device here) — see §9.

## 6. The design

### 6.1 `yt::json` — `include/yt/json.h` (+ `src/json/json.cpp` for the builder and decoder)

Public since 2026-10-16 (owner's decision after the migration): `Client::raw().post` delivers
a `std::shared_ptr<const json::Doc>` — a Doc of its own over the reply's body, never the
cached one, because escaped strings decode into the Doc on first read and a Doc is read by
one thread at a time.

A self-contained component (std only, C++17-compatible, no YouTube knowledge), usable outside
libinnertube.

```cpp
json::Doc doc(body);                         // one validating pass; doc.ok() false if malformed
json::Value v = doc["streamingData"];        // absent propagates: no checks between steps
for (json::Value f : v["adaptiveFormats"].items()) {
    int itag = (int) f["itag"].int64();
    std::string_view url = f["url"].str();   // a view into the body (decoded once if escaped)
}
doc.root().find({"lockupViewModel", "videoRenderer"}, [&](std::string_view key, json::Value r) {
    items.push_back(key == "lockupViewModel" ? fromLockup(r) : fromVideoRenderer(r));
    return json::Visit::SkipRest;            // a leaf: skip it and the rest of its object
});
std::string_view title = r.path("metadata", "lockupMetadataViewModel", "title", "content").str();
```

| Piece | Contract |
|---|---|
| `Doc(std::string_view)` | one pass; validates the grammar, numbers, literals, escapes, control bytes, nesting ≤ `kMaxDepth` (128, iterative — no recursion), size < 512 MiB. Malformed → `!ok()`, every lookup absent. Does **not** validate UTF-8 (bytes pass through; a lone `\uD800` decodes as WTF-8) |
| tape | `u32` per token: kind (2 bits) · escaped/object flag · offset (29 bits); objects/arrays take a second word = index past their close. 0.33–0.42 B per body byte, one allocation |
| `Value` | `{const Doc*, u32}`; `operator[](key)`, `operator[](i)`, `path(k...)`, `last()`, `size()`, `items()`, `members()`, `find(keys, f)`, `str()`, `string()`, `int64(fallback)`, `toInt`, `toDouble`, `boolean`, `isX()`, `raw()` (the value's exact JSON text); `find` callbacks answer `Visit::Descend` / `Skip` / `SkipRest` / `Stop` |
| strings | views into the body; an escaped string decodes once per tape index into the Doc (bounded, stable views) |
| threading | a Doc is read on one thread at a time (engine thread); not copyable; the body must outlive it |
| kernels | SSE2 (x86-64 baseline), NEON (ARMv7/AArch64), SWAR (everything else): only `stringStop()` differs |

### 6.2 YouTube helpers — `src/json/ytjson.h` rewritten over `Value`

`text(Value)` (simpleText, else concatenated runs), `lastThumbUrl`, `lastSourceUrl`,
`firstSourceUrl`, `contentOf`, `flexInt(Value)` (number or strictly-numeric string — the old
`FlexInt`), `base64Decode`, the continuation-token finder (`find({"continuationCommand",
"nextContinuationData", "reloadContinuationData"}, …)`). `json/shapes/` and `jsonscan.h`,
`tokenscan.h` are deleted.

### 6.3 One index per reply

`net::HttpClient::envelope` builds the `Doc` (it already reads every byte for the envelope);
`Reply` gains `std::shared_ptr<const json::Doc> doc` (holding the body alive). Parsers take a
`const json::Doc &` instead of re-scanning the body: one pass per reply instead of two or three
today (envelope + parser + `claim()`'s `parseChannel`). Behaviour change: a 2xx body that is
not well-formed JSON (truncated, HTML) fails as `InvalidResponse` instead of degrading to an
empty list — this is what AGENTS.md §4 already requires ("a truncated body … is a failed Reply").

### 6.4 Writing: `json::Writer`, and Glaze leaves the tree

Request bodies (`innertube/bodies.cpp`, `contextbuilder.cpp`) are small fixed objects written
through Glaze reflection today (117 + 94 KiB of objects). A ~60-line `json::Writer`
(`obj/arr/key/str/num/boolean/end`, RFC 8259 string escaping) replaces it; `json3.cpp` and the
OAuth/RYD readers move to `Value`. Glaze (12 MB in `third_party/`) is then unused by the
library. `yt-cli` writes and reads its state file (`yt-cli-state.json`) with the same
writer and `Doc` (the header became public afterwards, see below). Found on the way: Glaze writes control bytes
other than `\b\f\n\r\t` raw, so a body carrying one was invalid JSON; the writer escapes them.

## 7. Counter-argument (where the proposal is weaker)

* **It is our parser.** yyjson is mature, fuzzed for years, 1.0 API. We own ~400 lines of
  security-relevant code. Mitigation: the same three gates every change must pass (§8), and the
  builder is one function. yyjson would cost a third-party dependency (AGENTS.md §4 forbids it
  without the owner) and 4.2× the body in memory.
* **AVX2 SIMD indexing is faster in isolation** (simdjson stage 1 6.45 GB/s vs our 3.1). On this
  token density it does not survive stage 2, and it has no fast 32-bit ARM path.
* **No UTF-8 validation.** Matches today's behaviour (Glaze does not validate either); YouTube
  answers are UTF-8; consumers get bytes as sent.
* **Unverified on real ARM timing.** NEON→core transfers are slow on Cortex-A8; the SWAR kernel
  may beat NEON there. Both are built and correct; the choice per CPU needs a device run (§9).
* **Lookups are linear in the members of an object** (no hash). Real objects have 2–30
  members; a lookup is a few cache-resident compares.

## 8. Verification (pass criteria)

| Gate | Pass |
|---|---|
| Oracle | all 21 corpus documents byte-identical to `oracle.py`, x86 SSE2/SWAR and ARMv7 NEON/SWAR — `bench/run.sh`, `bench/arm.sh` |
| JSONTestSuite | 0 wrong on y_/n_ (318 files); i_ reported — **met by the prototype** (both kernels, ASan+UBSan) |
| Differential fuzzing | libFuzzer + ASan/UBSan vs strict yyjson: same accept/reject (except ill-formed UTF-8, lone surrogates, nesting > 128, out-of-range numbers — all documented above) and the same tree, `raw()` reparses, `find()` visits every member. **Met by the prototype:** 55 M (SSE2) + 73 M (SWAR) executions in 20 min each, then 27 M after the last builder change: 0 crashes, 0 OOM, 0 timeouts. One real bug was found and fixed on the way (`raw()` of a trailing nested container). |
| Speed | not slower than any candidate that runs on ARMv7, on every document group, x86 — **met** |
| Memory | index ≤ 0.5× body; peak ≤ the current reader's — **met** |
| Code size | JSON + parsers ≤ ¼ of today's in `yt-cli` (bloaty) after migration |
| Library | four trees green, every existing parser test unchanged, live `yt-cli`/`yt-watch`/`yt-channel` |

## 9. Measured on the full migration, and not measured

The whole migration (plan `docs/plans/2026-10-16-json-migration.md`) was carried out on a copy
of the tree in the design session, then executed on the real tree (2026-10-16); the numbers
below are the real tree's, Release, Ryzen 5 5600U:

| | before | after |
|---|---|---|
| parser + JSON objects (`size`, Release) | 2.66 MB | 0.47 MB |
| bodies + context objects | 211 KB | 33 KB |
| stripped `yt-cli` text | 2.88 MB | 1.43 MB |
| library build, `-j1`, Release | 157 s | 56 s |
| the library's parsers over the corpus (index + parse; old: parse only), runs interleaved | 8.2 ms | 3.7 ms |
| every parser's output on 21 live answers + 22 fixtures (2 364 lines) | — | identical |

On the way: the fuzzer's first seed found a bug in the prototype's `raw()` (`[[]   ]` returned
the outer bracket); fixed by walking the last-child chain. The tape prototype's own 15-minute
run (49 M executions) found nothing.

Not measured: wall time on a real ARM CPU (no device here; qemu timing is meaningless).
`bench/arm.sh` builds for ARMv7 with one command; a run on the N9 decides NEON vs SWAR on
Cortex-A8, and times the library before and after.

## 10. Owner decisions (all taken 2026-10-16)

1. **Our own `json.h`** — not yyjson. (The owner allowed any third-party library if it performed
   best; none does on all axes here.)
2. **Glaze leaves entirely**, reader and writer: it is optional and a burden (it forces C++23).
3. **`Reply` carries the `Doc`**; a malformed 2xx body becomes `InvalidResponse`.
4. **`bench/` is committed with the corpus** as `bench/corpus.tar.zst` (~480 KB), scrubbed first:
   the `/player` stream URLs embed the capturing client's public IP (`ip=…`, `/ip/…/`) and every
   answer carries a visitorData — both are replaced by placeholders of the same shape.

## 11. Migration (each stage green in the four trees)

1. `json/json.h` + `json.cpp` + `tst_json.cpp` (JSONTestSuite subset, edge cases, `raw`,
   `find`) + the fuzz target under `tests/fuzz/`.
2. `ytjson.h` helpers over `Value`; `HttpClient::envelope` builds the Doc; `Reply::doc`.
3. Parsers in two steps: the fixed-root readers (player, json3, suggestions, OAuth, QR, RYD),
   then the renderer families together — they share their converters
   (`rendererinternal.h`), so one commit per family would need both APIs side by side.
4. `json::Writer`; bodies and context; delete `json/shapes/`, `jsonscan.h`, `tokenscan.h`,
   Glaze from CMake and `third_party/`.
5. `bench/` (plan 1) + docs (AGENTS.md §3/§5/§7 recipes: "New parser" becomes paths, not
   shapes). Plans: `docs/plans/2026-10-16-json-core.md`, `docs/plans/2026-10-16-json-migration.md`.

## Links

Langdale & Lemire 2019 — arxiv.org/abs/1902.08318 · Keiser & Lemire 2024 — arxiv.org/abs/2312.17149 ·
Mison — microsoft.com/en-us/research/publication/mison-fast-json-parser-data-analytics/ ·
Pison — github.com/AutomataLab/Pison · Sparser — people.eecs.berkeley.edu/~matei/papers/2018/vldb_sparser.pdf ·
JSONSki — github.com/automatalab/jsonski · rsonpath — github.com/rsonquery/rsonpath ·
JSONTestSuite — github.com/nst/JSONTestSuite · yyjson — github.com/ibireme/yyjson ·
simdjson — github.com/simdjson/simdjson · sonic-cpp — github.com/bytedance/sonic-cpp ·
sajson — github.com/chadaustin/sajson
