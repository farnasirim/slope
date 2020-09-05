#include <errno.h>
#include <libmemcached/memcached.h>
#include <malloc.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "allocator.h"
#include "bench_bloom.h"
#include "bench_map.h"
#include "bench_readonly.h"
#include "bench_writeall.h"
#include "debug.h"
#include "discovery.h"
#include "ib.h"
#include "ib_container.h"
#include "logging.h"
#include "memcached_kv.h"
#include "modify_qp.h"
#include "rdma_control.h"
#include "sig.h"
#include "slope.h"
#include "stat.h"
#include "testdrive_migrate.h"

struct payload {
  int code;
  char msg[20];
};

#include "json.hpp"

using json = nlohmann::json;


int main(int argc, char **argv) {
  slope::sig::install_sigsegv_handler();

  std::string self_id;
  std::string workload_name;
  std::string memcached_confstr;

  std::vector<std::string> peers;
  std::map<std::string, std::string> params;

  std::string param_identifier = "--param=";
  std::string memcached_confstr_identifier = "--memcached_confstr=";
  std::string workload_identifier = "--workload=";
  std::string self_identifier = "--self=";
  std::string peer_identifier = "--peer=";

  for (int i = 1; i < argc; i++) {
    std::string current_arg = argv[i];
    if (current_arg.find(peer_identifier) == 0) {
      auto maybe_peer = current_arg.substr(peer_identifier.size());
      peers.push_back(current_arg.substr(peer_identifier.size()));
    } else if (current_arg.find(param_identifier) == 0) {
      auto kv = current_arg.substr(param_identifier.size());
      auto key = kv.substr(0, kv.find(":"));
      auto value = kv.substr(kv.find(":") + 1);
      params[key] = value;
    } else if (current_arg.find(memcached_confstr_identifier) == 0) {
      memcached_confstr =
          current_arg.substr(memcached_confstr_identifier.size());
    } else if (current_arg.find(workload_identifier) == 0) {
      workload_name = current_arg.substr(workload_identifier.size());
    } else if (current_arg.find(self_identifier) == 0) {
      self_id = current_arg.substr(self_identifier.size());
    } else {
      slope::logging::warn("unused commandline option: " + current_arg);
    }
  }

  if (find(peers.begin(), peers.end(), self_id) == peers.end()) {
    peers.push_back(self_id);
  }
  sort(peers.begin(), peers.end());

  auto node_index = static_cast<size_t>(
      find(peers.begin(), peers.end(), self_id) - peers.begin());
  auto num_pages_for_each_node = SLOPE_NUM_PAGES / peers.size();
  auto start_page_for_current_node = num_pages_for_each_node * node_index;
  // deb(sizeof(node_index));
  // deb(sizeof(start_page_for_current_node));
  // deb(sizeof(num_pages_for_each_node));
  slope::alloc::current_mem +=
      start_page_for_current_node * slope::alloc::page_size;

  deb(peers);
  {
    std::unique_ptr<slope::control::RdmaControlPlane<td_mig_type>>
        control_plane = nullptr;

    std::make_unique<slope::control::RdmaControlPlane<td_mig_type>>(
        self_id, peers,
        std::make_unique<slope::keyvalue::KeyValuePrefixMiddleware>(
            std::make_unique<slope::keyvalue::Memcached>(
                memcached_confstr.c_str()),
            "SLOPE_TIMECALIB_"),
        1);
  }
  slope::stat::set_meta(slope::stat::metakey::node_name, self_id);

  auto kv =
      std::make_unique<slope::keyvalue::Memcached>(memcached_confstr.c_str());
  auto slope_kv = std::make_unique<slope::keyvalue::KeyValuePrefixMiddleware>(
      std::move(kv), "SLOPE_");

  slope::stat::set_meta(slope::stat::metakey::workload_name, workload_name);
  if (workload_name == "migvector") {
    auto test_control_plane =
        std::make_unique<slope::control::RdmaControlPlane<td_mig_type>>(
            self_id, peers, std::move(slope_kv));

    testdrive_migrate(std::move(test_control_plane));
  } else if (workload_name == "readonly") {
    slope::bench::readonly::run(self_id, peers, std::move(slope_kv), params);
  } else if (workload_name == "writeall") {
    slope::bench::writeall::run(self_id, peers, std::move(slope_kv), params);
  } else if (workload_name == "bloomfilter") {
    slope::bench::bloomfilter::run(self_id, peers, std::move(slope_kv), params);
  } else if (workload_name == "map") {
    slope::bench::map::run(self_id, peers, std::move(slope_kv), params);
  } else {
    assert(false);
  }

  std::cout << slope::stat::get_all_logs().dump(4) << std::endl;
  return 0;
}
