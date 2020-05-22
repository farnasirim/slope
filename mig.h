#ifndef SLOPE_MIG_H_
#define SLOPE_MIG_H_

#include <utility>

#include "allocator.h"

namespace slope {

// T must already know that it must use the same tracked alloc
// No way we know how to pass TrackedAlloc to T
template<typename T>
class mig_ptr {
 public:
  template<typename ...Args>
  mig_ptr(Args&& ...args) {
    debout("here");
    {
      debout("inside");
      auto outer_allocator = alloc::allocator_instance<T>();
      auto lock = outer_allocator.acquire_context(outer_allocator.context_init);
      debout("locked");
      auto object_mem = outer_allocator.allocate(sizeof(T));
      new(object_mem) T(std::forward<Args>(args)...);
      ptr = object_mem;
    }
  }

  mig_ptr(const mig_ptr&) = delete;
  mig_ptr(mig_ptr&&) = delete;
  mig_ptr& operator==(const mig_ptr&) = delete;
  mig_ptr& operator==(mig_ptr&&) = delete;

  // direct operations on ptr must not cause new memory allocations
  T *get() {
    return ptr;
  }

  void push_back(T some_value) {
    auto& outer_allocator = alloc::allocator_instance<T>();
    auto lock = outer_allocator.acquire_context(ptr);
    ptr->push_back(some_value);
  }

  std::unique_ptr<std::lock_guard<std::mutex>> acquire_context() {
    auto& outer_allocator = alloc::allocator_instance<T>();
    return outer_allocator.acquire_context(ptr);
  }

  std::vector<alloc::memory_chunk> get_pages() {
    auto& outer_allocator = alloc::allocator_instance<T>();
    return outer_allocator.get_pages(ptr);
  }

  //
  // ~mig_ptr() {
  //   // TODO: ?
  // }
  //

 private:
  T *ptr;
};

}  // namespace slope
#endif  // SLOPE_MIG_H_
