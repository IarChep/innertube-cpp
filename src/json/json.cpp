// yt::json — the index builder, the escape decoder, raw extents, find() and the writer.
// The builder is the only code that reads untrusted bytes structurally: one forward pass,
// no recursion, every byte read bounds-checked; the views trust the tape it produced.
#include <yt/json.h>
#include <charconv>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define YT_JSON_SSE2 1
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#define YT_JSON_NEON 1
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace yt::json {

namespace {

using namespace detail;

// Bytes that end a number or literal: structure or whitespace.
struct Delimiters {
    bool t[256] = {};
    Delimiters() { for (unsigned char c : {',', '}', ']', ':', ' ', '\n', '\r', '\t'}) t[c] = true; }
};
const Delimiters kDelim;

// Index of the lowest set bit (x != 0).
int lowestBit(uint64_t x)
{
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64(&i, x);
    return (int) i;
#else
    return __builtin_ctzll((unsigned long long) x);
#endif
}

bool isBlank(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHex(char c) { return isDigit(c) || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }

// The first '"', '\\' or control byte (invalid raw inside a string) at or after p; e if none.
// 83-91% of an InnerTube answer is string bytes: this loop is the parser's speed. SSE2 and
// NEON take 16 bytes a step; elsewhere one machine word (SWAR).
const char *stringStop(const char *p, const char *e)
{
#if defined(YT_JSON_SSE2)
    const __m128i quote = _mm_set1_epi8('"'), slash = _mm_set1_epi8('\\'), ctl = _mm_set1_epi8(0x1F);
    while (e - p >= 16) {
        const __m128i v = _mm_loadu_si128((const __m128i *) p);
        const __m128i low = _mm_cmpeq_epi8(_mm_min_epu8(v, ctl), v);          // v <= 0x1F
        const unsigned m = (unsigned) _mm_movemask_epi8(
            _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, quote), _mm_cmpeq_epi8(v, slash)), low));
        if (m) return p + lowestBit(m);
        p += 16;
    }
#elif defined(YT_JSON_NEON)
    const uint8x16_t quote = vdupq_n_u8('"'), slash = vdupq_n_u8('\\'), space = vdupq_n_u8(0x20);
    while (e - p >= 16) {
        const uint8x16_t v = vld1q_u8((const uint8_t *) p);
        const uint8x16_t m = vorrq_u8(vorrq_u8(vceqq_u8(v, quote), vceqq_u8(v, slash)), vcltq_u8(v, space));
        const uint8x8_t nib = vshrn_n_u16(vreinterpretq_u16_u8(m), 4);       // 4 bits per byte
        uint64_t bits;
        std::memcpy(&bits, &nib, 8);
        if (bits) return p + (lowestBit(bits) >> 2);
        p += 16;
    }
#else
    using W = uintptr_t;
    const W ones = (W) 0x0101010101010101ull, highs = ones << 7;
    while ((size_t)(e - p) >= sizeof(W)) {
        W w;
        std::memcpy(&w, p, sizeof w);
        const W a = w ^ (ones * '"'), b = w ^ (ones * '\\');
        // A zero byte of a or b, or a byte below 0x20, sets its high bit; the lowest flag is
        // exact (borrows only run upwards), so on little-endian it is the answer.
        const W z = (((a - ones) & ~a) | ((b - ones) & ~b) | (w - ones * 0x20)) & ~w & highs;
        if (z) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            return p + lowestBit((uint64_t) z) / 8;
#else
            break;
#endif
        }
        p += sizeof(W);
    }
#endif
    while (p < e && *p != '"' && *p != '\\' && (unsigned char) *p >= 0x20) ++p;
    return p;
}

// An RFC 8259 §6 number or a literal at p, followed by a delimiter or the end: its end, or
// nullptr when malformed.
const char *scalarEnd(const char *p, const char *e)
{
    auto literal = [&](const char *w, size_t n) -> const char * {
        return (size_t)(e - p) >= n && std::memcmp(p, w, n) == 0 ? p + n : nullptr;
    };
    const char *q;
    if (*p == 't') q = literal("true", 4);
    else if (*p == 'f') q = literal("false", 5);
    else if (*p == 'n') q = literal("null", 4);
    else {
        q = p;
        if (q < e && *q == '-') ++q;
        if (q < e && *q == '0') ++q;
        else if (q < e && *q >= '1' && *q <= '9') while (q < e && isDigit(*q)) ++q;
        else return nullptr;
        if (q < e && *q == '.') {
            if (++q >= e || !isDigit(*q)) return nullptr;
            while (q < e && isDigit(*q)) ++q;
        }
        if (q < e && (*q == 'e' || *q == 'E')) {
            ++q;
            if (q < e && (*q == '+' || *q == '-')) ++q;
            if (q >= e || !isDigit(*q)) return nullptr;
            while (q < e && isDigit(*q)) ++q;
        }
    }
    if (!q || (q < e && !kDelim.t[(unsigned char) *q])) return nullptr;
    return q;
}

// Past the closing quote of the string whose opening quote is at s (already validated).
const char *stringEnd(const char *s)
{
    const char *q = s + 1;
    while (*q != '"') q += *q == '\\' ? (q[1] == 'u' ? 6 : 2) : 1;
    return q + 1;
}

} // namespace

Doc::Doc(std::string_view json) : m_src(json) { build(); }

Doc::Doc(std::shared_ptr<const std::string> body) : m_body(std::move(body))
{
    if (m_body) m_src = *m_body;
    build();
}

void Doc::build()
{
    const char *const b = m_src.data(), *p = b, *const e = b + m_src.size();
    if (m_src.size() >= kMaxSize) return;
    // ~15 bytes per token in InnerTube answers: a tenth of the size rarely grows.
    m_tape.resize(m_src.size() / 10 + 16);
    uint32_t *t = m_tape.data();
    size_t cap = m_tape.size(), n = 0;
    uint32_t openAt[kMaxDepth];
    bool inObject[kMaxDepth];
    int depth = 0;
    // What the grammar allows next.
    enum : uint8_t { Val, KeyOrEnd, Key, Colon, CommaOrEnd, ValOrEnd, Done } st = Val;
    auto fail = [&] { m_tape.clear(); m_tape.shrink_to_fit(); };
    for (;;) {
        if (p >= e) break;
        if ((unsigned char) *p <= ' ') {
            while (p < e && isBlank(*p)) ++p;
            if (p >= e) break;
        }
        if (cap - n < 2) {
            m_tape.resize(cap += cap / 2);
            t = m_tape.data();
        }
        const char c = *p;
        const uint32_t o = (uint32_t)(p - b);
        switch (c) {
        case '"': {
            uint32_t kind;
            if (st == KeyOrEnd || st == Key) { kind = kKey; st = Colon; }
            else if (st == Val || st == ValOrEnd) { kind = kString; st = CommaOrEnd; }
            else return fail();
            const char *q = p + 1;
            for (;;) {
                q = stringStop(q, e);
                if (q >= e) return fail();
                if (*q == '"') break;
                if (*q != '\\' || e - q < 2) return fail();   // a raw control byte, or cut short
                switch (q[1]) {
                case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
                    q += 2;
                    break;
                case 'u':
                    if (e - q < 6 || !isHex(q[2]) || !isHex(q[3]) || !isHex(q[4]) || !isHex(q[5]))
                        return fail();
                    q += 6;
                    break;
                default:
                    return fail();
                }
                kind |= kFlag;
            }
            t[n++] = kind | o;
            p = q + 1;
            break;
        }
        case ':':
            if (st != Colon) return fail();
            ++p;
            st = Val;
            break;
        case ',':
            if (st != CommaOrEnd) return fail();
            ++p;
            st = inObject[depth - 1] ? Key : Val;
            break;
        case '{': case '[':
            if (st != Val && st != ValOrEnd) return fail();
            if (depth == kMaxDepth) return fail();
            inObject[depth] = c == '{';
            openAt[depth++] = (uint32_t) n;
            t[n++] = kOpen | (c == '{' ? (uint32_t) kFlag : 0u) | o;
            t[n++] = 0;
            ++p;
            st = c == '{' ? KeyOrEnd : ValOrEnd;
            break;
        case '}': case ']':
            if (depth == 0 || inObject[depth - 1] != (c == '}')) return fail();
            if (st != CommaOrEnd && st != (c == '}' ? KeyOrEnd : ValOrEnd)) return fail();
            t[openAt[--depth] + 1] = (uint32_t) n;
            ++p;
            st = CommaOrEnd;
            break;
        default:
            if (st != Val && st != ValOrEnd) return fail();
            t[n++] = kScalar | o;
            if (!(p = scalarEnd(p, e))) return fail();
            st = CommaOrEnd;
            break;
        }
        if (depth == 0 && st == CommaOrEnd) {
            st = Done;
            break;
        }
    }
    while (p < e && isBlank(*p)) ++p;
    if (st != Done || p != e) return fail();
    m_tape.resize(n);
}

std::string_view Doc::decodeAt(uint32_t i) const
{
    if (auto it = m_decoded.find(i); it != m_decoded.end()) return it->second;
    auto hex = [](const char *h) {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v = (v << 4) | (uint32_t)(isDigit(h[k]) ? h[k] - '0' : (h[k] | 0x20) - 'a' + 10);
        return v;
    };
    std::string out;
    auto utf8 = [&](uint32_t cp) {
        if (cp < 0x80) {
            out += (char) cp;
            return;
        }
        if (cp < 0x800) out += (char)(0xC0 | (cp >> 6));
        else {
            if (cp < 0x10000) out += (char)(0xE0 | (cp >> 12));
            else {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
            }
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
        }
        out += (char)(0x80 | (cp & 0x3F));
    };
    // The builder validated every escape: this loop trusts the syntax.
    for (const char *p = m_src.data() + off(i) + 1; *p != '"';) {
        if (*p != '\\') {
            out += *p++;
            continue;
        }
        const char x = p[1];
        p += 2;
        switch (x) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
            uint32_t cp = hex(p);
            p += 4;
            if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                const uint32_t lo = hex(p + 2);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
            }
            utf8(cp);                                    // a lone surrogate stays (WTF-8)
            break;
        }
        default: out += x;                               // \" \\ \/
        }
    }
    return m_decoded.emplace(i, std::move(out)).first->second;
}

size_t Value::size() const
{
    size_t n = 0;
    if (isArray())
        for (uint32_t i = m_i + 2, end = m_d->m_tape[m_i + 1]; i < end; i = m_d->next(i)) ++n;
    else if (isObject())
        for (uint32_t i = m_i + 2, end = m_d->m_tape[m_i + 1]; i < end; i = m_d->next(i + 1)) ++n;
    return n;
}

bool Value::toInt(int64_t &out) const
{
    if (!isNumber()) return false;
    const char *s = m_d->m_src.data() + (word() & kOffMask), *e = m_d->m_src.data() + m_d->m_src.size();
    const std::from_chars_result r = std::from_chars(s, e, out);
    return r.ec == std::errc() && (r.ptr == e || kDelim.t[(unsigned char) *r.ptr]);
}

bool Value::toDouble(double &out) const
{
    if (!isNumber()) return false;
    // strtod would follow the C locale's decimal point; the bytes are ASCII JSON.
    const char *s = m_d->m_src.data() + (word() & kOffMask), *e = m_d->m_src.data() + m_d->m_src.size();
    return std::from_chars(s, scalarEnd(s, e), out).ec == std::errc();
}

std::string_view Value::raw() const
{
    if (!m_d) return {};
    const Doc &d = *m_d;
    const char *const src = d.m_src.data(), *const e = src + d.m_src.size();
    // The tape keeps no end offsets: go down the chain of last children to a leaf (or an
    // empty container), take its end, then climb back over one close bracket per level.
    // (Using the next token's offset instead returned the outer bracket for "[[]   ]".)
    uint32_t cur = m_i;
    int levels = 0;
    const char *p;
    for (;;) {
        const uint32_t w = d.m_tape[cur];
        const char *s = src + (w & kOffMask);
        if ((w & kKindMask) == kScalar) {
            p = scalarEnd(s, e);
            break;
        }
        if ((w & kKindMask) != kOpen) {
            p = stringEnd(s);
            break;
        }
        ++levels;
        const bool obj = (w & kFlag) != 0;
        uint32_t lastChild = 0;
        for (uint32_t i = cur + 2, end = d.m_tape[cur + 1]; i < end; i = d.next(obj ? i + 1 : i))
            lastChild = obj ? i + 1 : i;
        if (!lastChild) {
            p = s + 1;
            break;
        }
        cur = lastChild;
    }
    for (; levels > 0; --levels) {
        while (isBlank(*p)) ++p;
        ++p;                                             // the validated close bracket
    }
    const char *s = src + (word() & kOffMask);
    return std::string_view(s, (size_t)(p - s));
}

void Value::find(std::initializer_list<std::string_view> keys,
                 const std::function<Visit(std::string_view, Value)> &f) const
{
    if (!m_d || (word() & kKindMask) != kOpen) return;
    const uint32_t *t = m_d->m_tape.data();
    const char *src = m_d->m_src.data();
    // First-byte filter: most keys are rejected without a comparison.
    uint32_t first[8] = {};
    for (std::string_view k : keys) {
        const unsigned c = k.empty() ? '"' : (unsigned char) k[0];
        first[c >> 5] |= 1u << (c & 31);
    }
    // ends[]: the tape end of every container being walked, innermost last.
    uint32_t ends[kMaxDepth + 1];
    int sp = 0;
    ends[sp++] = t[m_i + 1];
    uint32_t i = m_i + 2;
    while (sp > 0) {
        if (i >= ends[sp - 1]) {
            --sp;
            continue;
        }
        const uint32_t w = t[i];
        const uint32_t kind = w & kKindMask;
        if (kind == kOpen) {
            ends[sp++] = t[i + 1];
            i += 2;
            continue;
        }
        if (kind != kKey) {
            ++i;
            continue;
        }
        const unsigned c = (unsigned char) src[(w & kOffMask) + 1];
        bool hit = false;
        std::string_view key;
        if ((first[c >> 5] & (1u << (c & 31))) || (w & kFlag))
            for (std::string_view k : keys)
                if (m_d->keyIs(i, k)) {
                    hit = true;
                    key = k;
                    break;
                }
        if (!hit) {
            ++i;
            continue;
        }
        switch (f(key, Value(m_d, i + 1))) {
        case Visit::Descend: ++i; break;
        case Visit::Skip: i = m_d->next(i + 1); break;
        case Visit::SkipRest: i = ends[sp - 1]; break;
        case Visit::Stop: return;
        }
    }
}

// ---- Writer ------------------------------------------------------------------------------
void Writer::comma()
{
    if (m_comma) m_out += ',';
}

Writer &Writer::open(char c)
{
    comma();
    m_out += c;
    m_comma = false;
    return *this;
}

Writer &Writer::close(char c)
{
    m_out += c;
    m_comma = true;
    return *this;
}

Writer &Writer::key(std::string_view k)
{
    str(k);
    m_out += ':';
    m_comma = false;
    return *this;
}

Writer &Writer::str(std::string_view s)
{
    comma();
    m_out += '"';
    for (char ch : s) {
        const unsigned char u = (unsigned char) ch;
        switch (ch) {
        case '"': m_out += "\\\""; break;
        case '\\': m_out += "\\\\"; break;
        case '\b': m_out += "\\b"; break;
        case '\f': m_out += "\\f"; break;
        case '\n': m_out += "\\n"; break;
        case '\r': m_out += "\\r"; break;
        case '\t': m_out += "\\t"; break;
        default:
            if (u < 0x20) {
                static const char kHex[] = "0123456789abcdef";
                m_out += "\\u00";
                m_out += kHex[u >> 4];
                m_out += kHex[u & 15];
            } else {
                m_out += ch;
            }
        }
    }
    m_out += '"';
    m_comma = true;
    return *this;
}

Writer &Writer::num(int64_t n)
{
    comma();
    char buf[24];
    m_out.append(buf, (size_t)(std::to_chars(buf, buf + sizeof buf, n).ptr - buf));
    m_comma = true;
    return *this;
}

Writer &Writer::boolean(bool b)
{
    comma();
    m_out += b ? "true" : "false";
    m_comma = true;
    return *this;
}

Writer &Writer::raw(std::string_view json)
{
    comma();
    m_out += json;
    m_comma = true;
    return *this;
}

} // namespace yt::json
