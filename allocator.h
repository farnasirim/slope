#ifndef SLOPE_ALLOCATOR_H_
#define SLOPE_ALLOCATOR_H_

#include <unistd.h>
#include <sys/mman.h>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cassert>

#include <new>
#include <limits>
#include <iostream>
#include <unordered_map>
#include <set>
#include <memory>
#include <vector>

#include "slope.h"
#include "debug.h"

void add_mmap(void) __attribute__((constructor));

namespace slope {
namespace alloc {

extern char *mem;
extern char *current_mem;
extern size_t page_size;
extern size_t num_pages;
extern size_t mem_size;

using memory_chunk = std::pair<uintptr_t, size_t>;
extern std::unordered_map<uintptr_t, std::set<slope::alloc::memory_chunk>>
  object_allocations;
extern std::unordered_map<uintptr_t, uintptr_t> addr_to_owner;

class OwnershipFrame: public std::enable_shared_from_this<OwnershipFrame> {
 public:
  OwnershipFrame(std::vector<std::shared_ptr<OwnershipFrame>>& ownership_stack,
      uintptr_t ptr);
  ~OwnershipFrame() = default;

  OwnershipFrame(const OwnershipFrame&) = delete;
  OwnershipFrame& operator=(const OwnershipFrame&) = delete;
  OwnershipFrame& operator=(OwnershipFrame&&) = delete;
  OwnershipFrame(OwnershipFrame&& rhs) = delete;

  void push();

  uintptr_t get_ptr() const;
  void set_ptr(uintptr_t ptr);

 private:
  std::vector<std::shared_ptr<OwnershipFrame>>& ownership_stack;
  uintptr_t ptr_;
 friend class OwnershipLock;
};

class OwnershipLock {
 public:
  OwnershipLock(std::shared_ptr<OwnershipFrame> frame): frame_(frame) {

  }
  ~OwnershipLock() {
    assert(frame_.get() == frame_->ownership_stack.back().get());
    frame_->ownership_stack.pop_back();
    frame_ = nullptr;
    assert(frame_.use_count() == 0);
  }
 private:
  OwnershipLock(const OwnershipLock&) = delete;
  OwnershipLock& operator=(const OwnershipLock&) = delete;
  OwnershipLock& operator=(OwnershipLock&&) = delete;
  OwnershipLock(OwnershipLock&& rhs) = delete;

  std::shared_ptr<OwnershipFrame> frame_;
};

extern std::vector<std::shared_ptr<OwnershipFrame>> global_ownership_stack;

// Has to be stateless. Must not allow mem to be passed in during object
// creation.
template <class T>
struct FixedPoolAllocator {
  typedef T value_type;

  static constexpr T* context_init = nullptr;
  static constexpr uintptr_t context_to_be_initialized = static_cast<uintptr_t>(-1);

  FixedPoolAllocator() = default;
  template <class U> constexpr FixedPoolAllocator(
      const FixedPoolAllocator<U>&) noexcept {}

  static std::vector<memory_chunk> get_pages(T* ptr) {
    auto& segments = object_allocations[reinterpret_cast<uintptr_t>(ptr)];
    return std::vector<memory_chunk>(segments.begin(), segments.end());
  }

  [[nodiscard]]
  static std::unique_ptr<OwnershipLock> create_context(T* obj) {
    auto owner = reinterpret_cast<uintptr_t>(obj);
    if(obj == context_init) {
      owner = context_to_be_initialized;
    }
    auto ret = std::make_shared<OwnershipFrame>(global_ownership_stack, owner);
    ret->push();
    return std::make_unique<OwnershipLock>(ret);
  };

  std::size_t align_to_page(std::size_t n) const {
    return (n + page_size - 1) & ~(page_size - 1);
  }

  [[nodiscard]]
  T* allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    n *= sizeof(T);
    n = align_to_page(n);
    deb(n);
    deb(global_ownership_stack);

    deb(reinterpret_cast<void *>(current_mem));
    auto start_addr = current_mem;
    current_mem += n;
    deb(static_cast<void*>(current_mem));
    debline();


    if(current_mem > mem + mem_size) {
      throw std::bad_alloc();
    }

    if(mprotect(start_addr, n, PROT_READ | PROT_WRITE)) {
      perror("mprotect");
      assert(false);
    }
    auto ret = reinterpret_cast<T*>(start_addr);

    if(global_ownership_stack.back()->get_ptr() == context_to_be_initialized) {
      global_ownership_stack.back()->set_ptr(reinterpret_cast<uintptr_t>(ret));
    }

    std::cout << std::hex << " : " << global_ownership_stack.back()->get_ptr() << std::endl;
    std::cout << std::hex << " :: " << memory_chunk(reinterpret_cast<uintptr_t>(ret), n) << std::endl;
    std::cout << std::endl;
    object_allocations[global_ownership_stack.back()->get_ptr()]
      .insert(memory_chunk(reinterpret_cast<uintptr_t>(ret), n));
    addr_to_owner[reinterpret_cast<uintptr_t>(ret)] = global_ownership_stack.back()->get_ptr();

    return ret;
  }

  void deallocate(T* p, std::size_t sz) noexcept {
    auto addr = reinterpret_cast<uintptr_t>(p);
    auto owner = addr_to_owner[addr];
    addr_to_owner.erase(owner);
    object_allocations[owner].erase(std::make_pair(addr, sz));
    std::cout << "dealloc: " << p << std::endl;
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
