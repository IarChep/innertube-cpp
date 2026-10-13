#include <doctest/doctest.h>
#include <yt/version.h>
#include <string>
TEST_CASE("version string is semver") {
    CHECK(std::string(yt::versionString()) == "0.1.0");
}
