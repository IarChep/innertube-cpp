#ifndef YT_RENDERERPARSER_H
#define YT_RENDERERPARSER_H
#include <yt/json.h>
#include <yt/types.h>
#include <string>
#include <vector>
namespace yt {
// Every function takes a json::Value: either a whole InnerTube response (a Reply's
// json(): parse*List / parseChannel / parseAccountsList / parseWatchPage) or the inner
// renderer object itself (parseVideoRenderer & co, parseText). Parsers never fail: an
// absent or unexpected value yields empty fields / an empty list.
std::string parseText(json::Value field);
VideoItem parseVideoRenderer(json::Value r);
// lockupViewModel (2024+ YouTube surfaces — watch-page related, some feeds/search —
// replaced compactVideoRenderer/gridVideoRenderer). Caller passes the inner object.
VideoItem parseLockupViewModel(json::Value lm);
// tileRenderer (TVHTML5 surfaces): the authed feeds (history/subscriptions/library)
// arrive TV-shaped because the OAuth bearer is only honored by the TV client.
VideoItem parseTileRenderer(json::Value t);
std::vector<VideoItem> parseVideoList(json::Value response, std::string *nextToken);
// createCommentParams (out, nullable): the create-comment box's submit token, scraped
// best-effort from the comments-section create-comment command (R4 — exact source
// unverified; left empty when absent). core::postComment reads it to post a comment.
std::vector<CommentThread> parseComments(json::Value response, std::string *nextToken,
                                 std::string *createCommentParams = nullptr);
Playlist parsePlaylistRenderer(json::Value r);
std::vector<Playlist> parsePlaylistList(json::Value response, std::string *nextToken);
// A playlist page (browse of VL<id>): the header — title, description, owner, video count,
// cover. Its entries are parseVideoList of the same response.
Playlist parsePlaylistPage(json::Value response);
// Channel header (c4TabbedHeaderRenderer / pageHeaderRenderer) → Channel.
Channel parseChannel(json::Value response);
// The viewer's subscribe state on a channel — the first subscribeButtonRenderer.subscribed
// anywhere (works for the WEB c4 header AND the TVHTML5 channelHeaderRenderer). Only an
// AUTHED (bearer/TV) browse carries it; anonymous responses omit it → false.
bool parseChannelSubscribed(json::Value response);
Channel parseUserRenderer(json::Value r);
std::vector<Channel> parseUserList(json::Value response, std::string *nextToken);
// account/accounts_list (authed) → the active account's identity. The channel id is
// reconstructed as "UC" + offlineCacheKeyToken.clientCacheKey (the youtubei.js trick —
// the response carries no plain channelId).
AccountInfo parseAccountsList(json::Value response);
// Watch page (/next): the primary video's details (title/description/views/likes +
// channel name/avatar/id) and the related-videos list, in one response. relatedToken:
// the related list's continuation (empty when there is none).
void parseWatchPage(json::Value response, Video *video, std::string *relatedToken = nullptr);
// navigation/resolve_url response → endpoint.browseEndpoint.browseId ("" when absent).
std::string parseResolvedBrowseId(json::Value response);
}
#endif
