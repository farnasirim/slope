#include "sig.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

namespace slope {
namespace sig {

void add_page_dirty_detection() {}

void handler(int signo, siginfo_t *info, void *context) {
}

bool is_dirty(uintptr_t page) { return true; }

void remove_dirty_detection(uintptr_t page) {}

void add_dirty_detection(uintptr_t page) {}

void install_sigsegv_handler() {
  struct sigaction sa;

  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NODEFER | SA_RESTART | SA_SIGINFO;
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    perror("sigaction");
  }
}

}  // namespace sig
}  // namespace slope

