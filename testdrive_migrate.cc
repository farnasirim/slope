#include "testdrive_migrate.h"

#include <unistd.h>
#include <iomanip>

#include "discovery.h"
#include "ib.h"
#include "ib_container.h"
#include "logging.h"
#include "modify_qp.h"

#include "debug.h"
#include "mig.h"
#include "allocator.h"

template<typename T>
using mig_vector = std::vector<T, slope::alloc::FixedPoolAllocator<T>>;

template<typename T>
using mig_alloc = slope::alloc::FixedPoolAllocator<T>;

#include "json.hpp"

using json = nlohmann::json;

void to_json(json& j, const NodeInfo& inf) {
  j = json{
    {"node_id", inf.node_id}
  };
}

void from_json(const json& j, NodeInfo& inf) {
  j.at("node_id").get_to(inf.node_id);
}

void do_common(slope::discovery::DiscoverySerivce& d, const char *node_id) {
  json info ;
  info["node_id"] = node_id;

  char name[200];
  assert(!gethostname(name, 200));
  if(std::strcmp(name, "colonelthink")) {
    IbvDeviceContextByName ib_context("mlx5_1");
    IbvAllocPd pd(ib_context.get());
  }

  deb(info);
  d.register_node(node_id, info.dump());
  auto peers_info = d.wait_for_peers();


  if(node_id == min_element(peers_info.begin(), peers_info.end())->first) {
    // init control plane
  }

  for(auto inf: peers_info) {
    deb(inf.first);
    deb(inf.second);
    deb(json::parse(inf.second).dump(4));
    // std::cout << std::setw(4) <<  json(inf.second).dump(4) << std::endl;
  }

}

void do_src(slope::discovery::DiscoverySerivce& d, const char *node_id) {
  do_common(d, node_id);

  debout("first");
  slope::mig_ptr<mig_vector<int>> ptr(static_cast<size_t>(10), 0);
  std::cout << "before" << std::endl;
  deb(slope::alloc::global_ownership_stack.size());
  {
    deb(slope::alloc::global_ownership_stack.size());
    auto lock = ptr.create_context();
    deb(slope::alloc::global_ownership_stack.size());
    for(size_t i = 0;i < 10; i++) {
      (*ptr.get())[i] = i;
    }
  }
  deb(slope::alloc::global_ownership_stack.size());

  for(size_t i = 0;i < 10; i++) {
    std::cout << (*ptr.get())[i] << " ";
  }
  std::cout << std::endl;

  std::cout << "done" << std::endl;
  deb(*ptr.get());

  for(auto it: ptr.get_pages()) {
    deb(it);
    // rdma address: it.first with size it.second to another machine
  }

  deb(slope::alloc::global_ownership_stack.size());
  // for(auto& it: slope::alloc::global_ownership_stack) {
  //   it.
  // }

  // IbvDeviceContextByName ib_context("mlx5_1");
  // IbvAllocPd pd(ib_context.get());

  // int flags =
  //     IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

}

void do_snk(slope::discovery::DiscoverySerivce& d, const char *node_id) {
  do_common(d, node_id);
}

void testdrive_migrate(slope::discovery::DiscoverySerivce& d, const char *node_id) {
  if(std::string(node_id).find("0") != std::string::npos) {
    do_src(d, node_id);
  } else {
    do_snk(d, node_id);
  }
}
