// Pluggable HTTP transport (network backend) interface. C++17. See docs/backends.md.
#ifndef YT_TRANSPORT_H
#define YT_TRANSPORT_H
namespace yt {

// Engine-thread integration for loop-driven backends (e.g. a curl multi handle):
// the engine's idle wait is delegated to the driver, which does its I/O there.
// Self-driven backends (own threads, OS callbacks) have no driver.
class LoopDriver {
public:
    virtual ~LoopDriver() = default;
    // Engine thread: block for I/O up to maxWaitMs or until wake(), then dispatch
    // whatever became ready. A wake() that happened before wait() was entered must
    // make that wait() return immediately.
    virtual void wait(int maxWaitMs) = 0;
    virtual void wake() = 0;               // any thread
};

}
#endif
