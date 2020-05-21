#ifndef SLOPE_ALLOCATOR_H_
#define SLOPE_ALLOCATOR_H_

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cassert>

#include <new>
#include <limits>
#include <iostream>
#include <unordered_map>
#include <set>
#include <mutex>
#include <memory>

#include "slope.h"
#include "debug.h"

void add_mmap(void) __attribute__((constructor));

namespace slope {
namespace alloc {

namespace context {

extern const uintptr_t context_init;

}  // context

extern char *mem;
extern char *current_mem;
extern size_t page_size;
extern size_t num_pages;
extern size_t mem_size;

using memory_chunk = std::pair<uintptr_t, size_t>;
extern std::unordered_map<uintptr_t, std::set<slope::alloc::memory_chunk>>
  object_allocations;

extern std::mutex allocation_mutex;

// Has to be stateless. Must not allow mem to be passed in during object
// creation.
template <class T>
struct FixedPoolAllocator {
  typedef T value_type;

  static constexpr T* context_init = nullptr;
  static inline T* current_context;

  FixedPoolAllocator() = default;
  template <class U> constexpr FixedPoolAllocator(
      const FixedPoolAllocator<U>&) noexcept {}

  [[nodiscard]]
  static std::unique_ptr<std::lock_guard<std::mutex>> acquire_context(T* obj) {
    // No relation to unique lock: we're not deferring the acquisiton of the lock
    auto lock = std::make_unique<std::lock_guard<std::mutex>>(allocation_mutex);

    if(obj == context_init) {
      current_context = nullptr;
    } else {
      current_context = obj;
    }

    return lock;
  };

  std::size_t align_to_page(std::size_t n) const {
    return (n + page_size - 1) & ~(page_size - 1);
  }

  [[nodiscard]]
  T* allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    deb(n);
    n = align_to_page(n);
    deb(n);

    auto start_addr = current_mem;
    current_mem += n;
    deb(static_cast<void*>(current_mem));
    debline();


    if(current_mem > mem + mem_size) {
      throw std::bad_alloc();
    }

    auto ret = reinterpret_cast<T*>(start_addr);

    if(current_context == nullptr) {
      current_context = ret;
    }

    object_allocations[reinterpret_cast<uintptr_t>(current_context)]
      .insert(memory_chunk(reinterpret_cast<uintptr_t>(ret), n));

    return ret;
  }

  void deallocate(T* p, std::size_t) noexcept {
    // ?
    // keep the inverse maps from alloc, find the owner of this chunk,
    // correct the object_allocaitons data structure.
    std::cout << "dealloc" << std::endl;
  }
};

template<typename T>
FixedPoolAllocator<T>& allocator_instance() {
  static FixedPoolAllocator<T> ret;
  return ret;
}

template <class T, class U>
bool operator==(const FixedPoolAllocator <T>&,
    const FixedPoolAllocator <U>&) { return true; }
template <class T, class U>
bool operator!=(const FixedPoolAllocator <T>&,
    const FixedPoolAllocator <U>&) { return false; }

}  // namespace alloc
}  // namespace slope

#endif  // SLOPE_ALLOCATOR_H_
