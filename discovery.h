#ifndef DISCOVERY_H_
#define DISCOVERY_H_

#include <string>
#include <libmemcached/memcached.h>
#include <vector>
#include <cstdint>

namespace slope {
namespace discovery {

class DiscoverySerivce {
 public:
  virtual void register_node(const std::string& my_id, const std::string& info) = 0;
  virtual std::string wait_for(const std::string& other_id) = 0;
  virtual std::vector<std::pair<std::string, std::string>> wait_for_peers() = 0;
  virtual ~DiscoverySerivce() = default;
};

}  // namespace discovery
}  // namespace slope

struct QpInfo {
  uint16_t lid;
  uint32_t qp_num;
};

struct QpInfo make_qp_info(uint16_t lid, uint32_t qp_num);
struct QpInfo exchange_qp_info(memcached_st *memc, const char *adv_key,
                               const char *target_key, struct QpInfo local);

struct QpInfo exchange_qp_info(struct QpInfo local);

static const int QpInfoAdvTimeout = 1;

#endif  // DISCOVERY_H_
