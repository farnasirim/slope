#ifndef SLOPE_PAGE_TRACKER_H_
#define SLOPE_PAGE_TRACKER_H_

namespace slope {
namespace ds {

class PageTracker {
  void add(Page p);
  bool empty();
  void notify(uintptr_t addr);
  void pop();
  Page front();

 public:
  PageTracker();
};

class Page {
  uintptr_t addr;
  uint32_t sz;
  uint32_t lkey;
  uint32_t remote_rkey;
};

}  // namespace ds
}  // namespace slope

#endif  // SLOPE_PAGE_TRACKER_H_
