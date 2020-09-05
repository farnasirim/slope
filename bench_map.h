#ifndef SLOPE_BENCH_MAP_H_
#define SLOPE_BENCH_MAP_H_

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
namespace map {

template <typename T>
using MigAlloc = slope::alloc::FixedPoolAllocator<T>;

template <typename T, typename T2>
using MigMap = std::map<T, T2, std::less<T>, MigAlloc<std::pair<T, T2>>>;

using BInt = uint64_t;
using BMap = MigMap<BInt, BInt>;

const std::string map_size_param = "map_size";

void run(std::string self_id, std::vector<std::string> peers,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params);

}  // namespace map
}  // namespace bench
}  // namespace slope

#endif  // SLOPE_BENCH_MAP_H_

