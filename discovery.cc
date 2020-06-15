#include "discovery.h"

#include <cstring>
#include <cassert>

#include <sstream>

#include <libmemcached/memcached.h>

#include "debug.h"

struct QpInfo make_qp_info(uint16_t lid, uint32_t qp_num) {
  struct QpInfo ret = {};

  ret.lid = lid;
  ret.qp_num = qp_num;

  return ret;
}

struct QpInfo exchange_qp_info(struct QpInfo local) {
  struct QpInfo ret;
  std::cout << "############# " << std::endl;
  deb(local.lid);
  deb(local.qp_num);
  std::cout << std::endl;

  std::cout << "lid" << std::endl;
  std::cin >> ret.lid;
  std::cout << "qpn" << std::endl;
  std::cin >> ret.qp_num;

  return ret;
}

struct QpInfo exchange_qp_info(memcached_st *memc, const char *adv_key,
                               const char *target_key, struct QpInfo local) {
  std::stringstream ss;
  ss << local.lid << "x" << local.qp_num;
  struct QpInfo remote;
  auto str = ss.str();
  deb(str);
  deb(std::strlen(str.c_str()));
  auto ret = memcached_set(memc,
                adv_key, strlen(adv_key),
                str.c_str(), std::strlen(str.c_str()),
                static_cast<time_t>(0), static_cast<uint32_t>(0));
  assert(ret == MEMCACHED_SUCCESS);

  while(true) {
    size_t value_length;
    char *result = memcached_get(memc, target_key, strlen(target_key),
                                 &value_length, 0, &ret);
    if(result != NULL) {
      std::cout << std::endl;
      deb(value_length);
      deb(result);
      std::string inp(result);
      deb(inp);
      auto ind = inp.find("x");

      remote.lid = std::stoul(inp.substr(0, ind));
      remote.qp_num = std::stoul(inp.substr(ind + 1));
      break;
    }
  }

  return remote;
}
