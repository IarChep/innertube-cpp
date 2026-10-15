// The resource groups of yt::Client (client.h): thin wrappers that turn a link into an id
// and hand one chain to Engine::start. No request logic (core/chains.cpp) and no state —
// every group is a view of the one Engine.
#include "engine.h"
#include "core/chains.h"

namespace yt {

namespace {
using detail::Engine;

template <class T> using Chain = std::function<void(core::IHttp &, const core::JobToken &, Callback<T>)>;

template <class T> Job run(Engine *e, Chain<T> chain, Callback<T> done)
{
    return e->start<Result<T>>(std::move(chain), std::move(done));
}

template <class T> void fail(const Callback<T> &done, Error error)
{
    Result<T> r;
    r.error = std::move(error);
    done(r);
}

Job feed(Engine *e, std::string browseId, Callback<Page<VideoItem>> done)
{
    return run<Page<VideoItem>>(e, [browseId](core::IHttp &h, const core::JobToken &t, Callback<Page<VideoItem>> d) {
        core::VideoListSpec s;
        s.browseId = browseId;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job action(Engine *e, std::string videoLink, core::ActionKind kind, Callback<Done> done)
{
    return run<Done>(e, [videoLink, kind](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        const std::optional<std::string> id = core::videoIdOf(videoLink);
        if (!id) return fail(d, core::badLink(videoLink, "video"));
        core::submitAction(h, kind, *id, t, std::move(d));
    }, std::move(done));
}

Job edit(Engine *e, std::string videoLink, std::string playlistLink, core::PlaylistEdit what, Callback<Done> done)
{
    return run<Done>(e, [videoLink, playlistLink, what](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        const std::optional<std::string> video = core::videoIdOf(videoLink);
        if (!video) return fail(d, core::badLink(videoLink, "video"));
        const std::optional<std::string> list = core::playlistIdOf(playlistLink);
        if (!list) return fail(d, core::badLink(playlistLink, "playlist"));
        core::editPlaylist(h, *list, what, *video, t, std::move(d));
    }, std::move(done));
}
}

// ---- Videos ----

Job Videos::get(std::string link, Callback<Video> done)
{
    return run<Video>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Video> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchWatch(h, *id, t, std::move(d));
    }, std::move(done));
}

Job Videos::player(std::string link, Callback<Player> done)
{
    return player(std::move(link), WantAll, std::move(done));
}

Job Videos::player(std::string link, unsigned want, Callback<Player> done)
{
    return run<Player>(m_e, [link, want](core::IHttp &h, const core::JobToken &t, Callback<Player> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchPlayer(h, PlayerRequest{*id, want}, t, std::move(d));
    }, std::move(done));
}

Job Videos::captions(CaptionTrack track, Callback<std::vector<Cue>> done)
{
    return run<std::vector<Cue>>(m_e, [track](core::IHttp &h, const core::JobToken &t, Callback<std::vector<Cue>> d) {
        core::fetchCaptions(h, track, t, std::move(d));
    }, std::move(done));
}

Job Videos::votes(std::string link, Callback<Votes> done)
{
    return run<Votes>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Votes> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchDislikes(h, *id, t, std::move(d));
    }, std::move(done));
}

// ---- Search ----

Job Search::videos(std::string query, Callback<Page<VideoItem>> done)
{
    return videos(std::move(query), SearchOptions{}, std::move(done));
}

Job Search::videos(std::string query, SearchOptions options, Callback<Page<VideoItem>> done)
{
    return run<Page<VideoItem>>(m_e, [query, options](core::IHttp &h, const core::JobToken &t, Callback<Page<VideoItem>> d) {
        core::VideoListSpec s;
        s.kind = core::VideoListSpec::Search;
        s.query = query;
        s.sort = options.sort;
        core::fetchVideoList(h, s, t, std::move(d));
    }, std::move(done));
}

Job Search::channels(std::string query, Callback<Page<Channel>> done)
{
    return run<Page<Channel>>(m_e, [query](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchUserSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Search::playlists(std::string query, Callback<Page<Playlist>> done)
{
    return run<Page<Playlist>>(m_e, [query](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchPlaylistSearch(h, query, t, std::move(d));
    }, std::move(done));
}

Job Search::suggestions(std::string prefix, Callback<std::vector<std::string>> done)
{
    return run<std::vector<std::string>>(m_e,
        [prefix](core::IHttp &h, const core::JobToken &t, Callback<std::vector<std::string>> d) {
            core::fetchSearchSuggestions(h, prefix, t, std::move(d));
        }, std::move(done));
}

// ---- Channels ----

Job Channels::get(std::string link, Callback<Channel> done)
{
    return run<Channel>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Channel> d) {
        core::fetchChannel(h, link, t, std::move(d));
    }, std::move(done));
}

Job Channels::videos(std::string link, Callback<Page<VideoItem>> done)
{
    return run<Page<VideoItem>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<VideoItem>> d) {
        core::fetchChannelVideos(h, link, t, std::move(d));
    }, std::move(done));
}

Job Channels::playlists(std::string link, Callback<Page<Playlist>> done)
{
    return run<Page<Playlist>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchChannelPlaylists(h, link, t, std::move(d));
    }, std::move(done));
}

// ---- Playlists ----

Job Playlists::get(std::string link, Callback<Playlist> done)
{
    return run<Playlist>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Playlist> d) {
        const std::optional<std::string> id = core::playlistIdOf(link);
        if (!id) return fail(d, core::badLink(link, "playlist"));
        core::fetchPlaylist(h, *id, t, std::move(d));
    }, std::move(done));
}

// ---- Feeds ----

Job Feeds::home(Callback<Page<VideoItem>> done) { return feed(m_e, "FEwhat_to_watch", std::move(done)); }
Job Feeds::subscriptions(Callback<Page<VideoItem>> done) { return feed(m_e, "FEsubscriptions", std::move(done)); }
Job Feeds::history(Callback<Page<VideoItem>> done) { return feed(m_e, "FEhistory", std::move(done)); }
Job Feeds::topic(std::string id, Callback<Page<VideoItem>> done) { return feed(m_e, std::move(id), std::move(done)); }

// ---- Comments ----

Job Comments::list(std::string link, Callback<Page<CommentThread>> done)
{
    return run<Page<CommentThread>>(m_e, [link](core::IHttp &h, const core::JobToken &t, Callback<Page<CommentThread>> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::fetchComments(h, *id, t, std::move(d));
    }, std::move(done));
}

Job Comments::post(std::string link, std::string text, Callback<Done> done)
{
    return run<Done>(m_e, [link, text](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        const std::optional<std::string> id = core::videoIdOf(link);
        if (!id) return fail(d, core::badLink(link, "video"));
        core::postComment(h, *id, text, t, std::move(d));
    }, std::move(done));
}

// ---- Account ----

Job Account::info(Callback<AccountInfo> done)
{
    return run<AccountInfo>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<AccountInfo> d) {
        core::fetchAccount(h, t, std::move(d));
    }, std::move(done));
}

Job Account::rate(std::string videoLink, Rating rating, Callback<Done> done)
{
    const core::ActionKind kind = rating == Rating::Like ? core::Like
                                : rating == Rating::Dislike ? core::Dislike : core::RemoveLike;
    return action(m_e, std::move(videoLink), kind, std::move(done));
}

Job Account::subscribe(std::string channelLink, Callback<Done> done)
{
    return run<Done>(m_e, [channelLink](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::channelAction(h, channelLink, core::Subscribe, t, std::move(d));
    }, std::move(done));
}

Job Account::unsubscribe(std::string channelLink, Callback<Done> done)
{
    return run<Done>(m_e, [channelLink](core::IHttp &h, const core::JobToken &t, Callback<Done> d) {
        core::channelAction(h, channelLink, core::Unsubscribe, t, std::move(d));
    }, std::move(done));
}

Job Account::save(std::string videoLink, std::string playlistLink, Callback<Done> done)
{
    return edit(m_e, std::move(videoLink), std::move(playlistLink), core::AddVideo, std::move(done));
}

Job Account::unsave(std::string videoLink, std::string playlistLink, Callback<Done> done)
{
    return edit(m_e, std::move(videoLink), std::move(playlistLink), core::RemoveVideo, std::move(done));
}

Job Account::playlists(Callback<Page<Playlist>> done)
{
    return run<Page<Playlist>>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<Page<Playlist>> d) {
        core::fetchPlaylists(h, "FElibrary", "", t, std::move(d));
    }, std::move(done));
}

Job Account::subscriptions(Callback<Page<Channel>> done)
{
    return run<Page<Channel>>(m_e, [](core::IHttp &h, const core::JobToken &t, Callback<Page<Channel>> d) {
        core::fetchChannelList(h, "FEchannels", t, std::move(d));
    }, std::move(done));
}

// ---- the future forms: each is its callback form inside futureOf ----

std::future<Result<Video>> Videos::get(std::string link)
{
    return detail::futureOf<Video>([&](Callback<Video> cb) { return get(std::move(link), std::move(cb)); });
}
std::future<Result<Player>> Videos::player(std::string link)
{
    return detail::futureOf<Player>([&](Callback<Player> cb) { return player(std::move(link), std::move(cb)); });
}
std::future<Result<Player>> Videos::player(std::string link, unsigned want)
{
    return detail::futureOf<Player>([&](Callback<Player> cb) { return player(std::move(link), want, std::move(cb)); });
}
std::future<Result<std::vector<Cue>>> Videos::captions(CaptionTrack track)
{
    return detail::futureOf<std::vector<Cue>>([&](Callback<std::vector<Cue>> cb) { return captions(std::move(track), std::move(cb)); });
}
std::future<Result<Votes>> Videos::votes(std::string link)
{
    return detail::futureOf<Votes>([&](Callback<Votes> cb) { return votes(std::move(link), std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Search::videos(std::string query)
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return videos(std::move(query), std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Search::videos(std::string query, SearchOptions options)
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return videos(std::move(query), options, std::move(cb)); });
}
std::future<Result<Page<Channel>>> Search::channels(std::string query)
{
    return detail::futureOf<Page<Channel>>([&](Callback<Page<Channel>> cb) { return channels(std::move(query), std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Search::playlists(std::string query)
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return playlists(std::move(query), std::move(cb)); });
}
std::future<Result<std::vector<std::string>>> Search::suggestions(std::string prefix)
{
    return detail::futureOf<std::vector<std::string>>(
        [&](Callback<std::vector<std::string>> cb) { return suggestions(std::move(prefix), std::move(cb)); });
}
std::future<Result<Channel>> Channels::get(std::string link)
{
    return detail::futureOf<Channel>([&](Callback<Channel> cb) { return get(std::move(link), std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Channels::videos(std::string link)
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return videos(std::move(link), std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Channels::playlists(std::string link)
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return playlists(std::move(link), std::move(cb)); });
}
std::future<Result<Playlist>> Playlists::get(std::string link)
{
    return detail::futureOf<Playlist>([&](Callback<Playlist> cb) { return get(std::move(link), std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Feeds::home()
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return home(std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Feeds::subscriptions()
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return subscriptions(std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Feeds::history()
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return history(std::move(cb)); });
}
std::future<Result<Page<VideoItem>>> Feeds::topic(std::string id)
{
    return detail::futureOf<Page<VideoItem>>([&](Callback<Page<VideoItem>> cb) { return topic(std::move(id), std::move(cb)); });
}
std::future<Result<Page<CommentThread>>> Comments::list(std::string link)
{
    return detail::futureOf<Page<CommentThread>>([&](Callback<Page<CommentThread>> cb) { return list(std::move(link), std::move(cb)); });
}
std::future<Result<Done>> Comments::post(std::string link, std::string text)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return post(std::move(link), std::move(text), std::move(cb)); });
}
std::future<Result<AccountInfo>> Account::info()
{
    return detail::futureOf<AccountInfo>([&](Callback<AccountInfo> cb) { return info(std::move(cb)); });
}
std::future<Result<Done>> Account::rate(std::string videoLink, Rating rating)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return rate(std::move(videoLink), rating, std::move(cb)); });
}
std::future<Result<Done>> Account::subscribe(std::string channelLink)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return subscribe(std::move(channelLink), std::move(cb)); });
}
std::future<Result<Done>> Account::unsubscribe(std::string channelLink)
{
    return detail::futureOf<Done>([&](Callback<Done> cb) { return unsubscribe(std::move(channelLink), std::move(cb)); });
}
std::future<Result<Done>> Account::save(std::string videoLink, std::string playlistLink)
{
    return detail::futureOf<Done>(
        [&](Callback<Done> cb) { return save(std::move(videoLink), std::move(playlistLink), std::move(cb)); });
}
std::future<Result<Done>> Account::unsave(std::string videoLink, std::string playlistLink)
{
    return detail::futureOf<Done>(
        [&](Callback<Done> cb) { return unsave(std::move(videoLink), std::move(playlistLink), std::move(cb)); });
}
std::future<Result<Page<Playlist>>> Account::playlists()
{
    return detail::futureOf<Page<Playlist>>([&](Callback<Page<Playlist>> cb) { return playlists(std::move(cb)); });
}
std::future<Result<Page<Channel>>> Account::subscriptions()
{
    return detail::futureOf<Page<Channel>>([&](Callback<Page<Channel>> cb) { return subscriptions(std::move(cb)); });
}

}
