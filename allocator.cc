#include "allocator.h"

#include <cassert>
#include <cstdint>
#include <sys/mman.h>
#include <errno.h>
#include <cstdio>

#include <unordered_map>
#include <set>

void add_mmap(void) {
  slope::alloc::page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  slope::alloc::num_pages = SLOPE_NUM_PAGES;
  slope::alloc::mem_size = slope::alloc::page_size * slope::alloc::num_pages;

  slope::alloc::mem = static_cast<char*>(mmap(reinterpret_cast<void*>(SLOPE_MEM_ADDR),
      slope::alloc::mem_size,
      PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
      -1, 0));
  if(slope::alloc::mem == MAP_FAILED) {
    std::perror("mmap (constructor)");
    assert(false);
  }
  slope::alloc::current_mem = slope::alloc::mem;
}

namespace slope {
namespace alloc {

// (for later) must be shared between allocators from the same core e.g.
// shared between FixedPoolAllocator<int, 0>, FixedPoolAllocator<char, 0>
char *mem;
char *current_mem;
size_t page_size;
size_t num_pages;
size_t mem_size;
std::vector<std::shared_ptr<OwnershipFrame>> global_ownership_stack;
std::unordered_map<uintptr_t, uintptr_t> addr_to_owner;

// Don't do ANYTHING else with uintptr_t as it would be UB
// i.e. (just store and retrieve)
std::unordered_map<uintptr_t, std::set<slope::alloc::memory_chunk>>
  object_allocations;


OwnershipFrame::OwnershipFrame(std::vector<std::shared_ptr<OwnershipFrame>>&
    ownership_stack_ref, uintptr_t ptr):
  ownership_stack(ownership_stack_ref), ptr_(ptr) {
}

void OwnershipFrame::push() {
  ownership_stack.push_back(shared_from_this());
}

OwnershipFrame::~OwnershipFrame() {
  if(ptr_ != 0) {
    auto now = ownership_stack.back();
    assert(this == ownership_stack.back().get());
    ownership_stack.pop_back();
  }
}

uintptr_t OwnershipFrame::get_ptr() const {
  return ptr_;
}

void OwnershipFrame::set_ptr(uintptr_t ptr) {
  ptr_ = ptr;
}


}  // namespace alloc
}  // namespace slope
