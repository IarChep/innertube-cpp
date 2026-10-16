// libFuzzer target for yt::json (INNERTUBE_FUZZ=ON, Clang). No second parser as oracle:
// the document is its own. For any input the builder must not crash; for an accepted one,
// writing the tree back must reparse to the same text (a round trip through Writer),
// every value's raw() must reparse, and find() must visit every member exactly once.
#include <yt/json.h>
#include <cstdlib>
#include <string>
using namespace yt::json;

namespace {
void check(bool c) { if (!c) std::abort(); }

// Canonical text of v: members/items through the views, numbers and literals as raw text.
void write(Writer &w, Value v, size_t &members)
{
    if (v.isObject()) {
        w.beginObject();
        for (Member m : v.members()) {
            ++members;
            w.key(m.key);
            write(w, m.value, members);
        }
        w.endObject();
    } else if (v.isArray()) {
        w.beginArray();
        size_t n = 0;
        for (Value x : v.items()) {
            write(w, x, members);
            ++n;
        }
        check(n == v.size());
        w.endArray();
    } else if (v.isString()) {
        w.str(v.str());
    } else {
        w.raw(v.raw());
    }
    Doc again(v.raw());
    check(again.ok());
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
    Doc d(std::string_view((const char *) data, size));
    if (!d.ok()) return 0;
    Writer w;
    size_t members = 0;
    write(w, d.root(), members);
    Doc back(w.text());
    check(back.ok());
    Writer w2;
    size_t members2 = 0;
    write(w2, back.root(), members2);
    check(w2.text() == w.text() && members2 == members);
    // find() with every key that occurs: the callback sees each member once.
    size_t found = 0;
    std::string keys[4];
    int nk = 0;
    for (Member m : d.root().members())
        if (nk < 4) keys[nk++] = std::string(m.key);
    if (nk > 0) {
        size_t expect = 0;
        d.root().find({keys[0], keys[1 % nk], keys[2 % nk], keys[3 % nk]}, [&](std::string_view, Value) {
            ++found;
            return Visit::Descend;
        });
        // Count the same matches without find(): a naive recursive walk.
        auto count = [&](auto &&self, Value v) -> void {
            for (Member m : v.members()) {
                for (int i = 0; i < nk; ++i)
                    if (m.key == keys[i]) { ++expect; break; }
                self(self, m.value);
            }
            for (Value x : v.items()) self(self, x);
        };
        count(count, d.root());
        check(found == expect);
    }
    return 0;
}
