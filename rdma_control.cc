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

    std::vector<QpInfo> do_migrate_qps_list;
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

      auto qp = std::make_unique<IbvCreateQp>(global_pd_.get(), &qp_init_attr);
      do_migrate_qps_list.emplace_back(
          operating_port_attr_.lid, qp.get()->get()->qp_num, peer_name);
      do_migrate_qps_[peer_name] = std::move(qp);
    }

    NodeInfo my_info(self_name_, do_migrate_qps_list);
    deb(json(my_info).dump(4));
    my_info.node_id = self_name_;

    keyvalue_service_->set(self_name_, json(my_info).dump());
    for(auto peer: cluster_nodes_) {
      std::string peer_info;
      auto peer_result = keyvalue_service_->wait_for(peer, peer_info);
      assert(peer_result);
      cluster_info_[peer] = json::parse(peer_info).get<NodeInfo>();
    }

    for(auto& peer_name: cluster_nodes) {
      if(peer_name == self_name_) {
        continue;
      }

      auto this_node_info = cluster_info_[peer_name];
      QpInfo remote_qp;
      // < c++20 won't allow default operator == :(
      for(auto& it: this_node_info.do_migrate_qps) {
        if(it.remote_end_node_id == self_name_) {
          remote_qp = it;
          break;
        }
      }

      auto the_qp_ptr = do_migrate_qps_[peer_name].get()->get();
      {
        int ret = qp_attr::modify_qp(the_qp_ptr,
         qp_attr::qp_state(IBV_QPS_INIT),
         qp_attr::pkey_index(operating_pkey_),
         qp_attr::port_num(operating_port_num_),
         qp_attr::qp_access_flags(IBV_ACCESS_REMOTE_READ
           | IBV_ACCESS_REMOTE_WRITE
           | IBV_ACCESS_REMOTE_ATOMIC)
         );
        assert_p(ret == 0, "IBV_QPS_INIT");
      }

      {
        struct ibv_ah_attr ah_attrs = {};
        ah_attrs.is_global = 0;
        ah_attrs.dlid = remote_qp.host_port_lid;
        ah_attrs.sl = 0;
        ah_attrs.src_path_bits = 0;
        ah_attrs.port_num = 1;

        int ret = modify_qp(the_qp_ptr,
            qp_attr::qp_state(IBV_QPS_RTR),
            qp_attr::path_mtu(IBV_MTU_4096),
            qp_attr::dest_qp_num(remote_qp.host_qp_num),
            qp_attr::rq_psn(0),
            qp_attr::max_dest_rd_atomic(16),
            qp_attr::min_rnr_timer(12),
            qp_attr::ah_attr(ah_attrs)
            );

        assert_p(ret == 0, "IBV_QPS_RTR");
      }

      {
        int ret = modify_qp(the_qp_ptr,
            qp_attr::qp_state(IBV_QPS_RTS),
            qp_attr::max_rd_atomic(1),
            qp_attr::retry_cnt(7),
            qp_attr::rnr_retry(7),
            qp_attr::sq_psn(0),
            qp_attr::timeout(0x12),
            qp_attr::max_rd_atomic(1)
          );

        assert_p(ret == 0, "IBV_QPS_RTS");
      }
    }

    keyvalue_service_->set(self_name_ + "_DONE", json(my_info).dump());
    for(auto peer: cluster_nodes_) {
      std::string _;
      auto peer_result = keyvalue_service_->wait_for(peer_done_key(peer), _);
    }
    debout("All qps up");
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

std::string RdmaControlPlane::peer_done_key(const std::string& peer_name) {
  return peer_name + "_DONE";
}

void RdmaControlPlane::attach_dataplane(slope::data::DataPlane::ptr dataplane) {
  dataplane_ = std::move(dataplane);
}

void RdmaControlPlane::start_migrate_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {
  auto dest_qp = do_migrate_qps_[dest].get()->get();

}

void RdmaControlPlane::transfer_ownership_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {
}

bool RdmaControlPlane::init_kvservice() {
  return keyvalue_service_->set(migrate_in_progress_cas_name_,
      "0");
}

NodeInfo::NodeInfo(std::string node_id_v,
    const std::vector<QpInfo>& do_migrate_qps_v):
  node_id(node_id_v), do_migrate_qps(do_migrate_qps_v) { }

void to_json(json& j, const NodeInfo& inf) noexcept {
  j = json{
    {"node_id", inf.node_id},
    {"do_migrate_qps", json(inf.do_migrate_qps)}
  };
}

void from_json(const json& j, NodeInfo& inf) noexcept {
  j.at("node_id").get_to(inf.node_id);
  j.at("do_migrate_qps").get_to(inf.do_migrate_qps);
}

QpInfo::QpInfo(short unsigned int host_port_lid_v, unsigned int host_qp_num_v,
    const std::string& remote_end_node_id_v):
  host_port_lid(host_port_lid_v),
  host_qp_num(host_qp_num_v),
  remote_end_node_id(remote_end_node_id_v) { }

void to_json(json& j, const QpInfo& inf) noexcept {
  j = json{
    {"host_port_lid", inf.host_port_lid},
    {"host_qp_num", inf.host_qp_num}
  };
}

void from_json(const json& j, QpInfo& inf) noexcept {
  j.at("host_port_lid").get_to(inf.host_port_lid);
  j.at("host_qp_num").get_to(inf.host_qp_num);
}


}  // namespace control
}  // namespace slope
