#ifndef SLOPE_BENCH_WRITEALL_H_
#define SLOPE_BENCH_WRITEALL_H_

#include <unistd.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "allocator.h"
#include "memcached_kv.h"

namespace slope {
namespace bench {
namespace writeall {

template <typename T>
using MigAlloc = slope::alloc::FixedPoolAllocator<T>;

template <typename T>
using MigVector = std::vector<T, MigAlloc<T>>;

using MigVector32 = MigVector<uint32_t>;

const std::string num_pages_param = "num_pages";

void run(std::string self_id, std::vector<std::string> peres,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params);

}  // namespace writeall
}  // namespace bench
}  // namespace slope

#endif  // SLOPE_BENCH_WRITEALL_H_
