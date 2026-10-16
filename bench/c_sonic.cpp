// sonic-cpp DOM (x86 AVX2 / arm64 NEON only).
#include "domx.h"
#include "sonic/sonic.h"
struct SO {
    using V = const sonic_json::Node *;
    static V get(V v, std::string_view k) { if (!v || !v->IsObject()) return nullptr; auto it = v->FindMember(k); return it == v->MemberEnd() ? nullptr : &it->value; }
    static bool isObj(V v) { return v && v->IsObject(); }
    static bool isStr(V v) { return v && v->IsString(); }
    static std::string_view str(V v) { return isStr(v) ? v->GetStringView() : std::string_view(); }
    static bool i64(V v, int64_t &n) { if (v && v->IsInt64()) { n = v->GetInt64(); return true; } if (v && v->IsUint64()) { n = (int64_t) v->GetUint64(); return true; } return false; }
    template <class F> static void items(V v, F f) { if (!v || !v->IsArray()) return; for (auto it = v->Begin(); it != v->End(); ++it) f(&*it); }
    template <class F> static void members(V v, F f) { for (auto it = v->MemberBegin(); it != v->MemberEnd(); ++it) f(it->name.GetStringView(), &it->value); }
    static V last(V v) { if (!v || !v->IsArray() || v->Empty()) return nullptr; return &(*v)[v->Size() - 1]; }
};
std::string extract(const std::string &doc, Kind k)
{
    kName = "sonic";
    sonic_json::Document d;
    d.Parse(doc.data(), doc.size());
    return domx::run<SO>(static_cast<const sonic_json::Node *>(&d), k);
}
