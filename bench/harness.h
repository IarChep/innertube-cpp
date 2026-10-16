// Bench harness shared by every candidate: corpus, oracle check, malloc accounting,
// perf_event counters (user-space only), robust timing. A candidate defines
//   const char *kName;  std::string extract(const std::string &doc, Kind k);
// and includes this header once (it defines main()).
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <linux/perf_event.h>
#include <malloc.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

enum class Kind { Lists, Player, Json3 };

// ---- output: one '|' separated line per record, same escaping as oracle.py ----------
struct Out {
    std::string s;
    bool first = true;
    void field(std::string_view v)
    {
        if (!first) s += '|';
        first = false;
        for (char c : v) {
            if (c == '\\') s += "\\\\";
            else if (c == '\n') s += "\\n";
            else if (c == '|') s += "\\p";
            else s += c;
        }
    }
    void field(int64_t v, bool ok) { field(ok ? std::to_string(v) : std::string()); }
    void end() { s += '\n'; first = true; }
};

// ---- malloc accounting: glibc's __libc_* behind our own malloc symbols ---------------
extern "C" void *__libc_malloc(size_t);
extern "C" void *__libc_calloc(size_t, size_t);
extern "C" void *__libc_realloc(void *, size_t);
extern "C" void __libc_free(void *);
extern "C" void *__libc_memalign(size_t, size_t);
namespace heap {
inline bool on = false;
inline size_t calls = 0, live = 0, peak = 0;
inline void add(void *p) { if (on && p) { ++calls; live += malloc_usable_size(p); peak = std::max(peak, live); } }
inline void sub(void *p) { if (on && p) { size_t n = malloc_usable_size(p); live = n > live ? 0 : live - n; } }
}
extern "C" void *malloc(size_t n) noexcept { void *p = __libc_malloc(n); heap::add(p); return p; }
extern "C" void *calloc(size_t a, size_t b) noexcept { void *p = __libc_calloc(a, b); heap::add(p); return p; }
extern "C" void free(void *p) noexcept { heap::sub(p); __libc_free(p); }
extern "C" void *realloc(void *o, size_t n) noexcept { heap::sub(o); void *p = __libc_realloc(o, n); heap::add(p); return p; }
extern "C" void *memalign(size_t a, size_t n) noexcept { void *p = __libc_memalign(a, n); heap::add(p); return p; }
extern "C" void *aligned_alloc(size_t a, size_t n) noexcept { void *p = __libc_memalign(a, n); heap::add(p); return p; }
extern "C" int posix_memalign(void **r, size_t a, size_t n) noexcept { *r = __libc_memalign(a, n); heap::add(*r); return *r ? 0 : 12; }

// ---- perf counters --------------------------------------------------------------------
struct Counter {
    int fd = -1;
    explicit Counter(uint64_t cfg)
    {
        perf_event_attr a{};
        a.size = sizeof a; a.type = PERF_TYPE_HARDWARE; a.config = cfg;
        a.exclude_kernel = 1; a.exclude_hv = 1; a.disabled = 1;
        fd = (int) syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
    }
    void start() { if (fd >= 0) { ioctl(fd, PERF_EVENT_IOC_RESET, 0); ioctl(fd, PERF_EVENT_IOC_ENABLE, 0); } }
    uint64_t stop() { uint64_t v = 0; if (fd >= 0) { ioctl(fd, PERF_EVENT_IOC_DISABLE, 0); if (read(fd, &v, 8) != 8) v = 0; } return v; }
};

const char *kName;
std::string extract(const std::string &doc, Kind k);

namespace harness {
inline std::string slurp(const std::string &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
inline Kind kindOf(const std::string &name)
{
    if (name.rfind("player_", 0) == 0) return Kind::Player;
    if (name.size() > 6 && name.substr(name.size() - 6) == ".json3") return Kind::Json3;
    return Kind::Lists;
}
}

// argv: <corpus dir> <expect dir> [ms per doc]  ->  TSV: name doc bytes ns cycles instr allocs peak ok
int main(int argc, char **argv)
{
    using namespace harness;
    if (argc < 3) return 2;
    const std::string dir = argv[1], exp = argv[2];
    const double budgetMs = argc > 3 ? atof(argv[3]) : 300;
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) if (e->d_name[0] != '.') names.push_back(e->d_name);
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    Counter cyc(PERF_COUNT_HW_CPU_CYCLES), ins(PERF_COUNT_HW_INSTRUCTIONS);
    int bad = 0;
    for (const std::string &n : names) {
        const std::string doc = slurp(dir + "/" + n);
        const Kind k = kindOf(n);
        // one cold-ish call under accounting: allocations and peak heap of one parse
        heap::calls = heap::live = heap::peak = 0; heap::on = true;
        std::string got = extract(doc, k);
        heap::on = false;
        const size_t calls = heap::calls, peak = heap::peak;
        const bool ok = got == slurp(exp + "/" + n + ".expect");
        if (!ok) {
            ++bad;
            std::ofstream(std::string("/tmp/claude-1000/") + kName + "." + n + ".got") << got;
        }
        // timing: repeat until the budget is spent, keep per-call samples, report median
        std::vector<double> ns; std::vector<uint64_t> cs, is;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < budgetMs
               || ns.size() < 15) {
            cyc.start(); ins.start();
            const auto a = std::chrono::steady_clock::now();
            std::string r = extract(doc, k);
            const auto b = std::chrono::steady_clock::now();
            cs.push_back(cyc.stop()); is.push_back(ins.stop());
            ns.push_back(std::chrono::duration<double, std::nano>(b - a).count());
            asm volatile("" : : "r"(r.data()) : "memory");
        }
        auto med = [](auto v) { std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; };
        std::printf("%s\t%s\t%zu\t%.0f\t%llu\t%llu\t%zu\t%zu\t%s\n", kName, n.c_str(), doc.size(), med(ns),
                    (unsigned long long) med(cs), (unsigned long long) med(is), calls, peak, ok ? "ok" : "MISMATCH");
    }
    return bad ? 1 : 0;
}
