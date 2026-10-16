#include "contextbuilder.h"
#include <yt/json.h>
#include "catalog.h"
namespace yt {

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
    const ClientInfo &ci = s.clients[(int) id];
    // The device fields each client identity sends; absent ones are omitted.
    const char *make = nullptr, *model = nullptr, *os = nullptr, *osVersion = nullptr, *platform = nullptr;
    int sdk = 0;
    if (id == ClientId::IOS) {
        make = "Apple"; model = "iPhone16,2"; os = "iOS"; osVersion = "18.0";
    } else if (id == ClientId::ANDROID) {
        sdk = 34; os = "Android"; osVersion = "14"; platform = "MOBILE";
    } else if (id == ClientId::ANDROID_VR) {
        sdk = 32; os = "Android"; osVersion = "12L"; make = "Oculus"; model = "Quest 3"; platform = "MOBILE";
    } else if (id == ClientId::VISIONOS) {
        make = "Apple"; model = "RealityDevice14,1"; os = "visionOS"; osVersion = "1.0.2.21O209";
    }
    json::Writer w;
    w.beginObject().key("client").beginObject()
        .member("clientName", ci.name).member("clientVersion", ci.version).member("hl", s.hl).member("gl", s.gl);
    if (make) w.member("deviceMake", make);
    if (model) w.member("deviceModel", model);
    if (os) w.member("osName", os).member("osVersion", osVersion);
    if (sdk) w.member("androidSdkVersion", sdk);
    if (platform) w.member("platform", platform);
    // Omit visitorData ONLY on IOS (see omitVisitor above): there a cross-client
    // visitorData is a SABR "distrust" signal that strips the hlsManifestUrl we play,
    // while on ANDROID_VR the visitorData is what CLEARS the anti-bot LOGIN_REQUIRED.
    if (!omitVisitor(id) && !s.visitorData.empty()) w.member("visitorData", s.visitorData);
    if (id == ClientId::VISIONOS) w.member("userAgent", ci.userAgent);   // VISIONOS repeats its UA
    // user + request: minimum-viable shape real clients send; harmless when
    // unneeded, but several endpoints behave better with it present (see
    // docs/INNERTUBE_API.md §5).
    w.endObject()
        .key("user").beginObject().member("lockedSafetyMode", false).endObject()
        .key("request").beginObject().member("useSsl", true)
            .key("internalExperimentFlags").beginArray().endArray().endObject()
        .endObject();
    return w.take();
}

std::vector<Header> ContextBuilder::headers(ClientId id, const Session &s) {
    const ClientInfo &ci = s.clients[(int) id];
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
