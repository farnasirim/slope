#include "page_tracker.h"

#include <cassert>

#include "debug.h"

namespace slope {
namespace ds {

Page::Page() {}
Page::Page(uintptr_t addr_, uint32_t sz_, uint32_t lkey_, uint32_t remote_rkey_)
    : addr(addr_), sz(sz_), lkey(lkey_), remote_rkey(remote_rkey_) {}

void PageTracker::add(Page p) {
  std::lock_guard<std::mutex> lk(m_);
  if (page_states.find(p.addr) == page_states.end()) {
    page_states[p.addr] = PageTracker::PageState::awaiting;
    addr_to_page[p.addr] = p;
    channel_.push(std::make_pair(static_cast<int>(Priority::low), p.addr));
  }
}

void PageTracker::prioritize(uintptr_t addr) {
  std::lock_guard<std::mutex> lk(m_);
  auto it = page_states.find(addr);
  if (it != page_states.end() &&
      it->second == PageTracker::PageState::awaiting) {
    it->second = PageTracker::PageState::prioritized;
    channel_.push(
        std::make_pair(static_cast<int>(PageTracker::Priority::high), addr));
    page_states[addr] = PageTracker::PageState::prioritized;
  }
}

Page PageTracker::pop() {
  auto [_, page_addr] = channel_.block_to_pop();
  std::lock_guard lk(m_);
  if (page_states[page_addr] == PageTracker::PageState::finalized) {
    return pop();
  }
  page_states[page_addr] = PageTracker::PageState::finalized;
  return addr_to_page[page_addr];
}

void PageTracker::close() { channel_.close(); }

PageTracker::PageTracker() {}

}  // namespace ds
}  // namespace slope
