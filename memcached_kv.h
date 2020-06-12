#ifndef SLOPE_MEMCACHED_KV_H_
#define SLOPE_MEMCACHED_KV_H_

#include <memory>
#include <functional>

#include <memcached.h>

#include "keyvalue.h"

namespace slope {
namespace keyvalue {

class Memcached: public KeyValueService {
 public:

  Memcached(std::string memc_confstr);

  virtual bool compare_and_swap(const std::string& key,
      const std::string& oldv, const std::string& newv) final override;
  virtual bool set(const std::string& key, const std::string& val) final override;
  virtual bool get(const std::string& key, std::string& ret) final override;
  virtual bool wait_for(const std::string& key, std::string& ret) final override;
  using ptr = std::shared_ptr<Memcached>;
 private:

  std::unique_ptr<memcached_st, std::function<void(memcached_st*)>> memc_;
};

}  // namespace keyvalue
}  // namespace slope

#endif  // SLOPE_MEMCACHED_KV_H_
