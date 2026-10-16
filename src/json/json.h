// yt::json — read JSON over an unmodified buffer, and write small JSON. The bottom of the
// layering: std only, no YouTube knowledge, C++17, so every parser and the transport read
// through it and it can leave the project as is.
//
// Reading is one validating pass that builds an index (the "tape"), then cheap views read
// lazily. A malloc per JSON node is what ruled out a DOM on the N9; re-skipping siblings
// at every level is what ruled out lazy views without an index (measured 13x slower than
// this). Design and measurements: docs/specs/2026-10-16-json-design.md.
//
// Tape: one u32 per token, two per object/array. Word = kind (2 bits) | flag (1 bit) |
// offset (29 bits). The flag marks a string/key that has escapes, or an object (vs array).
// An object/array's second word is the tape index just past its matching close, so any
// subtree is skipped in O(1). Strings are never copied unless they have escapes.
#ifndef YT_JSON_JSON_H
#define YT_JSON_JSON_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yt::json {

// Nesting bound. Real InnerTube answers nest under 50 levels; a deeper document is refused
// whole, so a crafted answer cannot exhaust a 1 GB-RAM phone. Every walk is iterative.
inline constexpr int kMaxDepth = 128;
// Offsets are 29 bits: a larger document is refused.
inline constexpr size_t kMaxSize = (size_t) 1 << 29;

// What find() does after its callback saw a match.
enum class Visit {
    Descend,    // also look inside the matched value
    Skip,       // skip the matched value
    SkipRest,   // skip the matched value and the rest of the object that holds it
    Stop,       // end the walk (the first match is enough)
};

namespace detail {
enum : uint32_t {
    kScalar = 0u << 30,
    kString = 1u << 30,
    kKey = 2u << 30,
    kOpen = 3u << 30,
    kKindMask = 3u << 30,
    kFlag = 1u << 29,
    kOffMask = kFlag - 1,
};
}

class Doc;
class Items;
class Members;

// A value inside a Doc, or absent. Absence propagates: v["a"]["b"].str() is "" when any
// step is missing or has another type, so a path needs no checks between its steps.
// Two words, copied by value; valid while its Doc lives.
class Value {
public:
    Value() = default;
    explicit operator bool() const { return m_d != nullptr; }
    bool isObject() const;
    bool isArray() const;
    bool isString() const;
    bool isNumber() const;
    bool isBool() const;
    bool isNull() const;

    Value operator[](std::string_view key) const;   // the first member named key
    Value operator[](size_t index) const;           // O(index): iterate with items()
    template <class... K> Value path(K... keys) const
    {
        Value v = *this;
        ((v = v[keys]), ...);
        return v;
    }
    Value last() const;                              // the last array element
    size_t size() const;                             // elements or members; 0 otherwise

    std::string_view str() const;                    // a string's value; "" otherwise
    std::string string() const { return std::string(str()); }
    bool toInt(int64_t &out) const;                  // an integral number that fits
    int64_t int64(int64_t fallback = 0) const;
    bool toDouble(double &out) const;
    bool boolean(bool fallback = false) const;
    std::string_view raw() const;                    // the value's exact JSON text

    Items items() const;                             // array elements, in order
    Members members() const;                         // object members, in document order

    // Every member below this value whose key is one of `keys`, in document order; `f`
    // decides how the walk goes on. Iterative: depth cannot overflow the C stack.
    void find(std::initializer_list<std::string_view> keys,
              const std::function<Visit(std::string_view key, Value value)> &f) const;

private:
    friend class Doc;
    friend class Items;
    friend class Members;
    Value(const Doc *d, uint32_t i) : m_d(d), m_i(i) {}
    uint32_t word() const;
    char lead() const;
    const Doc *m_d = nullptr;
    uint32_t m_i = 0;
};

struct Member {
    std::string_view key;
    Value value;
};

class Items {
public:
    class iterator {
    public:
        Value operator*() const { return Value(m_d, m_i); }
        iterator &operator++();
        bool operator!=(const iterator &o) const { return m_i != o.m_i; }
    private:
        friend class Items;
        iterator(const Doc *d, uint32_t i) : m_d(d), m_i(i) {}
        const Doc *m_d;
        uint32_t m_i;
    };
    iterator begin() const { return iterator(m_d, m_begin); }
    iterator end() const { return iterator(m_d, m_end); }
private:
    friend class Value;
    Items(const Doc *d, uint32_t b, uint32_t e) : m_d(d), m_begin(b), m_end(e) {}
    const Doc *m_d;
    uint32_t m_begin, m_end;
};

class Members {
public:
    class iterator {
    public:
        Member operator*() const;
        iterator &operator++();
        bool operator!=(const iterator &o) const { return m_i != o.m_i; }
    private:
        friend class Members;
        iterator(const Doc *d, uint32_t i) : m_d(d), m_i(i) {}
        const Doc *m_d;
        uint32_t m_i;
    };
    iterator begin() const { return iterator(m_d, m_begin); }
    iterator end() const { return iterator(m_d, m_end); }
private:
    friend class Value;
    Members(const Doc *d, uint32_t b, uint32_t e) : m_d(d), m_begin(b), m_end(e) {}
    const Doc *m_d;
    uint32_t m_begin, m_end;
};

// An indexed JSON document. Validates RFC 8259 grammar, numbers, literals, escapes and raw
// control bytes, nesting <= kMaxDepth, size < kMaxSize; anything else leaves it !ok() and
// every lookup absent. UTF-8 is not validated: bytes pass through as sent, and a lone
// \uD800 escape decodes as WTF-8. Read from one thread at a time (escaped strings decode
// into the Doc on first read). Not copyable or movable: Values point at it.
class Doc {
public:
    explicit Doc(std::string_view json);                       // the caller keeps json alive
    explicit Doc(std::shared_ptr<const std::string> body);      // the Doc keeps body alive
    Doc(const Doc &) = delete;
    Doc &operator=(const Doc &) = delete;

    bool ok() const { return !m_tape.empty(); }
    Value root() const { return ok() ? Value(this, 0) : Value(); }
    Value operator[](std::string_view key) const { return root()[key]; }
    std::string_view text() const { return m_src; }
    size_t indexBytes() const { return m_tape.capacity() * sizeof(uint32_t); }

private:
    friend class Value;
    friend class Items;
    friend class Members;
    uint32_t off(uint32_t i) const { return m_tape[i] & detail::kOffMask; }
    uint32_t kind(uint32_t i) const { return m_tape[i] & detail::kKindMask; }
    uint32_t next(uint32_t i) const { return kind(i) == detail::kOpen ? m_tape[i + 1] : i + 1; }
    std::string_view strAt(uint32_t i) const;
    std::string_view decodeAt(uint32_t i) const;
    bool keyIs(uint32_t i, std::string_view k) const;
    void build();

    std::shared_ptr<const std::string> m_body;
    std::string_view m_src;
    std::vector<uint32_t> m_tape;
    mutable std::unordered_map<uint32_t, std::string> m_decoded;   // escaped strings, by index
};

// Compact JSON output (no spaces), for request bodies and saved state. Strings are escaped
// per RFC 8259 §7 (\" \\ \b \f \n \r \t, other bytes below 0x20 as \u00XX); everything else,
// UTF-8 included, is written as is. Commas are placed automatically.
class Writer {
public:
    Writer &beginObject() { return open('{'); }
    Writer &endObject() { return close('}'); }
    Writer &beginArray() { return open('['); }
    Writer &endArray() { return close(']'); }
    Writer &key(std::string_view k);
    Writer &str(std::string_view s);
    Writer &num(int64_t n);
    Writer &boolean(bool b);
    Writer &raw(std::string_view json);              // a value that is already JSON text
    // Shorthands for one member.
    Writer &member(std::string_view k, std::string_view s) { return key(k).str(s); }
    Writer &member(std::string_view k, const char *s) { return key(k).str(s); }
    Writer &member(std::string_view k, int64_t n) { return key(k).num(n); }
    Writer &member(std::string_view k, int n) { return key(k).num(n); }
    Writer &member(std::string_view k, bool b) { return key(k).boolean(b); }
    const std::string &text() const { return m_out; }
    std::string take() { return std::move(m_out); }

private:
    Writer &open(char c);
    Writer &close(char c);
    void comma();
    std::string m_out;
    bool m_comma = false;                            // a value was written at this level
};

// ---- inline accessors (the hot path of every parser) ----------------------------------------
inline uint32_t Value::word() const { return m_d->m_tape[m_i]; }
inline char Value::lead() const { return m_d->m_src[word() & detail::kOffMask]; }
inline bool Value::isObject() const
{
    return m_d && (word() & (detail::kKindMask | detail::kFlag)) == (detail::kOpen | detail::kFlag);
}
inline bool Value::isArray() const
{
    return m_d && (word() & (detail::kKindMask | detail::kFlag)) == detail::kOpen;
}
inline bool Value::isString() const { return m_d && (word() & detail::kKindMask) == detail::kString; }
inline bool Value::isNumber() const
{
    return m_d && (word() & detail::kKindMask) == detail::kScalar && lead() != 't' && lead() != 'f'
        && lead() != 'n';
}
inline bool Value::isBool() const
{
    return m_d && (word() & detail::kKindMask) == detail::kScalar && (lead() == 't' || lead() == 'f');
}
inline bool Value::isNull() const { return m_d && (word() & detail::kKindMask) == detail::kScalar && lead() == 'n'; }

inline bool Doc::keyIs(uint32_t i, std::string_view k) const
{
    const uint32_t w = m_tape[i];
    if (w & detail::kFlag) return decodeAt(i) == k;
    const size_t o = (w & detail::kOffMask) + 1;
    return o + k.size() < m_src.size() && m_src[o + k.size()] == '"'
        && std::memcmp(m_src.data() + o, k.data(), k.size()) == 0;
}
inline std::string_view Doc::strAt(uint32_t i) const
{
    const uint32_t w = m_tape[i];
    if (w & detail::kFlag) return decodeAt(i);
    const char *s = m_src.data() + (w & detail::kOffMask) + 1;
    const char *q = (const char *) std::memchr(s, '"', (size_t)(m_src.data() + m_src.size() - s));
    return std::string_view(s, (size_t)(q - s));
}

inline Value Value::operator[](std::string_view key) const
{
    if (!isObject()) return {};
    for (uint32_t i = m_i + 2, end = m_d->m_tape[m_i + 1]; i < end; i = m_d->next(i + 1))
        if (m_d->keyIs(i, key)) return Value(m_d, i + 1);
    return {};
}
inline Value Value::operator[](size_t index) const
{
    if (!isArray()) return {};
    for (uint32_t i = m_i + 2, end = m_d->m_tape[m_i + 1]; i < end; i = m_d->next(i))
        if (index-- == 0) return Value(m_d, i);
    return {};
}
inline Value Value::last() const
{
    if (!isArray()) return {};
    uint32_t prev = 0;
    for (uint32_t i = m_i + 2, end = m_d->m_tape[m_i + 1]; i < end; i = m_d->next(i)) prev = i;
    return prev ? Value(m_d, prev) : Value();
}
inline std::string_view Value::str() const { return isString() ? m_d->strAt(m_i) : std::string_view(); }
inline int64_t Value::int64(int64_t fallback) const
{
    int64_t v;
    return toInt(v) ? v : fallback;
}
inline bool Value::boolean(bool fallback) const { return isBool() ? lead() == 't' : fallback; }

inline Items::iterator &Items::iterator::operator++()
{
    m_i = m_d->next(m_i);
    return *this;
}
inline Member Members::iterator::operator*() const { return Member{m_d->strAt(m_i), Value(m_d, m_i + 1)}; }
inline Members::iterator &Members::iterator::operator++()
{
    m_i = m_d->next(m_i + 1);
    return *this;
}
inline Items Value::items() const
{
    return isArray() ? Items(m_d, m_i + 2, m_d->m_tape[m_i + 1]) : Items(nullptr, 0, 0);
}
inline Members Value::members() const
{
    return isObject() ? Members(m_d, m_i + 2, m_d->m_tape[m_i + 1]) : Members(nullptr, 0, 0);
}

} // namespace yt::json
#endif
