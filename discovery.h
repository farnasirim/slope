#ifndef DISCOVERY_H_
#define DISCOVERY_H_

#include <libmemcached/memcached.h>
#include <cstdint>

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
