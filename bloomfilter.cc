#include "bloomfilter.h"

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace slope {
namespace bench {
namespace bloomfilter {

uint32_t BloomFilter::get_ind(const T &val, uint32_t p) {
  long ret = 0;
  for (auto it : val) {
    ret = (ret << 4) + it * static_cast<int32_t>(p);
    p *= p;
    long g = ret & 0xF0000000L;
    if (g != 0) {
      ret ^= g >> 24;
    }
    ret &= ~g;
  }
  return static_cast<uint64_t>(ret) % (vec.size() * 8);
}

void BloomFilter::set(const T &s) {
  auto i1 = get_ind(s, p1), i2 = get_ind(s, p2);
  if (i1 > i2) {
    std::swap(i1, i2);
  }
  std::unique_lock<std::mutex> l1((*locks)[i1 / 8], std::defer_lock),
      l2((*locks)[i2 / 8], std::defer_lock);
  if (i1 / 8 != i2 / 8) {
    std::lock(l1, l2);
  } else {
    l1.lock();
  }

  vec[i1 / 8] |= ((1u << (i1 % 8)));
  vec[i2 / 8] |= ((1u << (i2 % 8)));
}

bool BloomFilter::get(const T &s) {
  auto i1 = get_ind(s, p1), i2 = get_ind(s, p2);
  if (i1 > i2) {
    std::swap(i1, i2);
  }
  std::unique_lock<std::mutex> l1((*locks)[i1 / 8], std::defer_lock),
      l2((*locks)[i2 / 8], std::defer_lock);
  if (i1 / 8 != i2 / 8) {
    std::lock(l1, l2);
  } else {
    l1.lock();
  }

  return !!(vec[i1 / 8] & (1u << (i1 % 8))) &&
         !!(vec[i2 / 8] & (1u << (i2 % 8)));
}

BloomFilter::BloomFilter(size_t sz) : vec(sz) {}

void BloomFilter::set_lock_vector(std::vector<std::mutex> *lock_vector) {
  this->locks = lock_vector;
}

size_t BloomFilter::size() const { return vec.size(); }

}  // namespace bloomfilter
}  // namespace bench
}  // namespace slope
