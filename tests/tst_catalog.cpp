#include <doctest/doctest.h>
#include <yt/catalog.h>
#include "innertube/contextbuilder.h"
using namespace yt;
TEST_CASE("topic feeds") {
    const std::vector<FeedEntry> t = topicFeeds();
    REQUIRE(t.size() == 6);
    CHECK(t[0].id == "FEnews_destination");
    CHECK(t[0].label == "News");
    CHECK(t[5].id == "UCEgdi0XIXXZ-qJOFPf4JSKw");
}
TEST_CASE("default client identities") {
    CHECK(defaultClientInfo(ClientId::IOS).version == "20.49.6");
    CHECK(defaultClientInfo(ClientId::IOS).id == 5);
    CHECK(defaultClientInfo(ClientId::TVHTML5).name == "TVHTML5");
}
TEST_CASE("a client override reaches the context and headers") {
    Session s;
    s.clients[(int)ClientId::TVHTML5] = ClientInfo{"TVHTML5", "9.0", "UA-X", 7};
    const std::string c = ContextBuilder::contextJson(ClientId::TVHTML5, s);
    CHECK(c.find("\"clientVersion\":\"9.0\"") != std::string::npos);
    bool ver = false, ua = false;
    for (const Header &h : ContextBuilder::headers(ClientId::TVHTML5, s)) {
        if (h.name == "X-YouTube-Client-Version" && h.value == "9.0") ver = true;
        if (h.name == "User-Agent" && h.value == "UA-X") ua = true;
    }
    CHECK(ver); CHECK(ua);
    CHECK(ContextBuilder::contextJson(ClientId::WEB, s).find("\"clientVersion\":\"2.20260626.01.00\"") != std::string::npos);
}
TEST_CASE("visionos identity") {
    const ClientInfo v = defaultClientInfo(ClientId::VISIONOS);
    CHECK(v.name == "VISIONOS");
    CHECK(v.version == "1.02");
    CHECK(v.id == 101);
    CHECK(v.userAgent.find("Version/26.0 Safari") != std::string::npos);
    CHECK(kClientIdCount == 7);
}
