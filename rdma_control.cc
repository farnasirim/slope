#include "rdma_control.h"

#include <cassert>

#include "mig.h"
#include "data.h"

#include "json.hpp"

#include "debug.h"

namespace slope {
namespace control {

using json = nlohmann::json;

RdmaControlPlane::RdmaControlPlane(const std::string& self_name,
      const std::vector<std::string>& cluster_nodes,
      slope::keyvalue::KeyValueService::ptr keyvalue_service):
  self_name_(self_name),
  cluster_nodes_(cluster_nodes),
  keyvalue_service_(std::move(keyvalue_service)),
  dataplane_(nullptr),
  ib_context_(ib_device_name_.c_str()),
  dev_attrs_result_(ibv_query_device(ib_context_.get(), &dev_attrs)),
  query_port_result_(ibv_query_port(ib_context_.get(),
        operating_port_num_, &operating_port_attr_)),
  global_pd_(ib_context_.get()),
  do_migrate_mr_(global_pd_.get(), &do_migrate_req_, sizeof(do_migrate_req_),
      do_migrate_mr_flags_),
  do_migrate_cq_(ib_context_.get(), dev_attrs.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0)
{
    if(self_name_ ==
        *min_element(cluster_nodes_.cbegin(), cluster_nodes_.cend())) {
      init_cluster();
    }

    for(auto& peer_name: cluster_nodes_) {
      if(peer_name == self_name_) {
        continue;
      }

      // TODO extrac constants away
      struct ibv_qp_init_attr qp_init_attr = {};
      qp_init_attr.send_cq = do_migrate_cq_.get();
      qp_init_attr.recv_cq = do_migrate_cq_.get();
      // qp_init_attr.cap.max_send_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr;
      qp_init_attr.cap.max_send_wr = 1;
      // qp_init_attr.cap.max_recv_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr);
      qp_init_attr.cap.max_recv_wr = 1;
      qp_init_attr.cap.max_send_sge = 1;
      qp_init_attr.cap.max_recv_sge = 1;
      // qp_init.cap.max_inline_data = 60;
      qp_init_attr.qp_type = IBV_QPT_RC;
    }


    NodeInfo my_info;
    my_info.node_id = self_name_;

    keyvalue_service_->set(self_name_, json(my_info).dump());
    for(auto peer: cluster_nodes_) {
      std::string peer_info;
      auto peer_result = keyvalue_service_->wait_for(peer, peer_info);
      assert(peer_result);
      cluster_info_[peer] = json::parse(peer_info).get<NodeInfo>();
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
  std::lock_guard<std::mutex> polling_guard(control_plane_polling_lock_);

  start_migrate_ping_pong(dest, chunks);
  // Later TODO: prefill
  // Laterer TODO: make sure prefill is pluggable

  for(auto& chunk: chunks) {
    auto mprotect_result = mprotect(
        reinterpret_cast<void*>(chunk.first),
        chunk.second, PROT_READ);
    assert(mprotect_result);
  }

  transfer_ownership_ping_pong(dest, chunks);
  // TODO: at this point we can also call back to the client to let them know
  // that the migration is done, before waiting for the lengthy CAS.

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

void to_json(json& j, const NodeInfo& inf) {
  j = json{
    {"node_id", inf.node_id}
  };
}

void from_json(const json& j, NodeInfo& inf) {
  j.at("node_id").get_to(inf.node_id);
}


}  // namespace control
}  // namespace slope
