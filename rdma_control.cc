#include "rdma_control.h"

#include <cassert>

#include "mig.h"
#include "data.h"
#include "json.hpp"

#include "debug.h"

namespace slope {
namespace control {

using json = nlohmann::json;

RdmaControlPlane::NodeInfo::NodeInfo(const std::string& _node_id):
  node_id(_node_id) {

}

RdmaControlPlane::RdmaControlPlane(const std::string& self_name,
      const std::vector<std::string>& cluster_nodes,
      slope::keyvalue::KeyValueService::ptr keyvalue_service):
  self_name_(self_name),
  cluster_nodes_(std::move(cluster_nodes)),
  keyvalue_service_(std::move(keyvalue_service)),
  dataplane_(nullptr) {

    if(self_name_ ==
        *min_element(cluster_nodes_.cbegin(), cluster_nodes_.cend())) {
      init_cluster();
    }

    json my_info;
    my_info["node_id"] = self_name_;
    deb("setting:");
    deb(self_name);
    keyvalue_service_->set(self_name_, my_info.dump());
    for(auto peer: cluster_nodes_) {
      std::string peer_info;
      auto peer_result = keyvalue_service_->wait_for(peer, peer_info);
      assert(peer_result);
      cluster_info_[peer] = json::parse(peer_info).get<RdmaControlPlane::NodeInfo>();
    }
}

void RdmaControlPlane::init_cluster() {
  assert(keyvalue_service_->set(migrate_in_progress_cas_name_, "0"));
}

bool RdmaControlPlane::do_migrate(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks) {
  if(!keyvalue_service_->compare_and_swap(
      migrate_in_progress_cas_name_,
      "0",
      "1")) {
    return false;
  }

  start_migrate_ping_pong(dest, chunks);
  // Later TODO: prefill

  for(auto& chunk: chunks) {
    auto mprotect_result = mprotect(
        reinterpret_cast<void*>(chunk.first),
        chunk.second, PROT_READ);
    assert(mprotect_result);
  }

  transfer_ownership_ping_pong(dest, chunks);

  // TODO: broadcast ownership changes

  assert(keyvalue_service_->compare_and_swap(
      migrate_in_progress_cas_name_,
      "1",
      "0"));
  return true;
}

void RdmaControlPlane::attach_dataplane(slope::data::DataPlane::ptr dataplane) {
  dataplane_ = std::move(dataplane);
}

void RdmaControlPlane::start_migrate_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {

}

void RdmaControlPlane::transfer_ownership_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {

}

bool RdmaControlPlane::init_kvservice() {
  return keyvalue_service_->set(migrate_in_progress_cas_name_,
      "0");
}

void to_json(json& j, const RdmaControlPlane::NodeInfo& inf) {
  j = json{
    {"node_id", inf.node_id}
  };
}

void from_json(const json& j, RdmaControlPlane::NodeInfo& inf) {
  j.at("node_id").get_to(inf.node_id);
}


}  // namespace control
}  // namespace slope
