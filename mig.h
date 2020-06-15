#ifndef SLOPE_MIG_H_
#define SLOPE_MIG_H_

#include <utility>

#include "allocator.h"

namespace slope {

// T must already know that it must use the same tracked alloc
// No way we know how to pass TrackedAlloc to T
template<typename T>
class mig_ptr {
 private:
  mig_ptr(T *p): ptr(p) { }

 public:
  static mig_ptr<T> adopt(T *raw) {
    return std::move(mig_ptr<T>(raw));
  }

  template<typename ...Args>
  mig_ptr(Args&& ...args) {
    {
      auto outer_allocator = alloc::allocator_instance<T>();
      // debout("create context");
      auto context = outer_allocator.create_context(outer_allocator.context_init);
      // debout("alloc for object");
      auto object_mem = outer_allocator.allocate(1);
      // debout("call ctor of object");
      new(object_mem) T(std::forward<Args>(args)...);
      ptr = object_mem;
      // std::cout << "about done" << std::endl;
    }
  }

  mig_ptr(const mig_ptr&) = delete;
  mig_ptr(mig_ptr&& rhs) {
    ptr = rhs.ptr;
    rhs.ptr = nullptr;
  }
  mig_ptr& operator==(const mig_ptr&) = delete;
  mig_ptr& operator==(mig_ptr&&) = delete;

  // direct operations on ptr must not cause new memory allocations
  T *get() {
    return ptr;
  }

  std::unique_ptr<alloc::OwnershipLock> create_context() {
    return alloc::allocator_instance<T>().create_context(ptr);
  }

  std::vector<alloc::memory_chunk> get_pages() {
    auto& outer_allocator = alloc::allocator_instance<T>();
    return outer_allocator.get_pages(ptr);
  }


  ~mig_ptr() {
    // TODO: ?
  }


 private:
  T *ptr;
};

}  // namespace slope
#endif  // SLOPE_MIG_H_
