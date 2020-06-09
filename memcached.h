#ifndef SLOPE_MEMCACHED_H_
#define SLOPE_MEMCACHED_H_

#include <string>
#include <libmemcached/memcached.h>
#include <cassert>
#include <cstdint>

#include "discovery.h"

namespace slope {
namespace discovery {

class Memcached: public DiscoverySerivce {
 public:
  std::string prefix_;
  memcached_st *memc;

  Memcached(const std::string& prefix, const char *memc_confstr);

  virtual void register_node(const std::string& my_id, const std::string& info)
    final override;

  virtual std::string wait_for(const std::string& other_id) final override;

  virtual ~Memcached();
};

}  // namespace discovery
}  // namespace slope

#endif  // SLOPE_MEMCACHED_H_
