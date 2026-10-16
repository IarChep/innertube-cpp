// RapidJSON DOM (copying parse, MemoryPoolAllocator).
#include "domx.h"
#include "rapidjson/document.h"
struct RJ {
    using V = const rapidjson::Value *;
    static V get(V v, std::string_view k) { if (!v || !v->IsObject()) return nullptr; auto it = v->FindMember(rapidjson::Value(rapidjson::StringRef(k.data(), k.size()))); return it == v->MemberEnd() ? nullptr : &it->value; }
    static bool isObj(V v) { return v && v->IsObject(); }
    static bool isStr(V v) { return v && v->IsString(); }
    static std::string_view str(V v) { return isStr(v) ? std::string_view(v->GetString(), v->GetStringLength()) : std::string_view(); }
    static bool i64(V v, int64_t &n) { if (v && v->IsInt64()) { n = v->GetInt64(); return true; } if (v && v->IsUint64()) { n = (int64_t) v->GetUint64(); return true; } return false; }
    template <class F> static void items(V v, F f) { if (!v || !v->IsArray()) return; for (auto &x : v->GetArray()) f(&x); }
    template <class F> static void members(V v, F f) { for (auto &m : v->GetObject()) f(std::string_view(m.name.GetString(), m.name.GetStringLength()), &m.value); }
    static V last(V v) { if (!v || !v->IsArray() || v->Empty()) return nullptr; return &(*v)[v->Size() - 1]; }
};
std::string extract(const std::string &doc, Kind k)
{
    kName = "rapidjson";
    rapidjson::Document d;
    d.Parse(doc.data(), doc.size());
    return domx::run<RJ>(static_cast<const rapidjson::Value *>(&d), k);
}
