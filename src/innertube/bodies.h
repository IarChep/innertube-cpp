#ifndef YT_BODIES_H
#define YT_BODIES_H
// Serialized InnerTube POST bodies (WITHOUT the context block — the transport
// splices that in; see core::Http::post). One function per endpoint body
// shape so the Glaze include cost is paid once, in bodies.cpp: the requests see
// only std::string_view -> std::string.
//
// Every builder returns a minified JSON OBJECT ("{...}") — the transport's
// context splice relies on that.
#include <string_view>
#include <string>
namespace yt {
namespace bodies {
// {browseId[,params]} — the first page of a browse; later pages are continuation().
std::string browse(std::string_view browseId, std::string_view params);
std::string search(std::string_view query, std::string_view params);
std::string nextVideo(std::string_view videoId);
// {continuation} — the next page of any list (browse, search, next): the endpoint and the
// client of page 1 carry the rest.
std::string continuation(std::string_view token);
// tv=true adds playbackContext.contentPlaybackContext (html5Preference, and the
// signatureTimestamp when known) and the playback nonce: TVHTML5 needs them — without
// them an authed TV /player answers "UNPLAYABLE: The page needs to be reloaded". The
// mobile/vision clients stay minimal.
struct PlayerBody { bool tv = false; std::string cpn; int signatureTimestamp = 0; };
std::string player(std::string_view videoId, const PlayerBody &b = {});
std::string resolveUrl(std::string_view url);
std::string accountsList();                            // {accountReadMask:{returnOwner:true}}
std::string subscribeChannels(std::string_view channelId);
std::string likeTarget(std::string_view videoId);
// {createCommentParams, commentText} — a top-level comment post (create_comment).
std::string createComment(std::string_view createCommentParams, std::string_view text);
// add=true → ACTION_ADD_VIDEO (id = videoId); add=false → ACTION_REMOVE_VIDEO
// (id = the per-entry setVideoId position handle). playlistId is WL|LL|PL….
std::string editPlaylist(std::string_view playlistId, bool add, std::string_view id);
// {handoffQrParams:{rapidQrParams:{qrPresetStyle, userCode, rapidQrFeature}}} — the TV
// sign-in QR for a device code (mdx/handoff).
std::string mdxHandoff(std::string_view userCode);
}
}
#endif
