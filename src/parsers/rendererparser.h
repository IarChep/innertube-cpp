#ifndef YT_RENDERERPARSER_H
#define YT_RENDERERPARSER_H
#include <string>
#include <string>
#include <string_view>
#include <yt/types.h>
namespace yt {
// Every function takes raw JSON bytes: either a whole InnerTube response
// (parse*List / parseChannel / parseAccountsList / parseWatchPage) or the inner
// renderer object itself (parseVideoRenderer & co, parseText). The buffer only
// needs to stay alive for the duration of the call.
std::string parseText(std::string_view field);
Video parseVideoRenderer(std::string_view r);
// lockupViewModel (2024+ YouTube surfaces — watch-page related, some feeds/search —
// replaced compactVideoRenderer/gridVideoRenderer). Caller passes the inner object.
Video parseLockupViewModel(std::string_view lm);
// tileRenderer (TVHTML5 surfaces): the authed feeds (history/subscriptions/library)
// arrive TV-shaped because the OAuth bearer is only honored by the TV client.
Video parseTileRenderer(std::string_view t);
std::vector<Video> parseVideoList(std::string_view response, std::string *nextToken);
// createCommentParams (out, nullable): the create-comment box's submit token, scraped
// best-effort from the comments-section create-comment command (R4 — exact source
// unverified; left empty when absent). Threaded into CommentPage so the model can post.
std::vector<Comment> parseComments(std::string_view response, std::string *nextToken,
                                 std::string *createCommentParams = 0);
Playlist parsePlaylistRenderer(std::string_view r);
std::vector<Playlist> parsePlaylistList(std::string_view response, std::string *nextToken);
// Channel header (c4TabbedHeaderRenderer / pageHeaderRenderer) → Channel.
// The response is a whole document; the const std::string & overload reads via
// the NUL-terminated sentinel path (kInDoc) — production passes *r.body.
Channel parseChannel(std::string_view response);
Channel parseChannel(const std::string &response);
// The viewer's subscribe state on a channel — the first subscribeButtonRenderer.subscribed
// anywhere (works for the WEB c4 header AND the TVHTML5 channelHeaderRenderer). Only an
// AUTHED (bearer/TV) browse carries it; anonymous responses omit it → false.
bool parseChannelSubscribed(std::string_view response);
Channel parseUserRenderer(std::string_view r);
std::vector<Channel> parseUserList(std::string_view response, std::string *nextToken);
// account/accounts_list (authed) → the active account's identity. The channel id is
// reconstructed as "UC" + offlineCacheKeyToken.clientCacheKey (the youtubei.js trick —
// the response carries no plain channelId).
Account parseAccountsList(std::string_view response);
// Watch page (/next): the primary video's details (title/description/views/likes +
// channel name/avatar/id) and the related-videos list, in one response.
void parseWatchPage(std::string_view response, Video *primary, std::vector<Video> *related);
// navigation/resolve_url response → endpoint.browseEndpoint.browseId ("" when absent).
std::string parseResolvedBrowseId(std::string_view response);
}
#endif
