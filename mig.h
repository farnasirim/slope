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

  // static mig_ptr from_mem(T* mem) {

  // }

  T *get() {
    return ptr;
  }

 private:
  T *ptr;
};

}  // namespace slope
#endif  // SLOPE_MIG_H_
