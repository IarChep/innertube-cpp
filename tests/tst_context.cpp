// The youtubei `context` block + per-client request headers (port of MeeTube's
// tst_meetube_context).
#include <doctest/doctest.h>
#include "innertube/contextbuilder.h"
#include <yt/session.h>
using namespace yt;

static bool has(const std::string &hay, const char *needle) { return hay.find(needle) != std::string::npos; }
static const Header *find(const std::vector<Header> &h, const char *name) {
    for (const Header &x : h) if (x.name == name) return &x;
    return nullptr;
}

TEST_CASE("iosContext") {
    Session s; s.visitorData = "VD";
    const std::string c = ContextBuilder::contextJson(ClientId::IOS, s);
    CHECK(has(c, "\"clientName\":\"IOS\""));
    CHECK(has(c, "\"clientVersion\":\"20.49.6\""));
    CHECK(has(c, "\"hl\":\"en\""));
    CHECK_FALSE(has(c, "\"visitorData\""));          // IOS stays clean (HLS manifest)
    CHECK(has(c, "\"user\":{\"lockedSafetyMode\":false}"));
    CHECK(has(c, "\"useSsl\":true"));
    CHECK(has(c, "\"internalExperimentFlags\":[]"));
    CHECK(has(c, "\"deviceMake\":\"Apple\""));
}
TEST_CASE("playerClientsOmitVisitorData") {
    Session s; s.visitorData = "VD";
    CHECK(has(ContextBuilder::contextJson(ClientId::ANDROID, s), "\"visitorData\":\"VD\""));
    CHECK_FALSE(has(ContextBuilder::contextJson(ClientId::IOS, s), "\"visitorData\""));
    CHECK(has(ContextBuilder::contextJson(ClientId::ANDROID_VR, s), "\"visitorData\":\"VD\""));
    CHECK(find(ContextBuilder::headers(ClientId::IOS, s), "X-Goog-Visitor-Id") == nullptr);
    const std::vector<Header> vrh = ContextBuilder::headers(ClientId::ANDROID_VR, s);
    const Header *vr = find(vrh, "X-Goog-Visitor-Id");
    REQUIRE(vr != nullptr);
    CHECK(vr->value == "VD");
}
TEST_CASE("androidVrDeviceIdentity") {
    Session s;
    CHECK(has(ContextBuilder::contextJson(ClientId::ANDROID_VR, s), "\"deviceMake\":\"Oculus\""));
}
TEST_CASE("webContextHasUserRequest") {
    Session s;
    const std::string c = ContextBuilder::contextJson(ClientId::WEB, s);
    CHECK((has(c, "\"user\":") && has(c, "\"request\":")));
    CHECK(has(c, "\"clientVersion\":\"2.20260626.01.00\""));
    CHECK_FALSE(has(c, "\"visitorData\""));
    CHECK_FALSE(has(c, "\"deviceMake\""));
    CHECK_FALSE(has(c, "\"androidSdkVersion\""));
}
TEST_CASE("headersHaveConsentCookieOnEveryClient") {
    Session s;
    for (ClientId id : {ClientId::WEB, ClientId::WEB_SAFARI, ClientId::IOS, ClientId::ANDROID,
                        ClientId::ANDROID_VR, ClientId::TVHTML5}) {
        const std::vector<Header> h = ContextBuilder::headers(id, s);
        const Header *c = find(h, "Cookie");
        REQUIRE(c != nullptr);
        CHECK(c->value == "SOCS=CAISAiAD");
    }
}
TEST_CASE("iosHeaders") {
    Session s;
    const std::vector<Header> h = ContextBuilder::headers(ClientId::IOS, s);
    REQUIRE(find(h, "X-YouTube-Client-Name") != nullptr);
    CHECK(find(h, "X-YouTube-Client-Name")->value == "5");
    CHECK(find(h, "X-YouTube-Client-Version")->value == "20.49.6");
    CHECK(find(h, "Content-Type")->value == "application/json");
}
TEST_CASE("bearerWhenSet") {
    Session s; s.bearer = "tok";
    const std::vector<Header> h = ContextBuilder::headers(ClientId::TVHTML5, s);
    const Header *a = find(h, "Authorization");
    REQUIRE(a != nullptr);
    CHECK(a->value == "Bearer tok");
}
TEST_CASE("bearerOnlyOnTv") {
    Session s; s.bearer = "tok";
    CHECK(find(ContextBuilder::headers(ClientId::TVHTML5, s), "Authorization") != nullptr);
    for (ClientId id : {ClientId::WEB, ClientId::WEB_SAFARI, ClientId::IOS, ClientId::ANDROID, ClientId::ANDROID_VR})
        CHECK(find(ContextBuilder::headers(id, s), "Authorization") == nullptr);
}
TEST_CASE("localeFromSession") {
    Session s; s.hl = "ru"; s.gl = "RU";
    const std::string c = ContextBuilder::contextJson(ClientId::WEB, s);
    CHECK(has(c, "\"hl\":\"ru\""));
    CHECK(has(c, "\"gl\":\"RU\""));
}
TEST_CASE("SessionState defaults") {
    SessionState st;
    CHECK(st.hl == "en"); CHECK(st.gl == "US");
    CHECK(st.refreshToken.empty()); CHECK_FALSE(st.account.has_value());
}
