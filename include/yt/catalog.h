// Static catalog data: InnerTube client identities (overridable per Client — YouTube
// rotates versions and a consumer must not wait for a library release), the anonymous
// topic feeds and the search types. Labels are English defaults; UIs may relabel. C++17.
#ifndef YT_CATALOG_H
#define YT_CATALOG_H
#include <string>
#include <vector>
namespace yt {

// VISIONOS is the JS-less sweet spot for /player: ready adaptive URLs (no n, no
// PoToken), HLS and SABR, and its sessions are not cut at ~60 s like ANDROID_VR's and
// IOS's; it needs the session visitorData. ANDROID_VR adds the progressive itag 18.
enum class ClientId { WEB, WEB_SAFARI, IOS, ANDROID, ANDROID_VR, TVHTML5, VISIONOS };
constexpr int kClientIdCount = 7;

struct ClientInfo { std::string name, version, userAgent; int id = 0; };   // id = X-YouTube-Client-Name
ClientInfo defaultClientInfo(ClientId id);

struct FeedEntry { std::string label, id; bool requiresAuth = false; };
struct SearchOrder { std::string label, value; };
struct SearchType { std::string label, kind; std::vector<SearchOrder> orders; };

std::vector<FeedEntry> topicFeeds();      // anonymous topic feeds known to return video lists
std::vector<FeedEntry> feedSections();    // the Home strip
std::vector<SearchType> searchTypes();    // Videos / Channels / Playlists with their sort orders

}
#endif
