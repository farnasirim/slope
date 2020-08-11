#ifndef SLOPE_PAGE_TRACKER_H_
#define SLOPE_PAGE_TRACKER_H_

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>

#include "debug.h"

namespace slope {
namespace ds {

namespace impl {

template <typename T>
class PriorityChannel {
  std::condition_variable cv_;
  std::set<T> q_;
  std::mutex m_;
  bool is_closed_;

 public:
  PriorityChannel() : is_closed_(false) {}

  T block_to_pop() {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [&] { return !q_.empty() || is_closed_; });
    if (q_.empty()) {
      throw std::exception();
    }
    auto ret = std::move(*q_.begin());
    q_.erase(q_.begin());
    lk.unlock();
    cv_.notify_one();
    return ret;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lk(m_);
      is_closed_ = 1;
    }
    cv_.notify_one();
  }

  void push(T&& val) {
    {
      std::lock_guard<std::mutex> lk(m_);
      q_.insert(std::forward<T>(val));
    }
    cv_.notify_one();
  }
};

}  // namespace impl

struct Page {
  uintptr_t addr;
  uint32_t sz;
  uint32_t lkey;
  uint32_t remote_rkey;

  Page(uintptr_t, uint32_t, uint32_t, uint32_t);
  Page();
};

class PageTracker {
 public:
  void add(Page p);
  void prioritize(uintptr_t addr);
  Page pop();
  void close();

  enum class PageState : int { awaiting, prioritized, finalized };
  enum class Priority : int { high = 0, low };

  PageTracker();

 private:
  impl::PriorityChannel<std::pair<int, uintptr_t>> channel_;
  std::mutex m_;
  std::map<uintptr_t, PageState> page_states;
  std::map<uintptr_t, Page> addr_to_page;
};

}  // namespace ds
}  // namespace slope

#endif  // SLOPE_PAGE_TRACKER_H_
