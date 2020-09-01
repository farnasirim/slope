#include "sig.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

#include "debug.h"

namespace slope {
namespace sig {

std::mutex global_lock;
std::map<uintptr_t, std::mutex> locks;
std::map<uintptr_t, std::condition_variable> cvs;
std::map<uintptr_t, bool> is_page_writable;
std::map<uintptr_t, bool> is_page_dirty;
std::map<uintptr_t, int> in_use_counter;
slope::ds::PageTracker *active_tracker;

void add_page_dirty_detection() {}

void handler(int signo, siginfo_t *info, void *context) {
  auto page =
      slope::alloc::page_of_addr(reinterpret_cast<uintptr_t>(info->si_addr));
  global_lock.lock();
  if (locks.find(page) != locks.end()) {
    auto &page_mutex = locks[page];
    page_mutex.lock();
    global_lock.unlock();

    auto &page_cv = cvs[page];
    bool &is_writable = is_page_writable[page];
    bool &is_dirty = is_page_dirty[page];
    int &in_use = in_use_counter[page];
    in_use += 1;
    if (is_dirty == 0) {
      is_dirty = 1;
      if (mprotect(reinterpret_cast<void *>(page), slope::alloc::page_size,
                   PROT_READ | PROT_WRITE)) {
        perror("mprotect");
        assert(false);
      }
    }
    page_mutex.unlock();
    page_cv.notify_all();

    std::unique_lock<std::mutex> ulk(page_mutex);
    page_cv.wait(ulk, [&is_writable] { return is_writable; });
    in_use -= 1;
    ulk.unlock();
    page_cv.notify_all();
    return;
  } else if (active_tracker != nullptr) {
    active_tracker->prioritize(page);
  } else {
    ;
  }
  global_lock.unlock();
}

bool is_dirty(uintptr_t page) {
  return is_page_dirty[page];
}

void remove_dirty_detection() {
  locks.clear();
  cvs.clear();
  is_page_writable.clear();
  is_page_dirty.clear();
  in_use_counter.clear();
}

void add_dirty_detection(uintptr_t page) {
  std::lock_guard<std::mutex> lk(global_lock);
  { auto &_ = locks[page]; }
  { auto &_ = cvs[page]; }
  is_page_writable[page] = false;
  is_page_dirty[page] = false;
  in_use_counter[page] = 0;
}

void finish_transfer(uintptr_t page) {
  global_lock.lock();
  auto &page_mutex = locks[page];
  auto &page_cv = cvs[page];
  bool &is_writable = is_page_writable[page];
  int &in_use = in_use_counter[page];
  global_lock.unlock();
  {
    std::lock_guard<std::mutex> lk(page_mutex);
    is_writable = true;
  }
  page_cv.notify_all();
}

void install_sigsegv_handler() {
  struct sigaction sa;

  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NODEFER | SA_RESTART | SA_SIGINFO;
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    perror("sigaction");
  }
}

void set_active_tracker(slope::ds::PageTracker *tracker) {
  std::lock_guard<std::mutex> lk(global_lock);
  active_tracker = tracker;
}

}  // namespace sig
}  // namespace slope

