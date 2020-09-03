#ifndef SLOPE_BENCH_BLOOM_H_
#define SLOPE_BENCH_BLOOM_H_

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
namespace bloomfilter {

const std::string bloomfilter_size_param = "bloomfilter_size";

void run(std::string self_id, std::vector<std::string> peres,
         std::unique_ptr<slope::keyvalue::KeyValuePrefixMiddleware> kv,
         std::map<std::string, std::string> params);

}  // namespace bloomfilter
}  // namespace bench
}  // namespace slope

#endif  // SLOPE_BENCH_READONLY_H_

