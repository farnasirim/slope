#include "memcached.h"

#include <cstring>

namespace slope {
namespace discovery {

Memcached::Memcached(const std::string& prefix, const char *memc_confstr,
    std::vector<std::string> peers):
  prefix_(prefix),
  peers_(std::move(peers)) {
  memc = memcached(memc_confstr, strlen(memc_confstr));
}

void Memcached::register_node(const std::string& my_id, const std::string& info) {
  auto key = prefixize_node_name(my_id);
  auto ret = memcached_set(memc,
      key.c_str(), std::strlen(key.c_str()),
      info.c_str(), std::strlen(info.c_str()),
      static_cast<time_t>(0), static_cast<uint32_t>(0));

  assert(ret == MEMCACHED_SUCCESS);
}

std::string Memcached::prefixize_node_name(const std::string& name) {
  return prefix_ + name;
}

std::string Memcached::wait_for(const std::string& other_id) {
  auto other_key = prefixize_node_name(other_id);
  while(true) {
    size_t value_length;
    memcached_return_t ret;
    char *result = memcached_get(memc, other_key.c_str(), std::strlen(other_key.c_str()),
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

std::vector<std::pair<std::string, std::string>> Memcached::wait_for_peers() {
  std::vector<std::pair<std::string, std::string>> ret;
  for(const auto& peer_name: peers_) {
    ret.emplace_back(peer_name, wait_for(peer_name));
  }
  return ret;
}

Memcached::~Memcached() {
  if(memc) {
    memcached_free(memc);
  }
}

}  // namespace discovery
}  // namespace slope
