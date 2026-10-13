// Per-operation cancellation flag for the facade layer. Pure C++ (no Qt): the
// token is a shared_ptr<JobState> handed to every async continuation so a
// delivery closure can drop its work if the operation was canceled meanwhile.
//
// Threading contract: canceled is SET only from the GUI thread (a facade's
// cancel()/destructor) and READ from anywhere (worker or GUI). It is monotonic —
// once true it never goes back to false — so a relaxed load is sufficient: the
// only transition a reader can observe is false→true, and either answer is a
// legitimate, race-free outcome of the moment it looked.
#ifndef YT_CORE_JOB_H
#define YT_CORE_JOB_H
#include <atomic>
#include <memory>
namespace yt { namespace core {
// One cancellation flag per facade operation. Set only from the GUI thread
// (cancel()/destructor); read anywhere. Monotonic: never un-canceled.
struct JobState { std::atomic<bool> canceled; JobState() : canceled(false) {} };
typedef std::shared_ptr<JobState> JobToken;
inline JobToken newJob() { return std::make_shared<JobState>(); }
inline bool live(const JobToken &t)
{ return t && !t->canceled.load(std::memory_order_relaxed); }
}}
#endif
