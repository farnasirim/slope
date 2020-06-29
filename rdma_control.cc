#include "rdma_control.h"

#include <cassert>
#include <arpa/inet.h>
#include <thread>
#include <cstdlib>
#include <malloc.h>
#include <iomanip>
#include <sstream>

#include "mig.h"
#include "data.h"
#include "discovery.h"
#include "cluster_time.h"

#include "stat.h"

#include "json.hpp"

#include "debug.h"

namespace slope {
namespace control {

using json = nlohmann::json;

const std::string RdmaControlPlane::self_name() {
  return self_name_;
}
const std::vector<std::string> RdmaControlPlane::cluster_nodes() {
  return cluster_nodes_;
}

RdmaControlPlane::RdmaControlPlane(const std::string& self_name,
      const std::vector<std::string>& cluster_nodes,
      slope::keyvalue::KeyValueService::ptr keyvalue_service, int calibrate_time):
  self_name_(self_name),
  cluster_nodes_(cluster_nodes),
  keyvalue_service_(std::move(keyvalue_service)),
  dataplane_(nullptr),
  ib_context_(ib_device_name_.c_str()),
  dev_attrs_result_(ibv_query_device(ib_context_.get(), &dev_attrs_)),
  query_port_result_(ibv_query_port(ib_context_.get(),
        operating_port_num_, &operating_port_attr_)),
  global_pd_(ib_context_.get()),
  do_migrate_mr_(global_pd_.get(), &do_migrate_req_, sizeof(do_migrate_req_),
      do_migrate_mr_flags_),
  do_migrate_cq_(ib_context_.get(), dev_attrs_.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0),
  time_calib_cq_(ib_context_.get(), dev_attrs_.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0)
{
    std::sort(cluster_nodes_.begin(), cluster_nodes_.end());
    self_index_ = static_cast<size_t>(
          std::find(cluster_nodes_.begin(), cluster_nodes_.end(), self_name_) -
          cluster_nodes_.begin()
          );

    if(is_leader()) {
      init_cluster();
    }

    std::map<std::string, std::vector<QpInfo>> all_qps;
    std::vector<std::string> qp_set_keys = {
      do_migrate_qps_key_,
      shared_address_qps_key_,
    };
    if(calibrate_time) {
      qp_set_keys.push_back(time_calib_qps_key_);
    }

    for(auto set_name: qp_set_keys) {
      fullmesh_qps_.try_emplace(set_name, get_cq(set_name));
      auto qps = fullmesh_qps_[set_name].prepare(self_name_,
          cluster_nodes_, global_pd_, operating_port_attr_);
      all_qps[set_name] = qps;
    }

    NodeInfo my_info(self_name_, all_qps);
    // deb(json(my_info).dump(4));
    my_info.node_id = self_name_;

    keyvalue_service_->set(self_name_, json(my_info).dump());
    for(auto peer: cluster_nodes_) {
      std::string peer_info;
      auto peer_result = keyvalue_service_->wait_for(peer, peer_info);
      assert(peer_result);
      cluster_info_[peer] = json::parse(peer_info).get<NodeInfo>();
    }

    for(auto set_name: qp_set_keys) {
      auto remote_qps = find_self_qps(cluster_info_, set_name);
      fullmesh_qps_[set_name].finalize(remote_qps);
    }

    for(auto& qp_ptr: fullmesh_qps_[do_migrate_qps_key_].qps_) {
      auto qp = qp_ptr.second.get()->get();
      prepost_do_migrate_qp(qp);
    }

    if(calibrate_time && !is_leader()) {
      post_calibrate_time(get_leader());
    }

    keyvalue_service_->set(self_name_ + "_DONE", json(my_info).dump());
    for(auto peer: cluster_nodes_) {
      std::string _;
      auto peer_result = keyvalue_service_->wait_for(peer_done_key(peer), _);
    }

    if(calibrate_time) {
      do_calibrate_time();
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

  debout("start ping pong");
  start_migrate_ping_pong(dest, chunks);
  // Later TODO: prefill
  // Laterer TODO: make sure prefill is pluggable

  for(auto& chunk: chunks) {
    deb(chunk);
    auto mprotect_result = mprotect(
        reinterpret_cast<void*>(chunk.first),
        chunk.second, PROT_READ);
    assert_p(mprotect_result == 0, "mprotect");
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

  auto dest_qp = fullmesh_qps_[do_migrate_qps_key_].qps_[dest].get()->get();
  auto& cq = fullmesh_qps_[do_migrate_qps_key_].cq_;

  {
    do_migrate_req_.number_of_chunks = chunks.size();

    struct ibv_sge sge = {};
    sge.lkey = do_migrate_mr_->lkey;
    sge.addr = reinterpret_cast<uintptr_t>(&do_migrate_req_);
    sge.length = sizeof (do_migrate_req_);

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};
    this_wr.wr_id = do_migrate_wrid_;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;
    this_wr.opcode = IBV_WR_SEND_WITH_IMM;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.imm_data = htonl(self_index_);

    int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
    deb(ret_post_send);
    assert_p(ret_post_send == 0, "ibv_post_send");
    struct ibv_wc completions[1];
    while(true) {
      int ret = ibv_poll_cq(cq.get(), 1, completions);
      if(ret > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        break;
      }
    }
  }

  // DoMigrateChunk *chunks_info = new DoMigrateChunk[chunks.size()];
  // size_t current_chunk = 0;
  // for(auto& it: chunks) {
  //   chunks_info[current_chunk].addr = it.first;
  //   chunks_info[current_chunk].sz = it.second;
  // }
  // delete[] chunks_info;
}

void RdmaControlPlane::transfer_ownership_ping_pong(const std::string& dest,
    const std::vector<slope::alloc::memory_chunk>& chunks) {
}

bool RdmaControlPlane::init_kvservice() {
  return keyvalue_service_->set(migrate_in_progress_cas_name_,
      "0");
}

NodeInfo::NodeInfo(std::string node_id_v,
    const std::map<std::string, std::vector<QpInfo>>& qp_sets):
  node_id(node_id_v), qp_sets_(qp_sets) { }

void to_json(json& j, const NodeInfo& inf) noexcept {
  j = json{
    {"node_id", inf.node_id},
    {"qp_sets", json(inf.qp_sets_)}
  };
}

void from_json(const json& j, NodeInfo& inf) noexcept {
  j.at("node_id").get_to(inf.node_id);
  j.at("qp_sets").get_to(inf.qp_sets_);
}

QpInfo::QpInfo(short unsigned int host_port_lid_v, unsigned int host_qp_num_v,
    const std::string& host_node_id_v, const std::string& remote_end_node_id_v):
  host_port_lid(host_port_lid_v),
  host_qp_num(host_qp_num_v),
  host_node_id(host_node_id_v),
  remote_end_node_id(remote_end_node_id_v) { }

void to_json(json& j, const QpInfo& inf) noexcept {
  j = json{
    {"host_port_lid", inf.host_port_lid},
    {"host_qp_num", inf.host_qp_num},
    {"remote_end_node_id", inf.remote_end_node_id},
    {"host_node_id", inf.host_node_id}
  };
}

void from_json(const json& j, QpInfo& inf) noexcept {
  j.at("host_port_lid").get_to(inf.host_port_lid);
  j.at("host_qp_num").get_to(inf.host_qp_num);
  j.at("host_node_id").get_to(inf.host_node_id);
  j.at("remote_end_node_id").get_to(inf.remote_end_node_id);
}

QpInfo RdmaControlPlane::find_self_qp(const std::vector<QpInfo>& infos) {
  for(const auto& it: infos) {
    if(it.remote_end_node_id == self_name_) {
      return it;
    }
  }
  assert(false);
}

std::vector<QpInfo> RdmaControlPlane::find_self_qps(const std::map<std::string, NodeInfo>& node_infos,
    const std::string& qp_set_key) {
  std::vector<QpInfo> ret;
  for(const auto& it: node_infos) {
    if(it.first != self_name_) {
      ret.push_back(find_self_qp(it.second.qp_sets_.find(qp_set_key)->second));
    }
  }
  return ret;
}

IbvCreateCq& RdmaControlPlane::get_cq(const std::string& str) {
  if(str == time_calib_qps_key_) {
    return time_calib_cq_;
  }
  return do_migrate_cq_;
}

FullMeshQpSet::FullMeshQpSet(): cq_(*std::unique_ptr<IbvCreateCq>(nullptr)) {
  assert(false); // map subscript access need this defined, but it should
  // never run
}
FullMeshQpSet::FullMeshQpSet(const IbvCreateCq& cq): cq_(cq) {

}

std::vector<QpInfo> FullMeshQpSet::prepare(const std::string& self_name,
    const std::vector<std::string>& nodes, const IbvAllocPd& pd,
    struct ibv_port_attr& port_attrs) {

  std::vector<QpInfo> ret;

  for(auto peer_name: nodes) {
    if(self_name == peer_name) {
      continue;
    }

    // TODO extract constants away
    struct ibv_qp_init_attr qp_init_attr = {};
    qp_init_attr.send_cq = cq_.get();
    qp_init_attr.recv_cq = cq_.get();
    // qp_init_attr.cap.max_send_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr;
    qp_init_attr.cap.max_send_wr = 1024;
    // qp_init_attr.cap.max_recv_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr);
    qp_init_attr.cap.max_recv_wr = 1024;
    qp_init_attr.cap.max_send_sge = 20;
    qp_init_attr.cap.max_recv_sge = 20;
    // qp_init.cap.max_inline_data = 60;
    qp_init_attr.qp_type = IBV_QPT_RC;
    auto attr_ptr = std::make_unique<ibv_qp_init_attr>(qp_init_attr);

    auto qp = std::make_unique<IbvCreateQp>(pd.get(), attr_ptr.get());
    ret.emplace_back(
        port_attrs.lid, qp.get()->get()->qp_num, self_name,peer_name);
    qps_[peer_name] = std::move(qp);
    qp_attrs_[peer_name] = std::move(attr_ptr);
  }

  return ret;
}
void to_rts(IbvCreateQp& qp, QpInfo remote_qp) {
  {
    int ret = qp_attr::modify_qp(qp,
     qp_attr::qp_state(IBV_QPS_INIT),
     qp_attr::pkey_index(0),
     qp_attr::port_num(1),
     qp_attr::qp_access_flags(IBV_ACCESS_REMOTE_READ
       | IBV_ACCESS_REMOTE_WRITE
       | IBV_ACCESS_REMOTE_ATOMIC)
     );

    assert_p(ret == 0, "init");
  }

  {
    struct ibv_ah_attr ah_attrs = {};
    ah_attrs.is_global = 0;
    ah_attrs.dlid = remote_qp.host_port_lid;
    ah_attrs.sl = 0;
    ah_attrs.src_path_bits = 0;
    ah_attrs.port_num = 1;

    int ret = modify_qp(qp,
        qp_attr::qp_state(IBV_QPS_RTR),
        qp_attr::path_mtu(IBV_MTU_4096),
        qp_attr::dest_qp_num(remote_qp.host_qp_num),
        qp_attr::rq_psn(0),
        qp_attr::max_dest_rd_atomic(16),
        qp_attr::min_rnr_timer(12),
        qp_attr::ah_attr(ah_attrs)
        );

    assert_p(ret == 0, "rtr");
  }

  {
    int ret = modify_qp(qp,
        qp_attr::qp_state(IBV_QPS_RTS),
        qp_attr::max_rd_atomic(1),
        qp_attr::retry_cnt(7),
        qp_attr::rnr_retry(7),
        qp_attr::sq_psn(0),
        qp_attr::timeout(0x12),
        qp_attr::max_rd_atomic(1)
      );

    assert_p(ret == 0, "rts");
  }
}

void RdmaControlPlane::prepost_do_migrate_qp(ibv_qp *qp) {
  do_migrate_sge_.lkey = do_migrate_mr_->lkey;
  do_migrate_sge_.addr = reinterpret_cast<uintptr_t>(do_migrate_mr_->addr);
  do_migrate_sge_.length = sizeof(do_migrate_req_);

  do_migrate_wr_ = ibv_recv_wr{};
  do_migrate_bad_wr_ = nullptr;
  do_migrate_wr_.wr_id = do_migrate_wrid_;
  do_migrate_wr_.num_sge = 1;
  do_migrate_wr_.sg_list = &do_migrate_sge_;
  int ret_post_recv = ibv_post_recv(
      qp, &do_migrate_wr_, &do_migrate_bad_wr_);
  assert_p(ret_post_recv == 0, "ibv_post_recv");
}

void FullMeshQpSet::finalize(const std::vector<QpInfo>& remote_qps) {
  for(auto& remote_qp: remote_qps) {
    auto peer_name = remote_qp.host_node_id;
    auto qp = std::move(qps_[peer_name]);
    to_rts(*(qp.get()), remote_qp);
    qps_[peer_name] = std::move(qp);
  }
}

std::string RdmaControlPlane::get_leader() {
  return *min_element(cluster_nodes_.begin(), cluster_nodes_.end());
}

bool RdmaControlPlane::is_leader() {
  return self_name_ == get_leader();
}

bool RdmaControlPlane::is_leader(const std::string& name) {
  return name == get_leader();
}

void RdmaControlPlane::post_calibrate_time(const std::string& remote_node) {
  ibv_recv_wr calibrate_time_wr_ = {};
  ibv_recv_wr *calibrate_time_bad_wr_ = nullptr;
  calibrate_time_wr_.wr_id = calibrate_time_wrid_;
  calibrate_time_wr_.num_sge = 0;
  calibrate_time_wr_.sg_list = nullptr;
  int ret_post_recv = ibv_post_recv(
      fullmesh_qps_[time_calib_qps_key_].qps_[remote_node].get()->get(),
      &calibrate_time_wr_, &calibrate_time_bad_wr_);
  assert_p(ret_post_recv == 0, "ibv_post_recv");
}

void RdmaControlPlane::do_calibrate_time_leader() {
  slope::time::calibrated_local_start_time =
    std::chrono::high_resolution_clock::now();
  // deb(time_point_to_string(slope::time::calibrated_local_start_time));
  // deb(time_point_to_string(slope::time::calibrated_local_start_time));
  for(auto dest: cluster_nodes_) {
    if(is_leader(dest)) {
      continue;
    }
    int current_node_rtt_micros = 0;
    for(int i = 0; i < time_calib_rounds_; i++) {
      auto dest_qp = fullmesh_qps_[time_calib_qps_key_].qps_[dest].get()->get();
      auto& cq = fullmesh_qps_[time_calib_qps_key_].cq_;

      struct ibv_send_wr *bad_wr;
      struct ibv_send_wr this_wr = {};
      this_wr.wr_id = calibrate_time_wrid_;
      this_wr.num_sge = 0;
      this_wr.sg_list = NULL;
      this_wr.opcode = IBV_WR_SEND_WITH_IMM;
      this_wr.send_flags = IBV_SEND_SIGNALED;
      auto current_node_offset_micros =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() -
            slope::time::calibrated_local_start_time
            ).count();
      this_wr.imm_data = htonl(current_node_rtt_micros/2 +
          current_node_offset_micros);

      post_calibrate_time(dest);

      auto then = std::chrono::high_resolution_clock::now();
      int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
      assert_p(ret_post_send == 0, "ibv_post_send");
      while(true) {
        struct ibv_wc completions[1];
        int ret = ibv_poll_cq(cq.get(), 1, completions);
        if(ret > 0) {
          assert_p(completions[0].status == 0, "ibv_poll_cq");
          break;
        }
      }

      while(true) {
        struct ibv_wc completions[1];
        int ret = ibv_poll_cq(cq.get(), 1, completions);
        if(ret > 0) {
          assert_p(completions[0].status == 0, "ibv_poll_cq");
          break;
        }
      }
      current_node_rtt_micros = std::chrono::duration_cast<
        std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - then).count();
    }
  }
}

void RdmaControlPlane::do_calibrate_time_follower() {
  auto leader_qp = fullmesh_qps_[time_calib_qps_key_].qps_[get_leader()].get()->get();
  auto& cq = fullmesh_qps_[time_calib_qps_key_].cq_;
  std::vector<std::chrono::high_resolution_clock::time_point> time_points;
  for(int i = 0; i < time_calib_rounds_; i++) {
    post_calibrate_time(get_leader());
    while(true) {
      struct ibv_wc completions[1];
      int ret = ibv_poll_cq(cq.get(), 1, completions);
      if(ret > 0) {
        time_points.push_back(
          std::chrono::high_resolution_clock::now() -
            std::chrono::microseconds(ntohl(completions[0].imm_data))
            );
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        break;
      }
    }

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};
    this_wr.wr_id = calibrate_time_wrid_;
    this_wr.num_sge = 0;
    this_wr.sg_list = NULL;
    this_wr.opcode = IBV_WR_SEND_WITH_IMM;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.imm_data = htonl(self_index_);
    int ret_post_send = ibv_post_send(leader_qp, &this_wr, &bad_wr);

    while(true) {
      struct ibv_wc completions[1];
      int ret = ibv_poll_cq(cq.get(), 1, completions);
      if(ret > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        break;
      }
    }
  }
  int selected_ind = time_points.size()/2;
  std::nth_element(time_points.begin(),
      time_points.begin() + selected_ind,
      time_points.end());
  slope::time::calibrated_local_start_time =
    *(time_points.begin() + selected_ind);
}

std::string time_point_to_string(
    const std::chrono::high_resolution_clock::time_point& tp) {
  auto now_tm = std::chrono::high_resolution_clock::to_time_t(tp);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&now_tm), "%F %T");

  return ss.str();
}

void RdmaControlPlane::do_calibrate_time() {
  if(is_leader()) {
    do_calibrate_time_leader();
  } else {
    do_calibrate_time_follower();
  }

  slope::stat::add_value(slope::stat::key::operation,
      slope::stat::value::done_time_calibrate);
}


}  // namespace control
}  // namespace slope
