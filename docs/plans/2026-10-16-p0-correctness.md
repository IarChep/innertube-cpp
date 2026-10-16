# P0 — correctness fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the defects of the current library that the parity research found (W1, W2, W3, W4, W6, W9, W10), each as its own `fix(lib)` commit with a regression test, and let `yt-cli raw` reach signed-in endpoints so the owner can live-check them.

**Architecture:** Small, independent fixes in the places every caller routes through: the body builders (`innertube/bodies.cpp`), the request chains (`core/chains.cpp`), the parsers (`parsers/*.cpp`) and `yt::Link`. No public type changes; the behaviour changes are listed under each task.

**Tech Stack:** C++23 implementation / C++17 public headers, `yt::json`, doctest, CMake + Ninja, libcurl backend.

**Spec:** [`docs/specs/2026-10-16-feature-parity-design.md`](../specs/2026-10-16-feature-parity-design.md) §3 (inventory), §6 P0; evidence per item in [`docs/specs/2026-10-16-feature-gap-list.md`](../specs/2026-10-16-feature-gap-list.md).

Out of this plan, with the reason recorded in the gap list (2026-10-16): W5 (Shorts comments) did not reproduce anonymously — re-checked signed in during P3; W11 (subscribe-state templates) is correct for the TV answers the library reads today — it moves to P1 with the web session; W7 and W8 need P2's MWEB client and guest feed.

## Global Constraints

- Public headers (`include/yt/`) stay C++17; only comments change there in this plan.
- No new third-party dependency.
- Parsers never fail: an absent `json::Value` reads as `""`/`0`/`false`; a malformed answer degrades to empty fields.
- URLs and server-issued ids are byte-exact (the only id rewrite is dropping the `VL` browse prefix, Task 4).
- Credentials never reach a log.
- Style (AGENTS.md §5–§6): 4 spaces; `&`/`*` bind to the name; definition braces on their own line; `(int) x` casts; comments say *why* and cite the source (`// Troubadour (Computershik73) src/net/YTApi.m +rate:as:params:`); no `TODO`/`FIXME`.
- Every fix ships a regression test whose comment names the old failure; run it before the fix and watch it fail.
- Done = warning-free build and all ctests green in `build`, `build-asan`, `build-tsan`, `build-none`; `http_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl` green (Task 10).
- Commits: one per task, message as given, trailer lines (both): `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN`. Commit only when the owner has authorized commits for this plan (AGENTS.md §10).

## Review Focus

1. **A comment refused with HTTP 200** carrying `actionResult.status = STATUS_FAILED` *and* other comments' `commentEntityPayload` — must fail (the status decides first), not pass on the payload (Task 6, `parseCommentAccepted` test d).
2. **A refusal without any words** (`{}` after 200) — fails with a generic message, never an empty `Error::message` (Task 6, chain test).
3. **"VLLL" / "VLWL"** pasted as ids — become LL / WL; saving to LL then goes through like (Tasks 3 and 4, `VLLL` link test).
4. **A signed-out user's playlist** — still anonymous WEB, still the WEB sidebar header; Watch Later signed out stays AuthRequired without a request (Task 5 keeps the existing tests and adds the signed-in case).
5. **A sorted search** keeps its sort params; only relevance gains `EgIQAQ==` (Task 9 runs the existing `CAISAhAB` test next to the new one).

---

## File map

| File | Task | Change |
|---|---|---|
| `examples/yt-cli/main.cpp` | 1 | `raw <endpoint> [json] [client]` |
| `src/innertube/bodies.{h,cpp}`, `tests/tst_bodies.cpp` | 2, 7 | full `accountReadMask`; subscribe `params` |
| `src/core/chains.{h,cpp}`, `tests/tst_chains.cpp` | 3, 5, 6, 7, 8, 9 | LL via rating; TV playlist browse; comment acceptance; subscribe params; FEplaylist_aggregation; relevance params |
| `src/link.cpp`, `include/yt/link.h`, `tests/tst_link.cpp` | 4 | bare `VL…` ids |
| `src/parsers/rendererinternal.h`, `tests/tst_parsers.cpp` | 4 | tile playlist ids without `VL` |
| `src/parsers/playlistparser.cpp`, `src/parsers/rendererparser.h`, `tests/fixtures/playlist_tv.json` | 5 | the TVHTML5 playlist header |
| `src/parsers/commentparser.cpp`, `src/parsers/rendererparser.h` | 6 | `parseCommentAccepted` |
| `src/groups.cpp` | 8 | own playlists = FEplaylist_aggregation |
| `docs/specs/2026-10-16-feature-gap-list.md`, `AGENTS.md` §12 | 10 | status |

Build and test commands used below:

```sh
cmake --build build -j 2>&1 | tail -3
./build/tests/tst_chains -tc="<test name>"      # one doctest case; -tc splits on commas, so use * / ? there
(cd build && ctest -j8 --output-on-failure)     # everything
```

---

### Task 1: `yt-cli raw` names the client

`yt-cli raw` always posts as WEB, and the OAuth bearer rides TVHTML5 only (`ContextBuilder::headers`), so even after `yt-cli signin` it cannot make a single signed-in request. The owner's live checks of Tasks 5, 7 and 8 need one.

**Files:**
- Modify: `examples/yt-cli/main.cpp` (header comment line 12, usage text ~line 146, the `raw` branch ~line 204)

**Interfaces:**
- Produces: `yt-cli raw <endpoint> [json] [client]`, client one of `WEB WEB_SAFARI IOS ANDROID ANDROID_VR TVHTML5 VISIONOS` (default `WEB`); an unknown name exits 2 with a message.

- [ ] **Step 1: Change the header comment line**

```cpp
//   yt-cli raw <endpoint> [json] [client]   e.g. raw browse '{"browseId":"FEwhat_to_watch"}' TVHTML5 (unstable)
```

- [ ] **Step 2: Change the usage text**

Replace `"       raw <endpoint> [json] | signin\n",` with:

```cpp
                             "       raw <endpoint> [json] [client] | signin\n",
```

- [ ] **Step 3: Replace the `raw` branch**

```cpp
    if (cmd == "raw") {                              // unstable escape hatch: prints the JSON
        // The bearer rides TVHTML5 only: a signed-in request names that client.
        static const std::pair<const char *, yt::ClientId> kClients[] = {
            {"WEB", yt::ClientId::WEB}, {"WEB_SAFARI", yt::ClientId::WEB_SAFARI}, {"IOS", yt::ClientId::IOS},
            {"ANDROID", yt::ClientId::ANDROID}, {"ANDROID_VR", yt::ClientId::ANDROID_VR},
            {"TVHTML5", yt::ClientId::TVHTML5}, {"VISIONOS", yt::ClientId::VISIONOS}};
        const std::string name = argc > 4 ? argv[4] : "WEB";
        const auto *hit = std::find_if(std::begin(kClients), std::end(kClients),
                                       [&](const auto &c) { return name == c.first; });
        if (hit == std::end(kClients)) {
            std::fprintf(stderr, "unknown client: %s\n", name.c_str());
            return 2;
        }
        const auto r = yt.raw().post(arg, hit->second, argc > 3 ? argv[3] : "{}").get();
        if (!r.ok()) return fail(*r.error);
        const std::string_view body = r.value->text();
        std::printf("%.*s\n", (int) body.size(), body.data());
        return 0;
    }
```

Add `#include <algorithm>` and `#include <utility>` to the std includes (alphabetical, after `<cstdlib>`/before `<fstream>` for algorithm; `<utility>` last).

- [ ] **Step 4: Build and run it**

Run: `cmake --build build -j 2>&1 | tail -3 && cd build/examples && ./yt-cli raw browse '{"browseId":"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"}' TVHTML5 | head -c 120; echo; ./yt-cli raw browse '{}' NOPE; echo "rc=$?"`
Expected: the first output starts with `{"responseContext"` and contains `tvBrowseRenderer` further on (check with `| grep -c tvBrowseRenderer` → 1); the second prints `unknown client: NOPE` and `rc=2`.

- [ ] **Step 5: Commit**

```bash
git add examples/yt-cli/main.cpp
git commit -m "fix(examples): yt-cli raw names its client

- raw always posted as WEB, and the bearer rides TVHTML5 only, so after signin it could
  not make one signed-in request. An optional third argument names the client.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 2: W1 — `accounts_list` asks for every channel of the account

With `accountReadMask` = `{returnOwner}` only the owner comes back. When the TV session acts as a brand channel, that channel is not in the answer, so `parseAccountsList` cannot find the `isSelected` item and `account().info()` reports the owner instead of the active channel. Troubadour sends the full mask (`src/net/YTApi.m +accountReadMask`, verified 2026-10-16).

**Files:**
- Modify: `src/innertube/bodies.cpp` (`accountsList`), `src/innertube/bodies.h` (its comment)
- Test: `tests/tst_bodies.cpp` (`"account / actions"`), `tests/tst_parsers.cpp`

**Interfaces:**
- Produces: `bodies::accountsList()` → `{"accountReadMask":{"returnOwner":true,"returnBrandAccounts":true,"returnPersonaAccounts":true,"returnFamilyChildAccounts":true,"returnFamilyMembersAccounts":false}}`.

- [ ] **Step 1: Write the failing tests**

In `tests/tst_bodies.cpp`, replace the `accountsList` line of `TEST_CASE("account / actions")` with:

```cpp
    // Only returnOwner was asked: a brand channel the session acts as never came back, so
    // info() reported the owner (W1).
    CHECK(bodies::accountsList() ==
          "{\"accountReadMask\":{\"returnOwner\":true,\"returnBrandAccounts\":true,"
          "\"returnPersonaAccounts\":true,\"returnFamilyChildAccounts\":true,"
          "\"returnFamilyMembersAccounts\":false}}");
```

In `tests/tst_parsers.cpp`, after `TEST_CASE("parsesAccountsList")`, add (it pins the parser half: with several channels the selected one wins):

```cpp
TEST_CASE("accounts_list with brand channels reports the selected one") {
    const char *j = "{\"actions\":[{\"x\":{\"contents\":["
        "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Owner\"},\"isSelected\":false}},"
        "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Brand\"},\"isSelected\":true,"
        "\"serviceEndpoint\":{\"selectActiveIdentityEndpoint\":{\"supportedTokens\":["
        "{\"offlineCacheKeyToken\":{\"clientCacheKey\":\"brand42\"}}]}}}}]}}]}";
    const AccountInfo a = parseAccountsList(J(j));
    CHECK(a.name == "Brand");
    CHECK(a.channelId == "UCbrand42");
}
```

- [ ] **Step 2: Run them**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_bodies -tc="account / actions"; ./build/tests/tst_parsers -tc="accounts_list with brand channels reports the selected one"`
Expected: `tst_bodies` FAILS on the `accountsList` check; the parser test PASSES (the parser was already right — the body hid the brand item).

- [ ] **Step 3: Write the fix**

`src/innertube/bodies.cpp`:

```cpp
std::string accountsList()
{
    // Every channel of the account (Troubadour (Computershik73) src/net/YTApi.m
    // +accountReadMask): with returnOwner alone the brand channel the session acts as is
    // missing, and info() fell back to the owner.
    return json::Writer().beginObject()
        .key("accountReadMask").beginObject()
            .member("returnOwner", true).member("returnBrandAccounts", true)
            .member("returnPersonaAccounts", true).member("returnFamilyChildAccounts", true)
            .member("returnFamilyMembersAccounts", false)
        .endObject()
        .endObject().take();
}
```

`src/innertube/bodies.h`: the declaration comment becomes

```cpp
std::string accountsList();                            // {accountReadMask:{every channel kind but family members}}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_bodies && ./build/tests/tst_parsers && ./build/tests/tst_chains -tc="accountFetchesIdentity"`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/innertube/bodies.cpp src/innertube/bodies.h tests/tst_bodies.cpp tests/tst_parsers.cpp
git commit -m "fix(lib): accounts_list asks for every channel of the account

- accountReadMask was {returnOwner} only: a brand channel the TV session acts as was
  missing from the answer, so account().info() reported the owner. The full mask
  (as Troubadour sends it) brings every channel; the selected one wins.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 3: W2 — saving to Liked videos is a like

`browse/edit_playlist` does not change LL: a video gets into Liked videos by a like and leaves it when the like is removed. `account().save(video, "LL")` and `unsave` sent an edit that the server refuses or ignores. Troubadour: `src/net/YTApi.m +setVideo:saved:inPlaylist:`.

**Files:**
- Modify: `src/core/chains.cpp` (`editPlaylist`), `src/core/chains.h` (its comment)
- Test: `tests/tst_chains.cpp`

**Interfaces:**
- Consumes: `submitAction(IHttp &, ActionKind, std::string targetId, const JobToken &, Callback<Done>)` (existing).

- [ ] **Step 1: Write the failing test**

After `TEST_CASE("playlist edits add and remove by video id")`:

```cpp
// Saving to "LL" sent browse/edit_playlist, which does not change Liked videos: a like
// puts a video there and removing the like takes it out (W2).
TEST_CASE("saving to Liked videos likes, unsaving removes the like") {
    FakeHttp t;
    t.queue("like/like", "{}");
    t.queue("like/removelike", "{}");
    Cap<Done> a, r;
    editPlaylist(t, "LL", AddVideo, "vid42", newJob(), a.fn());
    editPlaylist(t, "LL", RemoveVideo, "vid42", newJob(), r.fn());
    t.flush();
    CHECK(a.r.ok());
    CHECK(r.r.ok());
    REQUIRE(t.sent.size() == 2);
    CHECK(t.sent[0] == "{\"target\":{\"videoId\":\"vid42\"}}");
    CHECK(t.lastClientFor("like/like") == (int) ClientId::TVHTML5);
    CHECK(t.lastClientFor("like/removelike") == (int) ClientId::TVHTML5);
    CHECK(t.lastClientFor("browse/edit_playlist") == -1);
}
```

- [ ] **Step 2: Run it**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_chains -tc="saving to Liked videos likes*"`
Expected: FAIL (`a.r.ok()` false: no fixture queued for `browse/edit_playlist`; `lastClientFor("browse/edit_playlist")` is 7).

- [ ] **Step 3: Write the fix**

In `src/core/chains.cpp`, at the top of `editPlaylist`:

```cpp
void editPlaylist(IHttp &http, std::string playlistId, PlaylistEdit edit, std::string videoId, const JobToken &job,
                  Callback<Done> done)
{
    // Liked videos is not edited: a like puts a video there, removing the like takes it out
    // (Troubadour (Computershik73) src/net/YTApi.m +setVideo:saved:inPlaylist:).
    if (playlistId == "LL") {
        submitAction(http, edit == AddVideo ? Like : RemoveLike, std::move(videoId), job, std::move(done));
        return;
    }
    std::string body = edit == AddVideo ? bodies::playlistAddVideo(playlistId, videoId)
                                        : bodies::playlistRemoveVideo(playlistId, videoId);
    http.post("browse/edit_playlist", ClientId::TVHTML5, std::move(body), job, doneOf(done));
}
```

`submitAction` is defined above `editPlaylist` in the file already (it is), so no forward declaration is needed.

In `src/core/chains.h`, the comment above `editPlaylist` becomes:

```cpp
// WL|PL…, by video id. "LL" (Liked videos) is a like / a removed like, not an edit.
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_chains`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/core/chains.cpp src/core/chains.h tests/tst_chains.cpp
git commit -m "fix(lib): saving to Liked videos is a like, unsaving removes it

- account().save/unsave with \"LL\" sent browse/edit_playlist, which does not change
  Liked videos. LL now goes through like/like and like/removelike, as in Troubadour.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 4: W2 — a `VL…` playlist id is the playlist

TV tiles name playlists by their browse id, `VL` + the playlist id (Troubadour `src/net/YTApi.m +playlistsIn:` accepts `contentId` starting with PL or VL; `+savePlaylistIdFrom:` strips the prefix before every edit). Such an id taken from a list and passed back to `playlists().get` or `account().save` failed as `InvalidArgument` (`Link::parse` does not know it). Fixed at both ends: parsers hand out the bare id, and `Link` accepts the prefixed one.

**Files:**
- Modify: `src/link.cpp` (`parseBare`), `include/yt/link.h` (the `Playlist` comment), `src/parsers/rendererinternal.h` (`fromTilePlaylist`)
- Test: `tests/tst_link.cpp`, `tests/tst_parsers.cpp`

- [ ] **Step 1: Write the failing tests**

`tests/tst_link.cpp`, at the end of `TEST_CASE("playlistAndChannelLinks")`:

```cpp
    // A playlist's browse id (VL + id, as TV tiles name it) was not a link (W2).
    CHECK(is("VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", LinkKind::Playlist, "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"));
    CHECK(is("VLLL", LinkKind::Playlist, "LL"));
    CHECK(is("VLWL", LinkKind::Playlist, "WL"));
    CHECK(is("VLxxxxxxxxx", LinkKind::Video, "VLxxxxxxxxx"));   // 11 characters: a video id
```

`tests/tst_parsers.cpp`, after `TEST_CASE("accounts_list with brand channels reports the selected one")`:

```cpp
// TV tiles may name a playlist VL<id>; that id passed back to playlists().get or save()
// was refused (W2). Playlist::id is the bare id.
TEST_CASE("a TV playlist tile named by its browse id gives the bare id") {
    const char *j = "{\"contents\":[{\"tileRenderer\":{\"contentType\":\"TILE_CONTENT_TYPE_PLAYLIST\","
        "\"contentId\":\"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI\","
        "\"metadata\":{\"tileMetadataRenderer\":{\"title\":{\"simpleText\":\"Mine\"}}}}}]}";
    std::string token;
    const std::vector<Playlist> p = parsePlaylistList(J(j), &token);
    REQUIRE(p.size() == 1);
    CHECK(p[0].id == "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI");
    CHECK(p[0].title == "Mine");
}
```

- [ ] **Step 2: Run them**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_link; ./build/tests/tst_parsers -tc="a TV playlist tile named by its browse id gives the bare id"`
Expected: `tst_link` FAILS on the three `VL…` playlist checks; the parser test FAILS (`p[0].id == "VLPL…"`).

- [ ] **Step 3: Write the fix**

`src/link.cpp`, `parseBare`, after the `isPlaylistId` line:

```cpp
    if (isPlaylistId(s)) return make(LinkKind::Playlist, s);
    // VL + a playlist id is that playlist's browse id (TV tiles name playlists so).
    if (s.substr(0, 2) == "VL" && isPlaylistId(s.substr(2))) return make(LinkKind::Playlist, s.substr(2));
    return std::nullopt;
```

`include/yt/link.h`, the `Playlist` enumerator comment:

```cpp
    Playlist,   // /playlist?list=, watch?list= without v=, or a bare PL… / RD… / UU… / OL… / LL / WL id (also VL-prefixed)
```

`src/parsers/rendererinternal.h`, `fromTilePlaylist`, replace `p.id = t["contentId"].string();` with:

```cpp
    p.id = t["contentId"].string();
    // TV tiles may give the browse id, VL + the playlist id (Troubadour (Computershik73)
    // src/net/YTApi.m +playlistsIn:); Playlist::id is always the bare id.
    if (p.id.compare(0, 2, "VL") == 0 && p.id.size() > 2) p.id.erase(0, 2);
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_link && ./build/tests/tst_parsers && ./build/tests/tst_types`
Expected: PASS (`tst_types` proves `link.h` still compiles at C++17).

- [ ] **Step 5: Commit**

```bash
git add src/link.cpp include/yt/link.h src/parsers/rendererinternal.h tests/tst_link.cpp tests/tst_parsers.cpp
git commit -m "fix(lib): a VL-prefixed playlist id is that playlist

- TV tiles name playlists by their browse id (VL + id). Such an id from a list was
  refused by playlists().get and account().save as an invalid link. Tile playlists now
  carry the bare id, and Link::parse accepts the prefixed form (VLLL -> LL).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 5: W3 — signed in, playlists are browsed as TVHTML5

`fetchPlaylist` chose the client with `clientForBrowse`, which sends every playlist except LL/WL as anonymous WEB. A signed-in user's private or unlisted playlist is invisible to an anonymous browse. Signed in, every playlist now goes TVHTML5 with the bearer (Troubadour `src/net/YTApi.m +playlist:`). The TV answer has no WEB sidebar: its header is the left column's `entityMetadataRenderer` (captured live 2026-10-16 with an anonymous TVHTML5 browse of `VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI`; the entries are `tileRenderer`s that `parseVideoList` already reads, continued by `nextContinuationData` on TVHTML5 — page 2 checked live too).

**Files:**
- Create: `tests/fixtures/playlist_tv.json`
- Modify: `src/core/chains.cpp` (`fetchPlaylist`), `src/parsers/playlistparser.cpp` (`parsePlaylistPage`), `src/parsers/rendererparser.h` (its comment)
- Test: `tests/tst_parsers.cpp`, `tests/tst_chains.cpp`

**Interfaces:**
- Produces: `parsePlaylistPage` also reads the TV header: `title`, `thumbnailUrl`, `videoCount` (owner stays empty on TV: the byline names no channel id).

- [ ] **Step 1: Create the fixture**

`tests/fixtures/playlist_tv.json` (trimmed from the live answer; tracking fields, menus and buttons removed, the continuation token replaced by `TVPLNEXT`):

```json
{
  "contents": {
    "tvBrowseRenderer": {
      "content": {
        "tvSurfaceContentRenderer": {
          "content": {
            "twoColumnRenderer": {
              "leftColumn": {
                "entityMetadataRenderer": {
                  "title": {
                    "simpleText": "Popular Music Videos"
                  },
                  "thumbnail": {
                    "thumbnails": [
                      {
                        "url": "https://yt3.googleusercontent.com/12Z2lesRDLk4t_Pd-lMs-TlvEJBFLIOv1Uh9mJUUneRqYVRRSjAkjcKkCNF7f3dejleGz77F7xN5",
                        "width": 1000,
                        "height": 1000
                      }
                    ]
                  },
                  "bylines": [
                    {
                      "lineRenderer": {
                        "items": [
                          {
                            "lineItemRenderer": {
                              "badge": {
                                "metadataBadgeRenderer": {
                                  "icon": {}
                                }
                              }
                            }
                          },
                          {
                            "lineItemRenderer": {
                              "text": {
                                "simpleText": "Music"
                              }
                            }
                          },
                          {
                            "lineItemRenderer": {
                              "text": {
                                "simpleText": "•"
                              }
                            }
                          },
                          {
                            "lineItemRenderer": {
                              "text": {
                                "runs": [
                                  {
                                    "text": "182"
                                  },
                                  {
                                    "text": " videos"
                                  }
                                ]
                              }
                            }
                          },
                          {
                            "lineItemRenderer": {
                              "text": {
                                "simpleText": "•"
                              }
                            }
                          },
                          {
                            "lineItemRenderer": {
                              "text": {
                                "simpleText": "1B views"
                              }
                            }
                          }
                        ]
                      }
                    }
                  ]
                }
              },
              "rightColumn": {
                "playlistVideoListRenderer": {
                  "contents": [
                    {
                      "tileRenderer": {
                        "header": {
                          "tileHeaderRenderer": {
                            "thumbnail": {
                              "thumbnails": [
                                {
                                  "url": "https://i.ytimg.com/vi/fOT0BUpITw8/sddefault.jpg",
                                  "width": 640,
                                  "height": 480
                                }
                              ]
                            },
                            "thumbnailOverlays": [
                              {
                                "thumbnailOverlayTimeStatusRenderer": {
                                  "text": {
                                    "simpleText": "3:55"
                                  }
                                }
                              }
                            ]
                          }
                        },
                        "metadata": {
                          "tileMetadataRenderer": {
                            "title": {
                              "simpleText": "BELLAKEO (Video Oficial) - Peso Pluma, Anitta"
                            },
                            "lines": [
                              {
                                "lineRenderer": {
                                  "items": [
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "runs": [
                                            {
                                              "text": "Peso Pluma",
                                              "navigationEndpoint": {
                                                "browseEndpoint": {
                                                  "browseId": "UCzrM_068Odho89mTRrrxqbA",
                                                  "canonicalBaseUrl": "/channel/UCzrM_068Odho89mTRrrxqbA"
                                                }
                                              }
                                            }
                                          ]
                                        }
                                      }
                                    }
                                  ]
                                }
                              },
                              {
                                "lineRenderer": {
                                  "items": [
                                    {
                                      "lineItemRenderer": {
                                        "badge": {
                                          "metadataBadgeRenderer": {
                                            "label": "4K"
                                          }
                                        }
                                      }
                                    },
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "simpleText": "768M views"
                                        }
                                      }
                                    },
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "simpleText": "•"
                                        }
                                      }
                                    },
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "simpleText": "2 years ago"
                                        }
                                      }
                                    }
                                  ]
                                }
                              }
                            ]
                          }
                        },
                        "onSelectCommand": {
                          "watchEndpoint": {
                            "videoId": "fOT0BUpITw8",
                            "playlistId": "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"
                          }
                        },
                        "contentId": "fOT0BUpITw8",
                        "contentType": "TILE_CONTENT_TYPE_VIDEO"
                      }
                    },
                    {
                      "tileRenderer": {
                        "header": {
                          "tileHeaderRenderer": {
                            "thumbnail": {
                              "thumbnails": [
                                {
                                  "url": "https://i.ytimg.com/vi/NFvDHYMzj9U/sddefault.jpg?sqp=-oaymwEmCIAFEOAD8quKqQMa8AEB-AH-CYAC0AWKAgwIABABGGMgYyhjMA8=&rs=AOn4CLD_taSxwdDS37p5VTaUVB0J0UhKGg",
                                  "width": 640,
                                  "height": 480
                                }
                              ]
                            },
                            "thumbnailOverlays": [
                              {
                                "thumbnailOverlayTimeStatusRenderer": {
                                  "text": {
                                    "simpleText": "10:50"
                                  }
                                }
                              }
                            ]
                          }
                        },
                        "metadata": {
                          "tileMetadataRenderer": {
                            "title": {
                              "simpleText": "Arcángel - FN8 ( Video Lyric )"
                            },
                            "lines": [
                              {
                                "lineRenderer": {
                                  "items": [
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "runs": [
                                            {
                                              "text": "Arcangel",
                                              "navigationEndpoint": {
                                                "browseEndpoint": {
                                                  "browseId": "UCu4bj03KW4CwAsYtHSwMEsQ",
                                                  "canonicalBaseUrl": "/channel/UCu4bj03KW4CwAsYtHSwMEsQ"
                                                }
                                              }
                                            }
                                          ]
                                        }
                                      }
                                    }
                                  ]
                                }
                              },
                              {
                                "lineRenderer": {
                                  "items": [
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "simpleText": "32M views"
                                        }
                                      }
                                    },
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "simpleText": "•"
                                        }
                                      }
                                    },
                                    {
                                      "lineItemRenderer": {
                                        "text": {
                                          "simpleText": "2 years ago"
                                        }
                                      }
                                    }
                                  ]
                                }
                              }
                            ]
                          }
                        },
                        "onSelectCommand": {
                          "watchEndpoint": {
                            "videoId": "NFvDHYMzj9U",
                            "playlistId": "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI"
                          }
                        },
                        "contentId": "NFvDHYMzj9U",
                        "contentType": "TILE_CONTENT_TYPE_VIDEO"
                      }
                    }
                  ],
                  "playlistId": "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI",
                  "continuations": [
                    {
                      "nextContinuationData": {
                        "continuation": "TVPLNEXT"
                      }
                    }
                  ]
                }
              }
            }
          }
        }
      }
    }
  }
}
```

- [ ] **Step 2: Write the failing tests**

`tests/tst_parsers.cpp`, after `TEST_CASE("a TV playlist tile named by its browse id gives the bare id")`:

```cpp
// A signed-in playlist is browsed as TVHTML5; its header was read from the WEB sidebar
// only, so it came back without a title (W3).
TEST_CASE("the TVHTML5 playlist page: header from entityMetadataRenderer, entries from tiles") {
    const json::Value root = J(loadFixtureRaw("playlist_tv.json"));
    const Playlist p = parsePlaylistPage(root);
    CHECK(p.title == "Popular Music Videos");
    CHECK(p.videoCount == 182);
    CHECK(p.thumbnailUrl.rfind("https://yt3.googleusercontent.com/12Z2lesRDLk4t", 0) == 0);
    std::string token;
    const std::vector<VideoItem> v = parseVideoList(root, &token);
    REQUIRE(v.size() == 2);
    CHECK(v[0].id == "fOT0BUpITw8");
    CHECK(v[0].channel.name == "Peso Pluma");
    CHECK(v[0].channel.id == "UCzrM_068Odho89mTRrrxqbA");
    CHECK(v[0].durationText == "3:55");
    CHECK(v[1].id == "NFvDHYMzj9U");
    CHECK(token == "TVPLNEXT");
}
```

`tests/tst_chains.cpp`, after `TEST_CASE("a playlist: its header and first page of entries, continued by more")`:

```cpp
// Signed in, a playlist other than WL/LL was browsed as anonymous WEB: the account's
// private and unlisted playlists could not be opened (W3).
TEST_CASE("signed in, a playlist is browsed as TVHTML5 and continues there") {
    FakeHttp t; t.session().bearer = "tok";
    t.queue("browse", loadFixtureRaw("playlist_tv.json"));
    Cap<Playlist> c;
    fetchPlaylist(t, "PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI", newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.lastClientFor("browse") == (int) ClientId::TVHTML5);
    CHECK(t.sent[0] == "{\"browseId\":\"VLPLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI\"}");
    CHECK(c.r.value.title == "Popular Music Videos");
    CHECK(c.r.value.entries.items.size() == 2);
    CHECK(tokenOf(c.r.value.entries.next) == "TVPLNEXT");
    CHECK(detail::CursorAccess::client(c.r.value.entries.next) == ClientId::TVHTML5);
}
```

- [ ] **Step 3: Run them**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_parsers -tc="the TVHTML5 playlist page*"; ./build/tests/tst_chains -tc="signed in? a playlist is browsed as TVHTML5*"`
Expected: the parser test FAILS (`p.title` empty, `videoCount` 0); the chain test FAILS (`lastClientFor("browse")` is 0 = WEB, title empty).

- [ ] **Step 4: Write the fix**

`src/core/chains.cpp`, `fetchPlaylist`, replace `const ClientId cid = clientForBrowse(browseId, http.session());` with:

```cpp
    // Signed in, every playlist is browsed as TVHTML5 with the bearer: the account's private
    // and unlisted playlists are invisible to an anonymous WEB browse (Troubadour
    // (Computershik73) src/net/YTApi.m +playlist:).
    const ClientId cid = http.session().bearer.empty() ? ClientId::WEB : ClientId::TVHTML5;
```

`src/parsers/playlistparser.cpp`, `parsePlaylistPage`, before `return p;`:

```cpp
    // TVHTML5 (a signed-in browse) has no sidebar: the left column's entityMetadataRenderer
    // holds the title, the cover and a byline "Music • 182 videos • 1B views" with no fixed
    // order. The count is the one item split into a number run and a label run (the label
    // is localized, the number is not).
    const Value e = response.path("contents", "tvBrowseRenderer", "content", "tvSurfaceContentRenderer",
                                  "content", "twoColumnRenderer", "leftColumn", "entityMetadataRenderer");
    if (p.title.empty()) p.title = textOf(e["title"]);
    if (p.thumbnailUrl.empty() && e["thumbnail"]) p.thumbnailUrl = lastThumbUrl(e["thumbnail"]);
    for (Value line : e["bylines"].items())
        for (Value item : line.path("lineRenderer", "items").items()) {
            const Value runs = item.path("lineItemRenderer", "text", "runs");
            if (p.videoCount == 0 && runs[1]) p.videoCount = digitsOf(runs[0]["text"].str());
        }
```

`src/parsers/rendererparser.h`, the comment above `parsePlaylistPage` becomes:

```cpp
// A playlist page (browse of VL<id>): the header — title, description, owner, video count,
// cover — from the WEB sidebar, or from the TVHTML5 entityMetadataRenderer (signed in; no
// owner there). Its entries are parseVideoList of the same response.
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_parsers && ./build/tests/tst_chains && ./build/tests/tst_client`
Expected: PASS — including the unchanged `"a playlist: its header and first page of entries, continued by more"` (signed out: WEB, sidebar header) and `"Watch Later signed out is AuthRequired, without a request"`.

- [ ] **Step 6: Commit**

```bash
git add tests/fixtures/playlist_tv.json src/core/chains.cpp src/parsers/playlistparser.cpp src/parsers/rendererparser.h tests/tst_parsers.cpp tests/tst_chains.cpp
git commit -m "fix(lib): signed in, playlists are browsed as TVHTML5

- Every playlist but WL/LL went anonymous WEB, so a signed-in user's private and
  unlisted playlists could not be opened. Signed in, playlists now browse as TVHTML5
  with the bearer (as Troubadour does), and the page header is also read from the TV
  answer's entityMetadataRenderer (title, cover, video count).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 6: W4 — a posted comment succeeds only when YouTube accepted it

`postComment` reported success on any 2xx. YouTube answers a refused comment (held for review, rate limit, a channel that may not comment) with 200 and a refusal. Success is an `actionResult` whose status is `STATUS_SUCCEEDED`, or — without any `actionResult` — the created comment in the answer (`commentEntityPayload` / `createCommentAction`); otherwise the call fails with the server's own words (Troubadour `src/net/YTApi.m +commentAccepted:`, `+refusalTextIn:`).

**Files:**
- Modify: `src/parsers/commentparser.cpp`, `src/parsers/rendererparser.h`, `src/core/chains.cpp` (`postComment`)
- Test: `tests/tst_parsers.cpp`, `tests/tst_chains.cpp`

**Interfaces:**
- Produces: `bool parseCommentAccepted(json::Value response, std::string *refusal);` in `rendererparser.h` — `refusal` (nullable) gets the server's refusal text when the answer is not a success.

- [ ] **Step 1: Write the failing tests**

`tests/tst_parsers.cpp`, after the W3 test:

```cpp
// A create_comment answer is a success only by its actionResult or the created comment;
// a 200 refusal used to count as posted (W4).
TEST_CASE("parseCommentAccepted") {
    std::string why;
    CHECK(parseCommentAccepted(J("{\"actionResult\":{\"status\":\"STATUS_SUCCEEDED\"}}"), &why));
    CHECK(parseCommentAccepted(J("{\"actions\":[{\"createCommentAction\":{\"contents\":{}}}]}"), &why));
    CHECK_FALSE(parseCommentAccepted(J(
        "{\"actionResult\":{\"status\":\"STATUS_FAILED\"},\"actions\":[{\"addToastAction\":{\"item\":"
        "{\"notificationTextRenderer\":{\"successResponseText\":{\"runs\":[{\"text\":\"Comment failed to post.\"}]}}}}}]}"),
        &why));
    CHECK(why == "Comment failed to post.");
    // The status decides before any payload: other comments in a refusal are no success.
    why.clear();
    CHECK_FALSE(parseCommentAccepted(J("{\"actionResult\":{\"status\":\"STATUS_FAILED\"},"
                                       "\"x\":{\"commentEntityPayload\":{\"properties\":{}}}}"), &why));
    CHECK(why.empty());
    CHECK_FALSE(parseCommentAccepted(J("{}"), nullptr));
}
```

`tests/tst_chains.cpp`: in `TEST_CASE("posting a comment finds its createCommentParams on the comments page")`, change the queued create answer to a real success:

```cpp
    t.queue("comment/create_comment", "{\"actionResult\":{\"status\":\"STATUS_SUCCEEDED\"}}");
```

and after `TEST_CASE("posting where comments are off fails without a create request")` add:

```cpp
// A 200 refusal (held for review, rate limit) was reported as posted (W4).
TEST_CASE("a comment YouTube refuses with 200 fails with the server's words") {
    FakeHttp t;
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", "{\"x\":{\"createCommentParams\":\"CPARAMS\"}}");
    t.queue("comment/create_comment", "{\"actionResult\":{\"status\":\"STATUS_FAILED\"},\"actions\":[{\"x\":"
            "{\"alertRenderer\":{\"text\":{\"simpleText\":\"You're posting too fast.\"}}}}]}");
    t.queue("next", loadFixtureRaw("next_for_comments.json"));
    t.queue("next", "{\"x\":{\"createCommentParams\":\"CPARAMS\"}}");
    t.queue("comment/create_comment", "{}");
    Cap<Done> said, mute;
    postComment(t, "aaa11111111", "hi", newJob(), said.fn());
    t.flush();
    postComment(t, "aaa11111111", "hi", newJob(), mute.fn());
    t.flush();
    REQUIRE_FALSE(said.r.ok());
    CHECK(said.r.error->code == ErrorCode::Api);
    CHECK(said.r.error->message == "You're posting too fast.");
    CHECK(said.r.error->httpStatus == 200);
    REQUIRE_FALSE(mute.r.ok());
    CHECK(mute.r.error->code == ErrorCode::Api);
    CHECK(mute.r.error->message == "the comment was not accepted");
}
```

- [ ] **Step 2: Run them**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: the build FAILS — `parseCommentAccepted` is not declared. (That is the failing state for the parser test; the chain test is run in Step 4.)

- [ ] **Step 3: Write the fix**

`src/parsers/rendererparser.h`, after the `parseComments` declaration:

```cpp
// A comment/create_comment (or reply/update) answer: true when YouTube accepted the text —
// an actionResult with STATUS_SUCCEEDED, or, without any actionResult, the created comment
// in the answer. Otherwise false, and `refusal` (nullable) gets the server's own words
// ("" when it gave none).
bool parseCommentAccepted(json::Value response, std::string *refusal);
```

`src/parsers/commentparser.cpp`, after `parseComments`:

```cpp
bool parseCommentAccepted(Value response, std::string *refusal)
{
    // The first actionResult with a status decides; without one, the created comment
    // itself is the success (Troubadour (Computershik73) src/net/YTApi.m +commentAccepted:).
    std::string status;
    response.find({"actionResult"}, [&](std::string_view, Value v) {
        status = v["status"].string();
        return status.empty() ? Visit::Skip : Visit::Stop;
    });
    bool ok = status == "STATUS_SUCCEEDED";
    if (status.empty())
        response.find({"commentEntityPayload", "createCommentAction"}, [&](std::string_view, Value) {
            ok = true;
            return Visit::Stop;
        });
    if (ok || !refusal) return ok;
    // The words the official client would show in its toast or dialog
    // (Troubadour src/net/YTApi.m +refusalTextIn:).
    response.find({"notificationTextRenderer", "alertRenderer", "messageRenderer", "confirmDialogRenderer",
                   "backstagePostDialogRenderer"}, [&](std::string_view, Value node) {
        for (const char *field : {"successResponseText", "errorMessage", "text", "title", "message", "dialogMessage"}) {
            std::string said = textOf(node[field]);
            if (!said.empty()) {
                *refusal = std::move(said);
                return Visit::Stop;
            }
        }
        return Visit::Skip;
    });
    return false;
}
```

`src/core/chains.cpp`, `postComment`, replace
`h->post("comment/create_comment", ClientId::TVHTML5, bodies::createComment(createParams, text), job, doneOf(done));`
with:

```cpp
        h->post("comment/create_comment", ClientId::TVHTML5, bodies::createComment(createParams, text), job,
                [done](const Reply &r) {
            Result<Done> out;
            std::string refusal;
            if (!r.ok) out.error = errorOf(r);
            // A 200 can be a refusal (held for review, posting too fast): only YouTube's
            // acceptance counts as posted.
            else if (!parseCommentAccepted(r.json(), &refusal))
                out.error = Error{ErrorCode::Api, refusal.empty() ? "the comment was not accepted" : refusal,
                                  r.httpStatus};
            done(out);
        });
```

and in `src/core/chains.h`, the comment above `postComment` becomes:

```cpp
// A top-level comment: the video's comments page supplies the createCommentParams. Comments
// turned off fail with Unavailable; a comment YouTube does not accept (a 200 refusal) fails
// with Api and the server's words.
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_parsers -tc="parseCommentAccepted" && ./build/tests/tst_chains`
Expected: PASS. To see the chain test fail without the fix: temporarily restore `doneOf(done)` in `postComment`, rebuild, run `./build/tests/tst_chains -tc="a comment YouTube refuses*"` → FAIL (`said.r.ok()` true); restore the fix.

- [ ] **Step 5: Commit**

```bash
git add src/parsers/commentparser.cpp src/parsers/rendererparser.h src/core/chains.cpp src/core/chains.h tests/tst_parsers.cpp tests/tst_chains.cpp
git commit -m "fix(lib): a posted comment succeeds only when YouTube accepted it

- comments().post reported success on any 2xx, but YouTube refuses with 200 (held for
  review, posting too fast). Success is now an actionResult STATUS_SUCCEEDED or the
  created comment; a refusal fails with ErrorCode::Api and the server's own words, as
  Troubadour reads them.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 7: W6 — subscribe and unsubscribe send the apps' `params`

Both apps send a constant `params` with `subscription/subscribe` (`CgIIAxgA`) and `subscription/unsubscribe` (`CgIIAxgB`) — Troubadour `src/net/YTApi.m +setSubscribed:channel:`, youtube_uwp (zemonkamin) `DefaultSubscribeParams` / `DefaultUnsubscribeParams`. The library sent none.

**Files:**
- Modify: `src/innertube/bodies.{h,cpp}` (`subscribeChannels`), `src/core/chains.cpp` (`submitAction`)
- Test: `tests/tst_bodies.cpp`, `tests/tst_chains.cpp` (`"actions"`)

**Interfaces:**
- Produces: `std::string bodies::subscribeChannels(std::string_view channelId, bool subscribe);` (replaces the one-argument form; `submitAction` is its only caller).

- [ ] **Step 1: Write the failing tests**

`tests/tst_bodies.cpp`, replace the `subscribeChannels` line of `"account / actions"` with:

```cpp
    // The apps' constant params were missing from subscribe/unsubscribe (W6).
    CHECK(bodies::subscribeChannels("UC1", true) == "{\"channelIds\":[\"UC1\"],\"params\":\"CgIIAxgA\"}");
    CHECK(bodies::subscribeChannels("UC1", false) == "{\"channelIds\":[\"UC1\"],\"params\":\"CgIIAxgB\"}");
```

`tests/tst_chains.cpp`, `TEST_CASE("actions")`, change the two subscribe rows' needles:

```cpp
        {Subscribe, "subscription/subscribe", "UCabc", "\"params\":\"CgIIAxgA\""},
        {Unsubscribe, "subscription/unsubscribe", "UCabc", "\"params\":\"CgIIAxgB\""},
```

- [ ] **Step 2: Run them**

Run: `cmake --build build -j 2>&1 | tail -3`
Expected: the build FAILS (`subscribeChannels` takes one argument) — the failing state.

- [ ] **Step 3: Write the fix**

`src/innertube/bodies.h`:

```cpp
// {channelIds:[id], params}: the constant params the apps send — CgIIAxgA subscribes,
// CgIIAxgB unsubscribes (Troubadour +setSubscribed:channel:, youtube_uwp DefaultSubscribeParams).
std::string subscribeChannels(std::string_view channelId, bool subscribe);
```

`src/innertube/bodies.cpp`:

```cpp
std::string subscribeChannels(std::string_view channelId, bool subscribe)
{
    return json::Writer().beginObject()
        .key("channelIds").beginArray().str(channelId).endArray()
        .member("params", subscribe ? "CgIIAxgA" : "CgIIAxgB")
        .endObject().take();
}
```

`src/core/chains.cpp`, `submitAction`:

```cpp
    std::string body = spec.channel ? bodies::subscribeChannels(targetId, kind == Subscribe)
                                    : bodies::likeTarget(targetId);
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_bodies && ./build/tests/tst_chains -tc="actions" && ./build/tests/tst_chains -tc="subscribe by link resolves the channel first"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/innertube/bodies.h src/innertube/bodies.cpp src/core/chains.cpp tests/tst_bodies.cpp tests/tst_chains.cpp
git commit -m "fix(lib): subscribe and unsubscribe send the apps' params

- subscription/subscribe and /unsubscribe went without the constant params both
  Troubadour and youtube_uwp send (CgIIAxgA / CgIIAxgB). They are sent now.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 8: W9 — the account's own playlists are `FEplaylist_aggregation`

`account().playlists()` browsed `FElibrary`. Both apps read the user's playlists from `FEplaylist_aggregation` on TVHTML5 with the bearer (Troubadour `src/net/YTApi.m +myPlaylists`, youtube_uwp `GetMyPlaylistsAsync`), device-verified there; `FElibrary` was never verified live here. The browse id must stay in the sign-in-only list, or a signed-out call would go out anonymously.

**Files:**
- Modify: `src/groups.cpp` (`Account::playlists`), `src/core/chains.cpp` (`feedRequiresAuth`, the comment in `fetchPlaylists`)
- Test: `tests/tst_chains.cpp`

- [ ] **Step 1: Write the failing test**

After `TEST_CASE("playlistBrowse")`:

```cpp
// The account's playlists were browsed as FElibrary; the apps use FEplaylist_aggregation
// (TVHTML5 + bearer). It needs sign-in like the other private feeds (W9).
TEST_CASE("FEplaylist_aggregation is private: TVHTML5 signed in, AuthRequired signed out") {
    FakeHttp out;
    Cap<Page<Playlist>> refused;
    fetchPlaylists(out, "FEplaylist_aggregation", "", newJob(), refused.fn());
    out.flush();
    REQUIRE_FALSE(refused.r.ok());
    CHECK(refused.r.error->code == ErrorCode::AuthRequired);
    CHECK(out.sent.empty());
    FakeHttp in; in.session().bearer = "tok";
    in.queue("browse", "{\"contents\":[{\"tileRenderer\":{\"contentType\":\"TILE_CONTENT_TYPE_PLAYLIST\","
             "\"contentId\":\"PLmine0000000\"}}]}");
    Cap<Page<Playlist>> mine;
    fetchPlaylists(in, "FEplaylist_aggregation", "", newJob(), mine.fn());
    in.flush();
    REQUIRE(mine.r.ok());
    CHECK(in.lastClientFor("browse") == (int) ClientId::TVHTML5);
    REQUIRE(mine.r.value.items.size() == 1);
    CHECK(mine.r.value.items[0].id == "PLmine0000000");
}
```

- [ ] **Step 2: Run it**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_chains -tc="FEplaylist_aggregation is private*"`
Expected: FAIL (signed out it is sent as WEB; signed in `lastClientFor("browse")` is WEB).

- [ ] **Step 3: Write the fix**

`src/core/chains.cpp`, `feedRequiresAuth`:

```cpp
bool feedRequiresAuth(const std::string &id)
{
    return id == "FEsubscriptions" || id == "FEhistory" || id == "FEplaylist_aggregation" || id == "FEchannels"
        || id == "VLLL" || id == "VLWL";   // the private Liked / Watch Later playlists
}
```

and the comment in `fetchPlaylists`:

```cpp
    // Signed-in FEplaylist_aggregation (the user's own playlists) is feedRequiresAuth → TVHTML5.
```

`src/groups.cpp`, `Account::playlists`:

```cpp
Job Account::playlists(Callback<Page<Playlist>> done)
{
    // The apps' own-playlists feed (Troubadour (Computershik73) src/net/YTApi.m +myPlaylists).
    return run<Page<Playlist>>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchPlaylists(h, "FEplaylist_aggregation", "", t, std::move(d));
    }, std::move(done));
}
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_chains && ./build/tests/tst_client`
Expected: PASS — `tst_client`'s `"a feed that needs sign-in fails with AuthRequired when signed out, without a request"` still covers `account().playlists()` signed out.

- [ ] **Step 5: Commit**

```bash
git add src/core/chains.cpp src/groups.cpp tests/tst_chains.cpp
git commit -m "fix(lib): the account's own playlists are FEplaylist_aggregation

- account().playlists() browsed FElibrary, never verified live. Troubadour and
  youtube_uwp read the user's playlists from FEplaylist_aggregation on TVHTML5 with the
  bearer; the library does the same, sign-in only like the other private feeds.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 9: W10 — relevance search asks for videos

`search().videos(q)` with the default (relevance) order sent no `params`, so YouTube answered every kind mixed — Shorts shelves, channels, playlists; measured 2026-10-16: 26 mixed items, versus 19–20 regular videos per page with the videos filter `EgIQAQ==` that Troubadour sends (`src/net/YTApi.m +search:continuation:kind:`). The sorted orders already carry the same type filter in their params (`CAISAhAB` = sort 2 + type video).

**Files:**
- Modify: `src/core/chains.cpp` (`sortParam`), `include/yt/types.h` (the `SearchSort` comment)
- Test: `tests/tst_chains.cpp`

- [ ] **Step 1: Write the failing test**

After `TEST_CASE("search pages continue through the search endpoint without the query")`:

```cpp
// Relevance search sent no params and got Shorts shelves, channels and playlists mixed in
// with the videos (W10); like the sorted orders it asks for videos only.
TEST_CASE("a relevance search asks for videos only") {
    FakeHttp t; t.queue("search", kSearchPage1);
    VideoListSpec spec; spec.kind = VideoListSpec::Search; spec.query = "cats";
    Cap<Page<VideoItem>> c;
    fetchVideoList(t, spec, newJob(), c.fn());
    t.flush();
    REQUIRE(c.r.ok());
    CHECK(t.sent[0] == "{\"query\":\"cats\",\"params\":\"EgIQAQ==\"}");
}
```

- [ ] **Step 2: Run it**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_chains -tc="a relevance search asks for videos only"`
Expected: FAIL (sent body is `{"query":"cats"}`).

- [ ] **Step 3: Write the fix**

`src/core/chains.cpp`, `sortParam`:

```cpp
// Search order -> the base64 protobuf `params`; every one also filters to videos (field 2
// {type 1}). Relevance is the bare filter EgIQAQ== (Troubadour (Computershik73)
// src/net/YTApi.m +search:continuation:kind:): without it YouTube mixes in Shorts shelves,
// channels and playlists.
std::string sortParam(SearchSort sort)
{
    switch (sort) {
    case SearchSort::UploadDate: return "CAISAhAB";
    case SearchSort::Views:      return "CAMSAhAB";
    case SearchSort::TopRated:   return "CAESAhAB";
    case SearchSort::Relevance:  break;
    }
    return "EgIQAQ==";
}
```

`include/yt/types.h`, above `enum class SearchSort`:

```cpp
// Every order returns videos only (no Shorts shelves, channels or playlists).
```

- [ ] **Step 4: Run the tests**

Run: `cmake --build build -j 2>&1 | tail -3 && ./build/tests/tst_chains && ./build/tests/tst_client && ./build/tests/tst_types`
Expected: PASS, including the sorted-order test (`CAISAhAB`).

- [ ] **Step 5: Live check (anonymous)**

Run: `cd build/examples && ./yt-cli search "rick astley" | head -25`
Expected: about 20 regular videos with durations, no channel or Shorts rows.

- [ ] **Step 6: Commit**

```bash
git add src/core/chains.cpp include/yt/types.h tests/tst_chains.cpp
git commit -m "fix(lib): relevance search asks for videos only

- The default search order sent no params, so YouTube mixed Shorts shelves, channels
  and playlists into search().videos. It sends the videos filter EgIQAQ== now, as
  Troubadour does; the sorted orders already carried it.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```

---

### Task 10: verification across the trees, live checks, status

**Files:**
- Modify: `docs/specs/2026-10-16-feature-gap-list.md`, `AGENTS.md` (§12 Status / Next step), this plan (the live-check table below)

- [ ] **Step 1: All four trees**

Run:

```sh
for b in build build-asan build-tsan build-none; do
  echo "== $b"; cmake --build $b -j 2>&1 | grep -E "warning|error" ; (cd $b && ctest -j8 --output-on-failure | tail -3)
done
http_proxy=http://127.0.0.1:9 ./build/tests/tst_conformance_curl | tail -2
```

Expected: no warning or error lines; `100% tests passed` in every tree; the conformance run passes. (`build-tsan` may need `setarch -R` on kernels with high ASLR entropy: `setarch $(uname -m) -R ctest …`.)

- [ ] **Step 2: Anonymous live checks**

Run: `cd build/examples && ./yt-cli playlist PLFgquLnL59alCl_2TQvOiD5Vgm1hCaGSI | head -3 && ./yt-cli search minecraft | head -5`
Expected: the playlist prints `Popular Music Videos — Music · <N> videos` (signed out: WEB); the search prints videos only.

- [ ] **Step 3: Owner live checks (signed in)**

The agent has no account; the owner runs these after `./yt-cli signin` (in `build/examples`) and the results go into the table below with the date:

| Item | Command | Pass when |
|---|---|---|
| W3 | `./yt-cli playlist <one of your private PL… ids>` | title and entries print |
| W6 | `./yt-cli raw subscription/subscribe '{"channelIds":["UCuAXFkgsw1L7xaCfnd5JJOw"],"params":"CgIIAxgA"}' TVHTML5`, then the same with `subscription/unsubscribe` and `CgIIAxgB` | both answer JSON without `"error"`, and the channel's subscribe state flips on youtube.com |
| W9 | `./yt-cli raw browse '{"browseId":"FEplaylist_aggregation"}' TVHTML5 \| grep -o TILE_CONTENT_TYPE_PLAYLIST \| wc -l` and the same with `FElibrary` | `FEplaylist_aggregation` lists your playlists (count > 0) |
| W1 | `./yt-cli raw account/accounts_list '{"accountReadMask":{"returnOwner":true,"returnBrandAccounts":true,"returnPersonaAccounts":true,"returnFamilyChildAccounts":true,"returnFamilyMembersAccounts":false}}' TVHTML5 \| grep -o '"accountName"' \| wc -l` | one per channel of the account |

| Item | Date | Result |
|---|---|---|
| W1 | | |
| W3 | | |
| W6 | | |
| W9 | | |

- [ ] **Step 4: Status in the docs**

In `docs/specs/2026-10-16-feature-gap-list.md`, append ` — fixed <short hash> (P0)` to the W1, W2, W3, W4, W6, W9 and W10 entries (each with its own commit; W2 has two). In `AGENTS.md` §12 replace the Status / Next step lines with:

```markdown
**Status:** P0 done (fix commits listed in the gap list); owner live checks recorded in
`docs/plans/2026-10-16-p0-correctness.md` Task 10.
**Next step:** the P1 design details → plan `docs/plans/<date>-p1-identities.md`.
```

- [ ] **Step 5: Commit**

```bash
git add docs/specs/2026-10-16-feature-gap-list.md AGENTS.md docs/plans/2026-10-16-p0-correctness.md
git commit -m "docs: P0 done — the gap list marks its fixes, AGENTS.md names P1 next

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JVbj4yPckDWcFeHEVPaDjN"
```
