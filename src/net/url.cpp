#include "net/url.h"
#include <vector>
namespace yt::net {
namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

struct Parts {
    std::string scheme;                 // lower-cased, "" if none
    bool hasAuthority = false; std::string_view authority;
    std::string_view path;
    bool hasQuery = false; std::string_view query;
    bool hasFragment = false; std::string_view fragment;
};

// RFC 3986 Appendix B split.
Parts split(std::string_view u) {
    Parts p;
    size_t i = 0;
    const size_t colon = u.find(':');
    const size_t firstDelim = u.find_first_of("/?#");
    if (colon != std::string_view::npos && (firstDelim == std::string_view::npos || colon < firstDelim) && colon > 0) {
        bool ok = true;
        for (size_t k = 0; k < colon; ++k) {
            const char c = u[k];
            const bool alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
            if (!(alpha || (k > 0 && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')))) { ok = false; break; }
        }
        if (ok) { for (size_t k = 0; k < colon; ++k) p.scheme += lower(u[k]); i = colon + 1; }
    }
    if (u.substr(i, 2) == "//") {
        p.hasAuthority = true;
        const size_t e = u.find_first_of("/?#", i + 2);
        p.authority = u.substr(i + 2, (e == std::string_view::npos ? u.size() : e) - (i + 2));
        i = (e == std::string_view::npos) ? u.size() : e;
    }
    const size_t pe = u.find_first_of("?#", i);
    p.path = u.substr(i, (pe == std::string_view::npos ? u.size() : pe) - i);
    i = (pe == std::string_view::npos) ? u.size() : pe;
    if (i < u.size() && u[i] == '?') {
        const size_t qe = u.find('#', i);
        p.hasQuery = true;
        p.query = u.substr(i + 1, (qe == std::string_view::npos ? u.size() : qe) - (i + 1));
        i = (qe == std::string_view::npos) ? u.size() : qe;
    }
    if (i < u.size() && u[i] == '#') { p.hasFragment = true; p.fragment = u.substr(i + 1); }
    return p;
}

// RFC 3986 §5.2.4.
std::string removeDots(std::string_view in) {
    std::vector<std::string_view> out;
    const bool absolute = !in.empty() && in.front() == '/';
    size_t i = absolute ? 1 : 0;
    bool trailingSlash = false;
    while (i <= in.size()) {
        size_t e = in.find('/', i);
        if (e == std::string_view::npos) e = in.size();
        const std::string_view seg = in.substr(i, e - i);
        trailingSlash = false;
        if (seg == ".") { trailingSlash = true; }
        else if (seg == "..") { if (!out.empty()) out.pop_back(); trailingSlash = true; }
        else out.push_back(seg);
        if (e == in.size()) break;
        i = e + 1;
    }
    std::string r = absolute ? "/" : "";
    for (size_t k = 0; k < out.size(); ++k) { if (k) r += '/'; r += out[k]; }
    if (trailingSlash && !r.empty() && r.back() != '/') r += '/';
    return r;
}

std::string merge(const Parts &base, std::string_view refPath) {
    if (base.hasAuthority && base.path.empty()) return "/" + std::string(refPath);
    const size_t slash = base.path.rfind('/');
    if (slash == std::string_view::npos) return std::string(refPath);
    return std::string(base.path.substr(0, slash + 1)) + std::string(refPath);
}

std::string join(const std::string &scheme, bool hasAuth, std::string_view auth, const std::string &path,
                 bool hasQuery, std::string_view query, bool hasFrag, std::string_view frag) {
    std::string r;
    if (!scheme.empty()) { r += scheme; r += ':'; }
    if (hasAuth) { r += "//"; r += auth; }
    r += path;
    if (hasQuery) { r += '?'; r += query; }
    if (hasFrag) { r += '#'; r += frag; }
    return r;
}
}

std::string schemeOf(std::string_view url) {
    const Parts p = split(url);
    return (p.scheme == "http" || p.scheme == "https") ? p.scheme : std::string();
}

std::string resolveLocation(std::string_view base, std::string_view location) {
    const Parts b = split(base), r = split(location);
    if (!r.scheme.empty())
        return join(r.scheme, r.hasAuthority, r.authority, removeDots(r.path), r.hasQuery, r.query, r.hasFragment, r.fragment);
    if (r.hasAuthority)
        return join(b.scheme, true, r.authority, removeDots(r.path), r.hasQuery, r.query, r.hasFragment, r.fragment);
    if (r.path.empty()) {
        return join(b.scheme, b.hasAuthority, b.authority, std::string(b.path),
                    r.hasQuery ? true : b.hasQuery, r.hasQuery ? r.query : b.query, r.hasFragment, r.fragment);
    }
    const std::string path = (r.path.front() == '/') ? removeDots(r.path) : removeDots(merge(b, r.path));
    return join(b.scheme, b.hasAuthority, b.authority, path, r.hasQuery, r.query, r.hasFragment, r.fragment);
}
}
