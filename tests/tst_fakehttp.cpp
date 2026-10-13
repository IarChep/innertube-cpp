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
