#include "memcached_kv.h"

#include <string>
#include <cstring>
#include <cassert>

#include <libmemcached/memcached.h>

#include "debug.h"

namespace slope {
namespace keyvalue {

Memcached::Memcached(std::string memc_confstr):
  memc_(memcached(memc_confstr.c_str(), strlen(memc_confstr.c_str())), memcached_free) {
  assert(
      memcached_behavior_set(
        memc_.get(),
        MEMCACHED_BEHAVIOR_SUPPORT_CAS,
        true) == MEMCACHED_SUCCESS
      );
}

bool Memcached::compare_and_swap(const std::string& key,
    const std::string& oldv, const std::string& newv)  {

  const char *keys[1] = {key.c_str()};
  size_t sizes[1] = {std::strlen(key.c_str())};

  auto mget_ret = memcached_mget(memc_.get(), keys, sizes, 1);
  assert(mget_ret == MEMCACHED_SUCCESS);

  memcached_result_st *res = memcached_result_create(memc_.get(), NULL);
  assert(res != NULL);

  memcached_return_t err;
  memcached_fetch_result(memc_.get(), res, &err);
  auto val = memcached_result_value(res);
  auto cas = memcached_result_cas(res);
  assert(NULL == memcached_fetch_result(memc_.get(), NULL, &err));

  bool ret = false;
  if(!strcmp(val, oldv.c_str())) {
    auto memc_ret = memcached_cas(memc_.get(), key.c_str(), std::strlen(key.c_str()),
        newv.c_str(), std::strlen(newv.c_str()), static_cast<time_t>(0),
        static_cast<uint32_t>(0), cas);
    ret = memc_ret == MEMCACHED_SUCCESS;
  }
  memcached_result_free(res);
  return ret;
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
