#include "allocator.h"

#include <cassert>
#include <cstdint>
#include <sys/mman.h>
#include <errno.h>
#include <cstdio>

#include <unordered_map>
#include <set>
#include <mutex>

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

std::mutex allocation_mutex;

// Don't do ANYTHING else with uintptr_t as it would be UB
// i.e. (just store and retrieve)
std::unordered_map<uintptr_t, std::set<slope::alloc::memory_chunk>>
  object_allocations;

}  // namespace alloc
}  // namespace slope
