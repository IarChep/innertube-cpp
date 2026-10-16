// yt::json: the validating index (grammar, escapes, numbers, depth), lazy views, find()
// and the writer. Built at C++17: the component promises to stay portable.
#include <doctest/doctest.h>
#include <yt/json.h>
#include <memory>
#include <string>
#include <vector>
using namespace yt::json;

TEST_CASE("pathsAndAbsence") {
    Doc d(R"({"a":{"b":[1,{"c":"x"}]},"n":null,"t":true})");
    REQUIRE(d.ok());
    CHECK(d["a"]["b"][1]["c"].str() == "x");
    CHECK(d.root().path("a", "b").size() == 2);
    CHECK(d["a"]["b"].last()["c"].str() == "x");
    // Absence propagates through any step, including a step of the wrong type.
    CHECK_FALSE(d["missing"]["b"][3]["c"]);
    CHECK(d["a"]["b"]["c"].str().empty());
    CHECK(d["a"]["b"][0]["c"].str().empty());
    CHECK(d["n"].isNull());
    CHECK(d["t"].boolean());
    CHECK(d["a"].boolean(true));                  // not a bool: the fallback
}

TEST_CASE("membersAndItemsInDocumentOrder") {
    Doc d(R"({"z":1,"a":2,"m":[3,4,5]})");
    std::string keys;
    for (Member m : d.root().members()) keys += m.key;
    CHECK(keys == "zam");
    int64_t sum = 0;
    for (Value v : d["m"].items()) sum += v.int64();
    CHECK(sum == 12);
    CHECK(d["m"].size() == 3);
    CHECK(d.root().size() == 3);
    for (Value v : d["z"].items()) FAIL("a number has no items " << v.raw());
}

TEST_CASE("duplicateKeysFirstWins") {
    Doc d(R"({"k":1,"k":2})");
    CHECK(d["k"].int64() == 1);
}

TEST_CASE("stringsDecodeOnceAndStayValid") {
    Doc d(R"({"plain":"abc","esc":"a\"b\\c\/d\n\u00e9\ud83d\ude00","lone":"\ud800x"})");
    REQUIRE(d.ok());
    CHECK(d["plain"].str() == "abc");
    CHECK(d["plain"].str().data() == d.text().data() + 10);   // a view into the body
    CHECK(d["esc"].str() == "a\"b\\c/d\n\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(d["esc"].str().data() == d["esc"].str().data());     // decoded once, then reused
    CHECK(d["lone"].str() == "\xED\xA0\x80x");                  // WTF-8, as documented
}

TEST_CASE("escapedKeysMatchTheirValue") {
    Doc d(R"({"\u0061b":1,"c\"d":2})");
    CHECK(d["ab"].int64() == 1);
    CHECK(d["c\"d"].int64() == 2);
    CHECK_FALSE(d["a"]);                          // a prefix of a key is not the key
}

TEST_CASE("numbers") {
    Doc d(R"([0,-12,9223372036854775807,9223372036854775808,1.5,2e3,-0.25])");
    REQUIRE(d.ok());
    int64_t v = 0;
    CHECK((d.root()[0].toInt(v) && v == 0));
    CHECK(d.root()[1].int64() == -12);
    CHECK(d.root()[2].int64() == INT64_MAX);
    CHECK_FALSE(d.root()[3].toInt(v));            // out of range: not an int
    CHECK_FALSE(d.root()[4].toInt(v));            // a fraction is not an int
    CHECK(d.root()[3].int64(7) == 7);
    double x = 0;
    CHECK((d.root()[4].toDouble(x) && x == 1.5));
    CHECK((d.root()[5].toDouble(x) && x == 2000));
    CHECK((d.root()[6].toDouble(x) && x == -0.25));
    CHECK(Doc(R"(["1"])").root()[0].int64(5) == 5);   // a string is not a number
}

TEST_CASE("acceptsEveryValueKind") {
    for (const char *ok : {"1", "\"s\"", "true", "null", "[]", "{}", " [ 1 , [ ] , { } ] ",
                           "{\"a\":[{\"b\":{}}]}", "-0", "0.5e+10", "\"\\u0000\""})
        CHECK_MESSAGE(Doc(ok).ok(), ok);
}

// A selection of JSONTestSuite's n_ cases: each must leave the document absent.
TEST_CASE("rejectsMalformed") {
    for (const char *bad : {"", " ", "[1,]", "{\"a\":1,}", "[01]", "[1.]", "[.5]", "[1e]", "[-]",
                            "[+1]", "[NaN]", "[Infinity]", "[tru]", "[nul]", "{\"a\" 1}", "{a:1}",
                            "{\"a\":1 \"b\":2}", "[1 2]", "[\"a\tb\"]", "[\"\\x\"]", "[\"\\u12\"]",
                            "[\"abc", "{\"a\":1", "[1]]", "[1] x", "{\"a\":1}{}", "[\"\x01\"]",
                            "{,}", "[,1]", ":", "\"a\":1"}) {
        Doc d(bad);
        CHECK_MESSAGE(!d.ok(), bad);
        CHECK_FALSE(d.root());
        CHECK(d["a"].str().empty());
    }
}

// A crafted answer nested deeper than kMaxDepth is refused whole (not walked recursively).
TEST_CASE("depthBound") {
    const std::string ok(kMaxDepth, '['), okEnd(kMaxDepth, ']');
    CHECK(Doc(ok + okEnd).ok());
    const std::string deep(kMaxDepth + 1, '['), deepEnd(kMaxDepth + 1, ']');
    CHECK_FALSE(Doc(deep + deepEnd).ok());
    const std::string huge(1000000, '[');
    CHECK_FALSE(Doc(huge).ok());
}

// raw() of a trailing nested container used to return the enclosing bracket too.
TEST_CASE("rawIsTheExactText") {
    Doc d("{ \"a\" : [ [ ]   ] , \"s\":\"x\\\"y\", \"n\": -1.5e3 , \"o\":{\"k\":{}} }");
    REQUIRE(d.ok());
    CHECK(d["a"].raw() == "[ [ ]   ]");
    CHECK(d["a"][0].raw() == "[ ]");
    CHECK(d["s"].raw() == "\"x\\\"y\"");
    CHECK(d["n"].raw() == "-1.5e3");
    CHECK(d["o"].raw() == "{\"k\":{}}");
    CHECK(d.root().raw() == d.text().substr(0, d.text().size()));
    CHECK(Value().raw().empty());
}

TEST_CASE("findWalksInDocumentOrder") {
    Doc d(R"({"r":{"x":1},"l":[{"r":{"x":2,"r":{"x":3}}},{"q":{"r":{"x":4}}}]})");
    REQUIRE(d.ok());
    std::vector<int64_t> seen;
    SUBCASE("Skip does not look inside a match") {
        d.root().find({"r"}, [&](std::string_view, Value v) { seen.push_back(v["x"].int64()); return Visit::Skip; });
        CHECK(seen == std::vector<int64_t>{1, 2, 4});
    }
    SUBCASE("Descend also visits matches inside a match") {
        d.root().find({"r"}, [&](std::string_view, Value v) { seen.push_back(v["x"].int64()); return Visit::Descend; });
        CHECK(seen == std::vector<int64_t>{1, 2, 3, 4});
    }
    SUBCASE("SkipRest leaves the rest of the holding object") {
        Doc e(R"({"o":{"r":{"x":1},"r2":{"r":{"x":9}}},"p":{"r":{"x":2}}})");
        e.root().find({"r"}, [&](std::string_view, Value v) { seen.push_back(v["x"].int64()); return Visit::SkipRest; });
        CHECK(seen == std::vector<int64_t>{1, 2});
    }
    SUBCASE("Stop ends the walk at the first match") {
        d.root().find({"r"}, [&](std::string_view, Value v) { seen.push_back(v["x"].int64()); return Visit::Stop; });
        CHECK(seen == std::vector<int64_t>{1});
    }
    SUBCASE("several keys, the matched key is reported") {
        std::string keys;
        d.root().find({"q", "x"}, [&](std::string_view k, Value) { keys += k; return Visit::Descend; });
        CHECK(keys == "xxxqx");
    }
}

TEST_CASE("findIsIterative") {
    std::string s;
    for (int i = 0; i < kMaxDepth - 1; ++i) s += "{\"k\":";
    s += "1";
    for (int i = 0; i < kMaxDepth - 1; ++i) s += "}";
    Doc d(s);
    REQUIRE(d.ok());
    int n = 0;
    d.root().find({"k"}, [&](std::string_view, Value) { ++n; return Visit::Descend; });
    CHECK(n == kMaxDepth - 1);
}

TEST_CASE("sharedBodyOutlivesTheCaller") {
    std::unique_ptr<Doc> d;
    {
        auto body = std::make_shared<const std::string>(R"({"a":"kept"})");
        d = std::make_unique<Doc>(body);
    }
    CHECK((*d)["a"].str() == "kept");
    CHECK_FALSE(Doc(std::shared_ptr<const std::string>()).ok());
}

TEST_CASE("writer") {
    Writer w;
    w.beginObject()
        .member("s", "a\"b\\c\n\t\x01\x1f\x7f\xD0\xBA")
        .member("i", (int64_t) INT64_MIN)
        .member("b", false)
        .key("arr").beginArray().num(1).str("x").beginObject().endObject().endArray()
        .key("o").beginObject().member("k", 2).endObject()
        .key("r").raw("[true]")
        .endObject();
    CHECK(w.text() == "{\"s\":\"a\\\"b\\\\c\\n\\t\\u0001\\u001f\x7f\xD0\xBA\",\"i\":-9223372036854775808,"
                      "\"b\":false,\"arr\":[1,\"x\",{}],\"o\":{\"k\":2},\"r\":[true]}");
    Doc back(w.text());
    REQUIRE(back.ok());
    CHECK(back["s"].str() == "a\"b\\c\n\t\x01\x1f\x7f\xD0\xBA");
    CHECK(Writer().beginArray().endArray().take() == "[]");
}
