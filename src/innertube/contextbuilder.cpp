#include "contextbuilder.h"
#include "catalog.h"
#include "parsers/ytjson.h"
namespace yt {

// The context block, typed (Glaze reflection; nullopt fields are omitted).
namespace cj {

struct Client {
    std::string clientName;
    std::string clientVersion;
    std::string hl;
    std::string gl;
    std::optional<std::string> deviceMake;
    std::optional<std::string> deviceModel;
    std::optional<std::string> osName;
    std::optional<std::string> osVersion;
    std::optional<int> androidSdkVersion;
    std::optional<std::string> platform;
    std::optional<std::string> visitorData;
};
struct User {
    bool lockedSafetyMode = false;
};
struct Request {
    bool useSsl = true;
    std::vector<int> internalExperimentFlags;   // always the empty array
};
struct Context {
    Client client;
    User user;
    Request request;
};

} // namespace cj

// IOS is used ONLY for the anonymous /player and must stay "clean" (no visitorData /
// X-Goog-Visitor-Id) or YouTube returns a SABR-only player response without the
// hlsManifestUrl we play (device-verified). ANDROID_VR is the OPPOSITE since the
// 2026-07 bot wall (live-verified 2026-07-12 against the N9's own IP + TLS stack):
// a visitor-LESS request now hits a per-video "Sign in to confirm you're not a bot"
// LOGIN_REQUIRED, while the same request WITH the session's WEB-seeded visitorData
// returns OK carrying direct progressive URLs (itag-18 ranged GET → 206; no n-param,
// no pot) — serverAbrStreamingUrl coexists with the fetchable urls.
static bool omitVisitor(ClientId id) {
    return id == ClientId::IOS;
}

std::string ContextBuilder::contextJson(ClientId id, const Session &s) {
    const ClientInfo &ci = clientInfo(id);
    cj::Context ctx;
    ctx.client.clientName = ci.name;
    ctx.client.clientVersion = ci.version;
    ctx.client.hl = s.hl;
    ctx.client.gl = s.gl;
    if (id == ClientId::IOS) {
        ctx.client.deviceMake = "Apple"; ctx.client.deviceModel = "iPhone16,2";
        ctx.client.osName = "iOS"; ctx.client.osVersion = "18.0";
    } else if (id == ClientId::ANDROID) {
        ctx.client.androidSdkVersion = 34; ctx.client.osName = "Android"; ctx.client.osVersion = "14";
        ctx.client.platform = "MOBILE";
    } else if (id == ClientId::ANDROID_VR) {
        ctx.client.androidSdkVersion = 32; ctx.client.osName = "Android"; ctx.client.osVersion = "12L";
        ctx.client.deviceMake = "Oculus"; ctx.client.deviceModel = "Quest 3"; ctx.client.platform = "MOBILE";
    }
    // Omit visitorData ONLY on IOS (see omitVisitor above): there a cross-client
    // visitorData is a SABR "distrust" signal that strips the hlsManifestUrl we play,
    // while on ANDROID_VR the visitorData is what CLEARS the anti-bot LOGIN_REQUIRED.
    if (!omitVisitor(id) && !s.visitorData.empty())
        ctx.client.visitorData = s.visitorData;
    // user + request: minimum-viable shape real clients send; harmless when
    // unneeded, but several endpoints behave better with it present (see
    // docs/INNERTUBE_API.md §5).
    return glz::write_json(ctx).value_or(std::string("{}"));
}

std::vector<Header> ContextBuilder::headers(ClientId id, const Session &s) {
    const ClientInfo &ci = clientInfo(id);
    std::vector<Header> h;
    h.push_back({"Content-Type", "application/json"});
    h.push_back({"X-YouTube-Client-Name", std::to_string(ci.id)});
    h.push_back({"X-YouTube-Client-Version", ci.version});
    h.push_back({"User-Agent", ci.userAgent});
    // Consent cookie: without it, EU/consent-gated regions return empty feeds.
    h.push_back({"Cookie", Catalog::kConsentCookie});
    if (!omitVisitor(id) && !s.visitorData.empty())
        h.push_back({"X-Goog-Visitor-Id", s.visitorData});
    // Bearer ONLY on TVHTML5: the token is minted with the TV client credentials
    // and every other client rejects it with 400 INVALID_ARGUMENT — not just the
    // IOS/ANDROID /player (research §6.1) but also signed-in WEB /next, /browse and
    // comments (live-verified 2026-07-02, broke the whole watch page after login).
    // WEB therefore stays anonymous; authed surfaces all go through the TV client.
    if (!s.bearer.empty() && id == ClientId::TVHTML5)
        h.push_back({"Authorization", "Bearer " + s.bearer});
    return h;
}
}
