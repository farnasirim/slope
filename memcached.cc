#include "memcached.h"

#include <cstring>

namespace slope {
namespace discovery {

Memcached::Memcached(const std::string& prefix, const char *memc_confstr): prefix_(prefix) {
  memc = memcached(memc_confstr, strlen(memc_confstr));
}

void Memcached::register_node(const std::string& my_id, const std::string& info) {
  auto key = std::string("SLOPE_DISCOVERY_") + my_id;
  auto ret = memcached_set(memc,
      key.c_str(), std::strlen(key.c_str()),
      info.c_str(), std::strlen(info.c_str()),
      static_cast<time_t>(0), static_cast<uint32_t>(0));

  assert(ret == MEMCACHED_SUCCESS);
}

std::string Memcached::wait_for(const std::string& other_id) {
  while(true) {
    size_t value_length;
    memcached_return_t ret;
    char *result = memcached_get(memc, other_id.c_str(), std::strlen(other_id.c_str()),
                                 &value_length, 0, &ret);
    if(result != NULL) {
      assert(ret == MEMCACHED_SUCCESS);
      auto to_return = std::string(result);
      free(result);
      return to_return;
    }
  }
  assert(false);
}

Memcached::~Memcached() {
  if(memc) {
    memcached_free(memc);
  }
}

}  // namespace discovery
}  // namespace slope
