#include <doctest/doctest.h>
#include "fakehttp.h"
using yt::core::Reply;
TEST_CASE("queued reply is delivered only by flush()") {
    FakeHttp f; f.queue("browse", "{\"a\":1}");
    int calls = 0; Reply got;
    f.post("browse", yt::ClientId::WEB, "{\"browseId\":\"X\"}", yt::core::newJob(), [&](const Reply &r) { ++calls; got = r; });
    CHECK(calls == 0);
    CHECK(f.lastClientFor("browse") == (int)yt::ClientId::WEB);
    REQUIRE(f.sent.size() == 1);
    CHECK(f.sent[0] == "{\"browseId\":\"X\"}");
    f.flush();
    CHECK(calls == 1);
    CHECK(got.ok);
    CHECK(*got.body == "{\"a\":1}");
}
TEST_CASE("missing fixture fails; dead token is skipped") {
    FakeHttp f;
    int calls = 0; Reply got;
    f.post("next", yt::ClientId::WEB, "{}", yt::core::newJob(), [&](const Reply &r) { ++calls; got = r; });
    yt::core::JobToken dead = yt::core::newJob(); dead->canceled = true;
    f.post("next", yt::ClientId::WEB, "{}", dead, [&](const Reply &) { ++calls; });
    f.flush();
    CHECK(calls == 1);
    CHECK_FALSE(got.ok);
    CHECK(f.lastClientFor("player") == -1);
}
TEST_CASE("flush drains chained steps") {
    FakeHttp f; f.queue("a", "{}"); f.queue("b", "{\"step\":2}");
    std::string last;
    f.post("a", yt::ClientId::WEB, "{}", yt::core::newJob(), [&](const Reply &) {
        f.post("b", yt::ClientId::WEB, "{}", yt::core::newJob(), [&](const Reply &r) { last = *r.body; });
    });
    f.flush();
    CHECK(last == "{\"step\":2}");
}
TEST_CASE("status >= 400 fails with httpStatus, body kept; forms and gets recorded") {
    FakeHttp f; f.queue("browse", "{\"error\":{}}", 401);
    Reply got;
    f.post("browse", yt::ClientId::TVHTML5, "{}", yt::core::newJob(), [&](const Reply &r) { got = r; });
    f.queue("https://oauth.test/token", "{\"access_token\":\"A\"}");
    Reply form;
    f.postForm("https://oauth.test/token", {{"grant_type", "refresh_token"}}, yt::core::newJob(), [&](const Reply &r) { form = r; });
    f.setGetBody("{\"likes\":1}");
    Reply g;
    f.get("https://ryd.test/votes?videoId=x", yt::core::newJob(), [&](const Reply &r) { g = r; });
    f.flush();
    CHECK_FALSE(got.ok); CHECK(got.httpStatus == 401); CHECK(*got.body == "{\"error\":{}}");
    CHECK(form.ok);
    REQUIRE(f.sentForm.size() == 1);
    CHECK(f.sentForm[0].first == "https://oauth.test/token");
    CHECK(f.sentForm[0].second[0].second == "refresh_token");
    CHECK(g.ok); CHECK(f.lastGetUrl() == "https://ryd.test/votes?videoId=x");
    f.setBearer("B"); CHECK(f.session().bearer == "B");
}
TEST_CASE("fake adopts a queued visitorData on delivery") {
    FakeHttp t;
    t.queue("player", "{}", 200, "VD1");
    t.queue("player", "{}", 200, "VD2");
    int calls = 0;
    t.post("player", yt::ClientId::WEB, "{}", yt::core::newJob(), [&](const Reply &) { ++calls; });
    t.post("player", yt::ClientId::VISIONOS, "{}", yt::core::newJob(), [&](const Reply &) { ++calls; });
    t.flush();
    CHECK(calls == 2);
    CHECK(t.session().visitorData == "VD1");          // only an empty identity is seeded
    CHECK(t.sentClients == std::vector<int>{(int) yt::ClientId::WEB, (int) yt::ClientId::VISIONOS});
}
TEST_CASE("fake fetch replies from the url queue") {
    FakeHttp t;
    t.queue("https://www.youtube.com/tv", "<html></html>");
    Reply got;
    t.fetch("https://www.youtube.com/tv", {{"User-Agent", "UA"}}, yt::core::newJob(), [&](const Reply &r) { got = r; });
    t.flush();
    CHECK(got.ok);
    REQUIRE(t.fetched.size() == 1);
    CHECK(t.fetched[0].headers[0].value == "UA");
}
