#include <doctest/doctest.h>
#include "core/log.h"
#include <string>
#include <vector>
TEST_CASE("YTLOG formats space-separated and reports category + level") {
    std::vector<std::string> got;
    yt::Logger log = [&](yt::LogLevel lvl, std::string_view c, std::string_view m) {
        CHECK(lvl == yt::LogLevel::Debug);
        got.push_back(std::string(c) + "|" + std::string(m));
    };
    YTLOG(log, "net") << "DONE" << 200 << "ms=" << 12;
    REQUIRE(got.size() == 1);
    CHECK(got[0] == "net|DONE 200 ms= 12");
}
TEST_CASE("YTLOG without a logger does not evaluate its arguments") {
    yt::Logger none;
    int evaluated = 0;
    YTLOG(none, "net") << ++evaluated;
    CHECK(evaluated == 0);
}
TEST_CASE("YTLOG is dangling-else safe") {
    yt::Logger none; bool elseRan = false;
    if (true) YTLOG(none, "x") << 1; else elseRan = true;
    CHECK_FALSE(elseRan);
}
