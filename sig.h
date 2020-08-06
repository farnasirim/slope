#ifndef SLOPE_SIG_H_
#define SLOPE_SIG_H_

#include "allocator.h"

namespace slope {
namespace sig {

void reg();
void add_page();
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

