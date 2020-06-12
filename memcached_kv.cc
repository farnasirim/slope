#include "memcached_kv.h"

#include <string>
#include <cstring>

#include <memcached.h>

namespace slope {
namespace keyvalue {

/*
 *
void Memcached::register_node(const std::string& my_id, const std::string& info) {
  auto key = prefixize_node_name(my_id);

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
 */

Memcached::Memcached(std::string memc_confstr):
  memc_(memcached(memc_confstr.c_str(), strlen(memc_confstr.c_str())), memcached_free) {
}

bool Memcached::compare_and_swap(const std::string& key,
    const std::string& oldv, const std::string& newv)  {
  return true;
}

bool Memcached::set(const std::string& key, const std::string& val) {
  auto ret = memcached_set(memc_.get(),
      key.c_str(), std::strlen(key.c_str()),
      val.c_str(), std::strlen(val.c_str()),
      static_cast<time_t>(0), static_cast<uint32_t>(0));
  return ret == MEMCACHED_SUCCESS;
}

bool Memcached::get(const std::string& key, std::string& ret) {
  size_t value_length;
  memcached_return_t memc_ret;
  char *result = memcached_get(memc_.get(), key.c_str(), std::strlen(key.c_str()),
                               &value_length, 0, &memc_ret);
  if(result != NULL) {
    assert(memc_ret == MEMCACHED_SUCCESS);
    ret = std::string(result);
    free(result);
    return true;
  }
  return false;
}

bool Memcached::wait_for(const std::string& key, std::string& ret) {
  while(!get(key, ret)) {
    // do it
  }
  return true;
}

}  // namespace keyvalue
}  // namespace slope
