// yyjson DOM (immutable). INSITU=1: copy into a padded buffer and parse in place.
#include "domx.h"
#include "yyjson.h"
struct Y {
    using V = yyjson_val *;
    static V get(V v, std::string_view k) { return yyjson_is_obj(v) ? yyjson_obj_getn(v, k.data(), k.size()) : nullptr; }
    static bool isObj(V v) { return yyjson_is_obj(v); }
    static bool isStr(V v) { return yyjson_is_str(v); }
    static std::string_view str(V v) { return yyjson_is_str(v) ? std::string_view(yyjson_get_str(v), yyjson_get_len(v)) : std::string_view(); }
    static bool i64(V v, int64_t &n) { if (yyjson_is_sint(v)) { n = yyjson_get_sint(v); return true; } if (yyjson_is_uint(v)) { n = (int64_t) yyjson_get_uint(v); return true; } return false; }
    template <class F> static void items(V v, F f) { if (!yyjson_is_arr(v)) return; size_t i, m; V x; yyjson_arr_foreach(v, i, m, x) f(x); }
    template <class F> static void members(V v, F f) { size_t i, m; V k, x; yyjson_obj_foreach(v, i, m, k, x) f(std::string_view(yyjson_get_str(k), yyjson_get_len(k)), x); }
    static V last(V v) { return yyjson_is_arr(v) ? yyjson_arr_get_last(v) : nullptr; }
};
std::string extract(const std::string &doc, Kind k)
{
#if INSITU
    kName = "yyjson_insitu";
    std::string buf; buf.reserve(doc.size() + YYJSON_PADDING_SIZE);
    buf.assign(doc); buf.append(YYJSON_PADDING_SIZE, '\0');
    yyjson_doc *d = yyjson_read_opts(buf.data(), doc.size(), YYJSON_READ_INSITU, nullptr, nullptr);
#else
    kName = "yyjson";
    yyjson_doc *d = yyjson_read(doc.data(), doc.size(), 0);
#endif
    std::string r = domx::run<Y>(yyjson_doc_get_root(d), k);
    yyjson_doc_free(d);
    return r;
}
