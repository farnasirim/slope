#ifndef SLOPE_SIG_H_
#define SLOPE_SIG_H_

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>

#include "allocator.h"
#include "page_tracker.h"

namespace slope {
namespace sig {

void add_page_dirty_detection();
void handler(int signo, siginfo_t *info, void *context);
void install_sigsegv_handler();
bool is_dirty(uintptr_t page);
void remove_dirty_detection();
void add_dirty_detection(uintptr_t page);
void finish_transfer(uintptr_t page);
void set_active_tracker(slope::ds::PageTracker *active_tracker);
//   struct sigaction sa;
// 
//   sa.sa_sigaction = handler;
//   sigemptyset(&sa.sa_mask);
//   // sigaddset(&sa.sa_mask, SIGSEGV);
//   sa.sa_flags = SA_NODEFER | SA_RESTART | SA_SIGINFO; /* Restart functions if
//                                interrupted by handler */
//   if (sigaction(SIGSEGV, &sa, NULL) == -1) {
//     perror("sigaction");
//   }
// }

}  // namespace sig
}  // namespace slope

#endif  // SLOPE_SIG_H_

