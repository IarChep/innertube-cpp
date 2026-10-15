# API scenarios: the code a consumer writes (R1 of the API redesign)

**Status:** agreed (2026-10-15); R2–R5 implemented by
[../plans/2026-10-15-api-groups.md](../plans/2026-10-15-api-groups.md) — see its deviations table.
Step R1 of [`2026-10-15-api-usability-research.md`](2026-10-15-api-usability-research.md): the
public API is designed from the code its users write, not the other way round (Cwalina's
scenario-driven design). R6 (`choose`, `captionFor`, `cueAt`) is implemented: `choose` prefers
separate streams, then HLS, then progressive (ANDROID_VR's itag 18 answered 403), so S1 prints
whichever it picked. R7 (`VideoItem`/`Video`, `myRating`, channel tabs) is not implemented yet.

**How to read it.** Each scenario is a user-level task ("play a pasted link", not "call
/player"). It shows the proposed code, and — where the difference matters — today's code for the
same task. Tags say when the *feature* exists: **[today]** the library can already do it (only
the API shape changes), **[phase 2]**/**[phase 3]**/**[phase 4]** it arrives with that phase of
the roadmap. All samples are C++17 (no designated initializers), because consumers include the
headers at C++17.

---

## The shape in one screen

```cpp
#include <yt/yt.h>                         // one umbrella header

yt::Client yt;                             // defaults: curl backend, own engine thread

// Every operation lives in a group named after what the user thinks about.
yt.videos()     .get(link) / .player(link) / .captions(track) / .votes(link)
yt.search()     .videos(q) / .channels(q) / .playlists(q) / .suggestions(prefix)
yt.channels()   .get(link) / .videos(link) / .shorts(link) / .streams(link) / .playlists(link)
yt.playlists()  .get(link)
yt.feeds()      .home() / .trending() / .subscriptions() / .history()
yt.comments()   .list(link) / .replies(thread) / .post(link, text) / .reply(thread, text)
yt.account()    .info() / .rate(link, Rating) / .subscribe(link) / .save(video, playlist) …
yt.auth()       .signIn(onCode, done) / .signOut() / .signedIn()

// Every operation has two forms:
yt.videos().player(link, [](const yt::Result<yt::Player> &r) { … });   // callback → yt::Job
yt::Result<yt::Player> r = yt.videos().player(link).get();             // std::future

// Every list is a yt::Page<T>; one call continues any of them.
yt.more(page.next, callback);              // page.next is an opaque yt::Cursor<T>
```

`link` is anything the user has: a bare id, a watch / `youtu.be` / shorts / live / embed URL, a
`/@handle`, `/channel/UC…`, `/c/…`, `/user/…` or `?list=` URL. No method asks for an InnerTube
browse id, protobuf `params` or a continuation string.

---

## S1. First contact: a pasted link → title and something playable  [today]

The README quickstart. A script, a test or a CLI must not need an executor or an event loop.

```cpp
#include <yt/yt.h>
#include <cstdio>

int main(int argc, char **argv)
{
    yt::Client yt;
    yt::Result<yt::Player> r = yt.videos().player(argv[1]).get();   // blocks this thread only
    if (!r.ok()) { std::printf("error: %s\n", r.error->message.c_str()); return 1; }

    std::printf("%s\n", r.value.details.title.c_str());
    yt::Choice c = yt::choose(r.value);                             // the best way to play it
    if (c.video) std::printf("%s\n%s\n", c.video->url.c_str(), c.audio->url.c_str());
    else if (!c.hls.empty()) std::printf("%s\n", c.hls.c_str());
}
```

Today the same needs `ClientOptions::deliver`, a hand-written queue (`yt-cli`'s `MainQueue`, 30
lines), a `PlayerRequest` and a loop over `sources × formats`.

*Implies:* future-returning overloads (R5); `choose()` with sensible defaults (R6); links
accepted as ids (R3); the umbrella header.

## S2. Play a video in an app  [today]

The player screen: pick what this device can decode, hand the URL **and the User-Agent** to the
media engine, show a seek preview, refresh before the URLs expire.

```cpp
yt::Preference want;
want.maxHeight = 720;                       // the N9 decodes ≤720p H.264
want.videoCodecs = {"avc1"};
want.audioCodecs = {"mp4a"};
want.audioLanguage = "ru";                  // a dubbed track when there is one

job = yt.videos().player(link, [this, want](const yt::Result<yt::Player> &r) {
    if (!r.ok()) return showError(r.error->message);
    const yt::Player &p = r.value;
    if (p.status.kind != yt::Playability::Ok)
        return showVerdict(p.status);       // bot check, age gate, "live in 3 days", removed…

    yt::Choice c = yt::choose(p, want);
    if (c.video && c.audio) media.playSeparate(c.video->url, c.audio->url, c.source->userAgent);
    else if (c.progressive)  media.play(c.progressive->url, c.source->userAgent);
    else if (!c.hls.empty()) media.playHls(c.hls, c.source->userAgent);

    seekBar.setPreview([p](int64_t ms) { return yt::storyboardFrame(p.storyboard.back(), ms); });
    refreshTimer.start(c.source->expiresAtSecs);   // re-ask player() before the URLs die
});
```

The full `Player` stays available: a client that plays SABR or wants every audio track reads
`p.sources` directly (the library returns everything; `choose` only answers the common question).

*Implies:* `Preference` / `Choice` (R6); `Choice` carries the `Source` (client + UA + expiry);
verdicts unchanged.

## S3. Search with filters, "load more" and suggestions  [today]

```cpp
// While typing:
yt.search().suggestions(text, [this](const yt::Result<std::vector<std::string>> &r) {
    if (r.ok()) suggestionList.set(r.value);
});

// On enter:
yt::SearchOptions opt;
opt.sort = yt::SearchSort::UploadDate;          // Relevance (default), UploadDate, Views, Rating
yt.search().videos(query, opt, [this](const yt::Result<yt::Page<yt::VideoItem>> &r) {
    if (!r.ok()) return showError(r.error->message);
    results.append(r.value.items);
    next = r.value.next;                         // yt::Cursor<yt::VideoItem>
    loadMoreButton.setVisible(bool(next));
});

// On "load more":
yt.more(next, sameCallback);
```

Channels and playlists: `yt.search().channels(q, cb)`, `yt.search().playlists(q, cb)` — the
same page type, the same `more`. Today: `search({q, "date"}, …)` → `VideoPage::next` (a raw
string) → `search` again with … nothing that accepts it; paging a search is not possible from the
public API, and channel/playlist search have different method names (`searchChannels`,
`searchPlaylists`) and no paging.

*Implies:* `Page<T>` + typed `Cursor<T>` + one `Client::more` (R4); `SearchSort` enum instead of
the string `order`.

## S4. A feed screen: home, trending, subscriptions, history  [today; history groups phase 2]

```cpp
void FeedView::load(Feed f)
{
    auto show = [this](const yt::Result<yt::Page<yt::VideoItem>> &r) { … same as S3 … };
    switch (f) {
    case Feed::Home:          yt.feeds().home(show); break;           // personal when signed in
    case Feed::Trending:      yt.feeds().trending(show); break;
    case Feed::Subscriptions: yt.feeds().subscriptions(show); break;  // AuthRequired when signed out
    case Feed::History:       yt.feeds().history(show); break;
    }
}
```

Today: `browse({"FEwhat_to_watch", "", ""}, …)`, `browse({"FEsubscriptions", …})` — the consumer
must know these ids; topic feeds are topic-channel ids listed by `topicFeeds()` in `catalog.h`.

*Implies:* named feeds; signed-in routing (TV client) stays internal; a feed that needs sign-in
fails with `ErrorCode::AuthRequired` instead of an empty page. Phase 2 adds `VideoItem::section`
(the "Today / Yesterday" group title of the history feed).

## S5. Open whatever the user pasted  [today]

A share target / "open link" box decides which screen to open — without a network call when the
URL says enough.

```cpp
std::optional<yt::Link> l = yt::Link::parse(text);
if (!l) return showError("not a YouTube link");
switch (l->kind) {
case yt::LinkKind::Video:    case yt::LinkKind::Short:
case yt::LinkKind::Live:     openVideo(l->id, l->startSecs); break;   // ?t= kept
case yt::LinkKind::Playlist: openPlaylist(l->id); break;
case yt::LinkKind::Channel:  case yt::LinkKind::Handle:
case yt::LinkKind::Custom:   openChannel(*l); break;                  // @x, /c/x, /user/x
}
```

`Link` is pure (no I/O) and every method also accepts the raw text, so `openChannel` can simply
call `yt.channels().get(text, …)`; handles and custom URLs are resolved inside.

*Implies:* `yt::Link` / `LinkKind` (R3); `channels().get` resolves handles (today:
`channelByUrl` is a separate method).

## S6. A channel page with tabs  [today: header + videos; phase 3: all tabs]

```cpp
yt.channels().get(link, [this](const yt::Result<yt::Channel> &r) {
    if (!r.ok()) return showError(r.error->message);
    header.show(r.value);                         // name, @handle, avatar, banner, subscribers, subscribed
    for (yt::ChannelTab t : r.value.tabs) tabBar.add(t);   // Videos, Shorts, Live, Playlists, Posts — what the server lists
});

void ChannelPage::openTab(yt::ChannelTab tab)
{
    switch (tab) {
    case yt::ChannelTab::Videos:    yt.channels().videos(id, showVideos); break;
    case yt::ChannelTab::Shorts:    yt.channels().shorts(id, showVideos); break;
    case yt::ChannelTab::Live:      yt.channels().streams(id, showVideos); break;
    case yt::ChannelTab::Playlists: yt.channels().playlists(id, showPlaylists); break;
    default: break;
    }
}
```

Today: `channel(id)` + `browse({id, "EgZ2aWRlb3PyBgQKAjoA", ""})` — the consumer would have to know
YouTube's protobuf tab params.

*Implies:* `ChannelTab` enum; tab params internal (phase 3 brings the full list).

## S7. A playlist, and "play next"  [today: playlist; phase 3: mixes and the watch queue]

```cpp
yt.playlists().get(link, [this](const yt::Result<yt::Playlist> &r) {
    if (!r.ok()) return showError(r.error->message);
    header.show(r.value);                          // title, owner, count
    list.append(r.value.entries.items);            // yt::Page<yt::PlaylistEntry>, first page included
    next = r.value.entries.next;
});
// more: yt.more(next, …) → Page<PlaylistEntry>
```

Mixes (`RD…`) and saved playlists look the same to the consumer; which client and browse form
serve them is internal (phase 3).

## S8. Comments: read, sort, reply, post  [today: list + post; phase 3: replies, edit, sort]

```cpp
yt::CommentOptions opt;
opt.sort = yt::CommentSort::Newest;
yt.comments().list(videoLink, opt, [this](const yt::Result<yt::Page<yt::CommentThread>> &r) {
    if (!r.ok()) return showError(r.error->message);
    if (r.value.disabled) return showNote("Comments are turned off");
    for (const yt::CommentThread &t : r.value.items)
        view.add(t.comment, t.replyCount);         // t is also the handle for replies / reply()
    next = r.value.next;
});

view.onExpand([this](const yt::CommentThread &t) { yt.comments().replies(t, showReplies); });
view.onReply ([this](const yt::CommentThread &t, std::string text) { yt.comments().reply(t, text, showPosted); });
view.onPost  ([this](std::string text) { yt.comments().post(videoLink, text, showPosted); });
```

`showPosted` receives the created `yt::Comment`, or an error whose message is the server's own
refusal text. Today: `comments(id, page)` + `postComment(page.createCommentParams, text)` — the
consumer carries a protobuf string from one call to the other.

*Implies:* `CommentThread` as an opaque handle (R4); `Page<T>::disabled` for "turned off".

## S9. Captions during playback  [today; translation phase 4]

```cpp
const yt::CaptionTrack *track = yt::captionFor(player, settings.subtitleLanguage);   // manual before ASR
if (track) yt.videos().captions(*track, [this](const yt::Result<std::vector<yt::Cue>> &r) {
    if (r.ok()) cues = r.value;
});

void onPosition(int64_t ms)
{
    const yt::Cue *c = yt::cueAt(cues, ms);         // binary search; nullptr between cues
    subtitleLabel.setText(c ? c->text : "");
}
```

*Implies:* `captionFor`, `cueAt` (R6). Phase 4 adds a `translateTo` field on the request.

## S10. Sign in with the QR, keep the session, sign out  [today]

```cpp
yt::ClientOptions o;
o.initial = loadState();                            // hl/gl, visitorData, refresh token, player build
o.onStateChanged = [](const yt::SessionState &s) { saveStateOwnerOnly(s); };
yt::Client yt(o);

signInJob = yt.auth().signIn(
    [this](const yt::DeviceCode &dc) {               // once; may arrive a few seconds after the call
        if (!dc.qrPng.empty()) qrImage.loadPng(dc.qrPng);
        codeLabel.setText(dc.userCode + "  —  " + dc.verificationUrl);
    },
    [this](const yt::Result<yt::Done> &r) {
        if (!r.ok()) return showError(r.error->message);   // denied / expired
        yt.account().info([this](const yt::Result<yt::Account> &a) { if (a.ok()) profile.show(a.value); });
    });

// Closing the dialog: signInJob.cancel();   Sign out: yt.auth().signOut();
```

A revoked token signs the client out on its own (`onStateChanged` reports it). Phase 2 adds
`yt.account().channels()` / `.switchTo(channel)` for brand accounts.

*Implies:* little change — this area is already close to the target; `account()` moves from
`Client::account` into the group.

## S11. Like, subscribe, bell, save to a playlist  [today: like/subscribe/save; phase 2: bell, save states]

```cpp
yt.account().rate(videoLink, yt::Rating::Like, done);          // Like, Dislike, None
yt.account().subscribe(channelLink, done);                      // .unsubscribe(channelLink, done)
yt.account().notifications(channelLink, yt::Bell::All, done);  // All, Personalized, None [phase 2]

// "Save to…" dialog: which of my playlists already contain the video, then toggle.
yt.account().saveTargets(videoLink, [this](const yt::Result<std::vector<yt::SaveTarget>> &r) {
    for (const yt::SaveTarget &t : r.value) dialog.addCheckbox(t.playlist.title, t.contains);  // [phase 2]
});
yt.account().save(videoLink, playlistLink, done);               // add
yt.account().unsave(entry, done);                               // remove: a PlaylistEntry handle
```

Today: `action(Action::Like, id)`, `editPlaylist(playlistId, /*add*/ false, setVideoId)` — a
boolean switch and an id whose meaning changes with it (removal needs the entry's setVideoId, which
the consumer must dig up).

*Implies:* `Rating` and `Bell` enums; add/remove as two verbs; removal by `PlaylistEntry` handle.

## S12. A watch page, and reporting the view to history  [today: page; phase 2: reporting, watch state]

```cpp
yt.videos().get(link, [this](const yt::Result<yt::Video> &r) {
    if (!r.ok()) return showError(r.error->message);
    const yt::Video &v = r.value;
    page.show(v.title, v.channel, v.viewCount, v.publishedText, v.description);
    page.setRating(v.myRating);                     // yt::Rating — Like / Dislike / None (signed in)
    page.setSubscribed(v.channel.subscribed);
    related.set(v.related.items);                   // yt::Page<yt::VideoItem>; more via yt.more
});
yt.videos().votes(link, showVotes);                 // Return YouTube Dislike counts

// [phase 2] History: one object per view, driven by the player's position.
yt::Viewing viewing = yt.videos().startViewing(player);   // sends the "playback" ping
media.onPosition([&](int64_t ms) { viewing.at(ms); });    // batched watch-time segments
media.onStopped ([&]() { viewing.finish(); });
```

*Implies:* `Video` (full page) vs `VideoItem` (list entry), `Rating` instead of the int
`likeStatus` (R7); a `Viewing` object owning the cpn and the segment batching (phase 2).

## S13. Living inside a GUI toolkit  [today]

MeeTube (Qt 4.7 on the N9), a Win32 or an iOS app: callbacks must arrive on the UI thread, and
closing a screen must guarantee no late callback.

```cpp
yt::ClientOptions o;
o.deliver = [](std::function<void()> fn) {          // run callbacks on the UI thread
    postToUiThread(std::move(fn));                    // Qt: a queued event; Win32: PostMessage; iOS: dispatch_async(main)
};
yt::Client yt(o);

class VideoPage : public QWidget {
    yt::Job m_job;
    void load(std::string link) { m_job = yt.videos().get(link, [this](const auto &r) { … }); }
    ~VideoPage() override { m_job.cancel(); }       // called on the UI thread: no callback after this
};
```

Single-threaded consumers (no threads at all) keep `ownThread = false` + `runOnce()`; there the
future form must not be used (nobody would pump the engine) — the header says so.

*Implies:* nothing new: today's threading contract is the right one and stays verbatim.

## S14. Advanced: transport, logging, client versions, raw requests  [today; raw request new]

```cpp
yt::ClientOptions o;
o.transport = [](const yt::TransportConfig &c) { return std::make_unique<MyWinHttpTransport>(c); };
o.log = [](yt::LogLevel, std::string_view category, std::string_view message) {
    std::fprintf(stderr, "[%.*s] %.*s\n", (int) category.size(), category.data(), (int) message.size(), message.data());
};
o.clients[yt::ClientId::TVHTML5] = {"TVHTML5", "7.20261101.00.00", tvUa, 7};   // YouTube moved on, no release needed
yt::Client yt(o);

// An endpoint the library doesn't wrap yet: context, auth and transport policy still apply.
yt.raw().post("feedback", yt::ClientId::WEB, R"({"feedbackTokens":["…"]})",
              [](const yt::Result<std::string> &r) { /* r.value: the JSON body, error envelope checked */ });
```

*Implies:* `raw()` (R8), documented as unstable; everything else exists.

---

## The API these scenarios imply

Header-style summary (names final only after review). C++17.

```cpp
namespace yt {

// ---- links (pure) ----
enum class LinkKind { Video, Short, Live, Playlist, Channel, Handle, Custom };
struct Link { LinkKind kind; std::string id; int64_t startSecs = 0;
              static std::optional<Link> parse(std::string_view text); };

// ---- lists ----
template <class T> class Cursor { public: explicit operator bool() const; /* opaque */ };
template <class T> struct Page { std::vector<T> items; Cursor<T> next; bool disabled = false; };

// ---- entities: list entries vs full pages ----
struct ChannelRef   { std::string id, name, avatarUrl; bool subscribed = false; };
struct VideoItem    { std::string id, title, thumbnailUrl, durationText, viewText, publishedText;
                      int64_t durationSecs = 0, viewCount = 0; bool isLive = false, isShort = false;
                      ChannelRef channel; };
enum class Rating   { None, Like, Dislike };
struct Video        { /* VideoItem's fields + */ std::string description; std::optional<int64_t> likeCount;
                      Rating myRating = Rating::None; Page<VideoItem> related; };
enum class ChannelTab { Videos, Shorts, Live, Playlists, Posts, Other };
struct Channel      { std::string id, name, handle, description, avatarUrl, bannerUrl, subscriberText;
                      bool subscribed = false; std::vector<ChannelTab> tabs; };
struct PlaylistEntry{ VideoItem video; /* + opaque removal handle */ };
struct Playlist     { std::string id, title, description; ChannelRef owner; int64_t videoCount = 0;
                      Page<PlaylistEntry> entries; };
struct Comment      { std::string id, text, publishedText, likeText; ChannelRef author; bool mine = false; };
struct CommentThread{ Comment comment; int64_t replyCount = 0; /* + opaque reply/replies handle */ };

// ---- choosing what to play (pure) ----
struct Preference { int maxHeight = 0; std::vector<std::string> videoCodecs, audioCodecs;
                    std::string audioLanguage; bool allowSeparate = true, allowHls = true; };
struct Choice { const Source *source = nullptr; const Format *progressive = nullptr,
                *video = nullptr, *audio = nullptr; std::string hls; };
Choice choose(const Player &, const Preference & = {});
const CaptionTrack *captionFor(const Player &, std::string_view language);
const Cue *cueAt(const std::vector<Cue> &, int64_t ms);

// ---- groups: each method has (args…, Callback<T>) → Job and (args…) → std::future<Result<T>> ----
class Videos    { get(link) → Video;  player(link[, unsigned want]) → Player;
                  captions(track) → std::vector<Cue>;  votes(link) → Votes; };
class Search    { videos(q[, SearchOptions]) → Page<VideoItem>;  channels(q) → Page<ChannelRef>;
                  playlists(q) → Page<Playlist>;  suggestions(prefix) → std::vector<std::string>; };
class Channels  { get(link) → Channel;  videos/shorts/streams(link) → Page<VideoItem>;
                  playlists(link) → Page<Playlist>; };
class Playlists { get(link) → Playlist; };
class Feeds     { home/trending/subscriptions/history() → Page<VideoItem>; };
class Comments  { list(link[, CommentOptions]) → Page<CommentThread>;  replies(thread) → Page<Comment>;
                  post(link, text) → Comment;  reply(thread, text) → Comment; };
class Account   { info() → Account;  rate(link, Rating);  subscribe/unsubscribe(link);
                  save(video, playlist);  unsave(entry);  [phase 2: notifications, saveTargets, channels] };
class Raw       { post(endpoint, ClientId, json) → std::string; };

class Client {
public:
    explicit Client(ClientOptions options = {});
    Videos &videos();  Search &search();  Channels &channels();  Playlists &playlists();
    Feeds &feeds();    Comments &comments();  Account &account();  Auth &auth();  Raw &raw();
    template <class T> Job more(const Cursor<T> &, Callback<Page<T>>);
    template <class T> std::future<Result<Page<T>>> more(const Cursor<T> &);
    void setLocale(std::string hl, std::string gl);
    void runOnce(int maxWaitMs);                    // ownThread == false only
};
}
```

What disappears from the public surface: `BrowseRequest`, `SearchRequest` (string `order`),
`VideoPage`/`PlaylistPage`/`ChannelPage`/`CommentPage` (→ `Page<T>`), `WatchResult` (→ `Video`),
`Action`, `editPlaylist(…, bool add, …)`, `createCommentParams`, `channelByUrl`,
`channelSubscribed` (→ `Channel::subscribed`), `Video::likeStatus`/`streamUrl`, every raw
continuation string.

---

## Decisions (owner, 2026-10-15)

1. **Groups as accessors** — `yt.videos().get(…)`, `yt.search().videos(…)`, not a flat client.
2. **Blocking form = `std::future` overloads** — every method also has a form without the callback
   returning `std::future<Result<T>>`; no lazy `Call<T>` type.
3. **`more(cursor)` lives on the client** — one `yt.more(page.next, …)` for every list.
4. **The signed-in group is `account()`.**
5. Kept as drafted: `Video` carries its first page of related videos and `Playlist` its first
   page of entries (as YouTube answers); selection helpers are free functions over plain value
   types (C++ Core Guidelines C.4).

## Usability check (before the headers are frozen)

Give a reader only `include/yt/*.h` (with their comments) and this file's *task list without the
code*; ask them to write S1, S3 and S11. Record where they hesitate or look for an id the API
should have given them. Redesign where most readers stumble (Cwalina: *"if the majority of
developers cannot implement one of the scenarios … the API should be redesigned"*). A fresh
agent with no repository access is an acceptable first reader; a human from the target audience
(MeeTube / Troubadour authors) is the real test.

## After approval

1. A plan (`docs/plans/<date>-api-groups.md`) implementing R2–R4 + R5 together as one breaking
   change, keeping today's chains and parsers underneath (the groups are thin wrappers over
   `core::fetch*`), with `examples/` rewritten as S1–S14.
2. R6/R7 (selection helpers, `VideoItem`/`Video`, enums) in the same release or right after.
3. MeeTube's bridge adapts once, to the final shape.
