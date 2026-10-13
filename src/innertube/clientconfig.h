#ifndef YT_CLIENTCONFIG_H
#define YT_CLIENTCONFIG_H
namespace yt {
// ANDROID_VR is the research-recommended "JS-less sweet spot": returns ready stream
// URLs with no po_token and no signature decipher. The LEAD anonymous /player client
// (see core::fetchPlayer); needs the session visitorData since the 2026-07 bot wall.
enum class ClientId { WEB, WEB_SAFARI, IOS, ANDROID, ANDROID_VR, TVHTML5 };
struct ClientInfo { const char *name; const char *version; const char *userAgent; int id; };
const ClientInfo &clientInfo(ClientId id);
}
#endif
