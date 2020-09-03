#ifndef SLOPE_BLOOMFILTER_H_
#define SLOPE_BLOOMFILTER_H_

#include <unistd.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "allocator.h"
#include "logging.h"
#include "memcached_kv.h"
#include "mig.h"
#include "rdma_control.h"
#include "stat.h"

namespace slope {
namespace bench {
namespace bloomfilter {

template <typename T>
using MigAlloc = slope::alloc::FixedPoolAllocator<T>;

template <typename T>
using MigVector = std::vector<T, MigAlloc<T>>;

class BloomFilter {
  using T = std::string;

 public:
  void set(const T&);
  bool get(const T &);
  void set_lock_vector(std::vector<std::mutex> *lock_vector);
  BloomFilter(size_t sz);
  size_t size() const;

  uint32_t get_ind(const T &, uint32_t);

  MigVector<uint8_t> vec;

 private:

  static inline const uint32_t p1 = 701;
  static inline const uint32_t p2 = 107;

  std::vector<std::mutex> *locks;
};

}  // namespace bloomfilter
}  // namespace bench
}  // namespace slope

#endif  // SLOPE_BLOOMFILTER_H_

