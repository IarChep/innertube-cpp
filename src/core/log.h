// YTLOG(logger, "cat") << a << b; — one diagnostic line through the injected Logger.
// No logger installed: the streamed arguments are not evaluated (zero cost).
#ifndef YT_CORE_LOG_H
#define YT_CORE_LOG_H
#include <yt/client.h>
#include <sstream>
#include <string_view>
namespace yt::core {
// Streams items space-separated (like qDebug did) and emits once, in the destructor.
class LogLine {
public:
    LogLine(const Logger &log, LogLevel lvl, std::string_view cat) : m_log(log), m_lvl(lvl), m_cat(cat) {}
    ~LogLine() { m_log(m_lvl, m_cat, m_s.str()); }
    LogLine(const LogLine &) = delete;
    LogLine &operator=(const LogLine &) = delete;
    template <class T> LogLine &operator<<(const T &v) {
        if (m_any) m_s << ' ';
        m_s << v;
        m_any = true;
        return *this;
    }
private:
    const Logger &m_log;
    LogLevel m_lvl;
    std::string_view m_cat;
    std::ostringstream m_s;
    bool m_any = false;
};
}
// if/else form: arguments unevaluated when off, and dangling-else safe.
#define YTLOG(logger, cat) \
    if (!(logger)) {} else ::yt::core::LogLine((logger), ::yt::LogLevel::Debug, cat)
#endif
