#ifndef SLOPE_MEMCACHED_H_
#define SLOPE_MEMCACHED_H_

#include <string>
#include <libmemcached/memcached.h>
#include <vector>
#include <cassert>
#include <cstdint>

#include "discovery.h"

namespace slope {
namespace discovery {

class Memcached: public DiscoverySerivce {
 public:
  Memcached(const std::string& prefix, const char *memc_confstr, std::vector<std::string> peers);

  virtual void register_node(const std::string& my_id, const std::string& info)
    final override;

  virtual std::string wait_for(const std::string& other_id) final override;

  virtual std::vector<std::pair<std::string, std::string>> wait_for_peers() final override;

  std::string prefixize_node_name(const std::string& name);

  virtual ~Memcached();

 private:
  const std::string prefix_;
  memcached_st *memc;
  std::vector<std::string> peers_;
};

}  // namespace discovery
}  // namespace slope

#endif  // SLOPE_MEMCACHED_H_
