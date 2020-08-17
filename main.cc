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
  if (argc < 5) {
    std::cerr << "incorrect num args" << std::endl;
    exit(-1);
  }

  std::string self_id = argv[1];
  std::string workload_name = argv[2];
  char *memcached_confstr = argv[3];

  std::vector<std::string> peers;

  for (int i = 4; i < argc; i++) {
    std::string current_arg = argv[i];
    std::string peer_arg = "--peer=";
    if (current_arg.find(peer_arg) == 0) {
      auto maybe_peer = current_arg.substr(peer_arg.size());
      if (maybe_peer == self_id) {
        continue;
      }
      peers.push_back(current_arg.substr(peer_arg.size()));
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
            std::make_unique<slope::keyvalue::Memcached>(memcached_confstr),
            "SLOPE_TIMECALIB_"),
        1);
  }
  slope::stat::set_meta(slope::stat::metakey::node_name, self_id);

  auto kv = std::make_unique<slope::keyvalue::Memcached>(memcached_confstr);
  auto slope_kv = std::make_unique<slope::keyvalue::KeyValuePrefixMiddleware>(
      std::move(kv), "SLOPE_");
  if (workload_name == "migvector") {
    slope::stat::set_meta(slope::stat::metakey::workload_name, workload_name);
    auto test_control_plane =
        std::make_unique<slope::control::RdmaControlPlane<td_mig_type>>(
            self_id, peers, std::move(slope_kv));

    testdrive_migrate(std::move(test_control_plane));
  }

  std::cout << slope::stat::get_all_logs().dump(4) << std::endl;
  return 0;
}
