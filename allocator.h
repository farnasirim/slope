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
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <set>
#include <memory>
#include <vector>

#include <boost/core/demangle.hpp>
#include <typeinfo>

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

uintptr_t page_of_addr(uintptr_t addr);

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

std::size_t align_to_page(std::size_t n);

std::vector<memory_chunk> chunks_to_pages(const std::vector<memory_chunk>&);
// std::vector<memory_chunk>&);

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

  static std::vector<memory_chunk> get_4k_pages(const T* ptr) {
    return chunks_to_pages(get_chunks(ptr));
  }

  static std::vector<memory_chunk> get_chunks(const T* ptr) {
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


  size_t get_final_size(size_t n, size_t sz) {
    return align_to_page(n * sz);
  }

  size_t get_fit_size(size_t n, size_t sz) {
    return n * sz;
  }

  T *register_preowned(const memory_chunk& owner,
      const std::vector<memory_chunk>& chunks) {

    T *ret = nullptr;
    for(const auto& chunk: chunks) {
      if(chunk == owner) {
        // This is a great moment. We are reinterpreting a uintptr_t
        // brought here from another machine, with no regards for human life.
        assert(ret == nullptr);
        ret = reinterpret_cast<T*>(chunk.first);
      }
      // don't need to touch global ownership stack
      // We're not allocating anything _for_ this new chunk.

      object_allocations[owner.first].insert(chunk);
      addr_to_owner[chunk.first] = owner.first;

      {
        std::stringstream deb_ss;
        std::string name = boost::core::demangle(typeid(T).name());
        deb_ss << "Place " << chunk.second << " bytes"
          << std::showbase << std::internal << std::setfill('0')
           << " @" << std::hex << std::setw(16) << chunk.first;
        deb_ss << " (" << name << ")";
        infoout(deb_ss.str());
      }
    }

    for(const auto& chunk: chunks_to_pages(chunks)) {
      if (mprotect(reinterpret_cast<void*>(chunk.first), chunk.second,
                   PROT_READ | PROT_WRITE)) {
        perror("mprotect");
        assert(false);
      }
    }

    assert(ret != nullptr);
    return ret;
  }

  [[nodiscard]]
  T* allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }

    auto orig_count = n;

    // TODO: fix alignment
    auto fit_size = get_fit_size(n, sizeof(T));
    T *ret = nullptr;
    if(global_ownership_stack.back()->get_ptr() != context_to_be_initialized) {
      auto object_id = global_ownership_stack.back()->get_ptr();
      // deb(object_id);
      // TODO: object allocations must not be empty
      auto [ad, sz] = *std::prev(object_allocations[object_id].end());
      // deb(ad + sz);
      // deb((page_size - 1) & (ad + sz));
      // deb(fit_size);
      if(((page_size - 1) & (ad + sz)) && page_size - ((page_size - 1) & (ad + sz)) >= fit_size) {
        ret = reinterpret_cast<T*>(ad + sz);
      }
    }
    n = fit_size;

    if(ret == nullptr) {
      auto aligned_size = get_final_size(n, sizeof(T));
      auto start_addr = current_mem;
      current_mem += aligned_size;

      if(current_mem > mem + mem_size) {
        throw std::bad_alloc();
      }

      if(mprotect(start_addr, aligned_size, PROT_READ | PROT_WRITE)) {
        perror("mprotect");
        assert(false);
      }

      ret = reinterpret_cast<T*>(start_addr);

      if(global_ownership_stack.back()->get_ptr() == context_to_be_initialized) {
        global_ownership_stack.back()->set_ptr(reinterpret_cast<uintptr_t>(ret));
      }
    }

    object_allocations[global_ownership_stack.back()->get_ptr()]
      .insert(memory_chunk(reinterpret_cast<uintptr_t>(ret), n));
    addr_to_owner[reinterpret_cast<uintptr_t>(ret)] = global_ownership_stack.back()->get_ptr();

    {
      std::stringstream deb_ss;
      std::string name = boost::core::demangle(typeid(T).name());
      deb_ss << "Alloc " << std::setw(4) << orig_count << " x " << std::setw(4)
        << sizeof(T) << " = " << std::setw(8) << n
        << std::showbase << std::internal << std::setfill('0')
        << " @" << std::hex << std::setw(16) << ret;
      deb_ss << " (" << name << ")";
      infoout(deb_ss.str());
    }

    return ret;
  }

  void deallocate(T* p, std::size_t sz) noexcept {
    auto addr = reinterpret_cast<uintptr_t>(p);
    auto owner = addr_to_owner[addr];
    addr_to_owner.erase(owner);
    sz = get_fit_size(sz, sizeof(T));
    auto chunk = std::make_pair(addr, sz);
    auto chunk_it = object_allocations[owner].find(chunk);
    assert(chunk_it != object_allocations[owner].end());
    object_allocations[owner].erase(chunk_it);

    {
      std::stringstream deb_ss;
      deb_ss << std::showbase << std::internal << std::setfill('0')
        << "Dealloc'd at: " << std::hex << std::setw(16) << p;
      infoout(deb_ss.str());
    }
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
