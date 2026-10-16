// simdjson DOM. Kernel picked at runtime from $SJ_IMPL (haswell / fallback ...).
#include "domx.h"
#include "simdjson.h"
using namespace simdjson;
struct SJ {
    struct V { dom::element e; bool ok = false; explicit operator bool() const { return ok; } };
    static V get(V v, std::string_view k) { if (!v.ok || !v.e.is_object()) return {}; auto r = dom::object(v.e).at_key(k); if (r.error()) return {}; return {r.value_unsafe(), true}; }
    static bool isObj(V v) { return v.ok && v.e.is_object(); }
    static bool isStr(V v) { return v.ok && v.e.is_string(); }
    static std::string_view str(V v) { return isStr(v) ? std::string_view(v.e.get_string().value_unsafe()) : std::string_view(); }
    static bool i64(V v, int64_t &n) { if (!v.ok) return false; if (v.e.is_int64()) { n = v.e.get_int64().value_unsafe(); return true; } if (v.e.is_uint64()) { n = (int64_t) v.e.get_uint64().value_unsafe(); return true; } return false; }
    template <class F> static void items(V v, F f) { if (!v.ok || !v.e.is_array()) return; for (dom::element x : dom::array(v.e)) f(V{x, true}); }
    template <class F> static void members(V v, F f) { for (dom::key_value_pair kv : dom::object(v.e)) f(kv.key, V{kv.value, true}); }
    static V last(V v) { if (!v.ok || !v.e.is_array()) return {}; dom::array a(v.e); size_t n = a.size(); if (!n) return {}; return {a.at(n - 1).value_unsafe(), true}; }
};
std::string extract(const std::string &doc, Kind k)
{
    static bool init = [] { if (const char *i = getenv("SJ_IMPL")) get_active_implementation() = get_available_implementations()[i]; return true; }();
    (void) init;
    static std::string name = std::string("simdjson_dom_") + get_active_implementation()->name();
    kName = name.c_str();
    dom::parser p;                           // a fresh parser per document, like a reply
    padded_string ps(doc);
    dom::element root = p.parse(ps).value_unsafe();
    return domx::run<SJ>(SJ::V{root, true}, k);
}
