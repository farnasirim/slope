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

#include "json.hpp"

#include "debug.h"

namespace slope {
namespace control {

using json = nlohmann::json;

int TwoStepMigrationOperation::get_ready_state() {
  std::lock_guard<std::mutex> lk(*m_);
  return *ready_state_;
}

bool TwoStepMigrationOperation::try_commit() {
  bool ret = false;
  {
    std::lock_guard<std::mutex> lk(*m_);
    if (*ready_state_ == 1) {
      *ready_state_ = 2;
      ret = true;
    } else if (*ready_state_ == 2) {
      throw std::exception();
    }
  }

  if (ret) {
    cv_->notify_all();
  }
  return ret;
}

void TwoStepMigrationOperation::collect() { t_.join(); }

TwoStepMigrationOperation::~TwoStepMigrationOperation() {
  if (t_.joinable()) {
    t_.join();
  }
}

TwoStepMigrationOperation::TwoStepMigrationOperation(
    std::shared_ptr<std::mutex> m, std::shared_ptr<int> ready_state,
    std::shared_ptr<std::condition_variable> cv, std::thread t)
    : m_(m), ready_state_(ready_state), cv_(cv), t_(std::move(t)) {}

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

QpInfo::QpInfo() = default;

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


namespace impl {
static std::unique_ptr<IbvCreateCq> slope_tmp_(nullptr);
}  // namespace impl
FullMeshQpSet::FullMeshQpSet(): cq_(*impl::slope_tmp_) {
  assert(false); // map subscript access need this defined, but it should
  // never run
}
FullMeshQpSet::FullMeshQpSet(const IbvCreateCq& cq): cq_(cq) {

}

void FullMeshQpSet::finalize(const std::vector<QpInfo>& remote_qps) {
  for(auto& remote_qp: remote_qps) {
    auto peer_name = remote_qp.host_node_id;
    auto qp = std::move(qps_[peer_name]);
    to_rts(*(qp.get()), remote_qp);
    qps_[peer_name] = std::move(qp);
  }
}

std::vector<QpInfo> FullMeshQpSet::prepare(
    const std::string& self_name, const std::vector<std::string>& nodes,
    const IbvAllocPd& pd, struct ibv_port_attr& port_attrs,
    const struct ibv_device_attr& dev_attrs, int sig_all) {
  std::vector<QpInfo> ret;

  for(auto peer_name: nodes) {
    if(self_name == peer_name) {
      continue;
    }

    // TODO extract constants away
    struct ibv_qp_init_attr qp_init_attr = {};
    qp_init_attr.send_cq = cq_.get();
    qp_init_attr.recv_cq = cq_.get();
    qp_init_attr.cap.max_send_wr = static_cast<uint32_t>(dev_attrs.max_qp_wr);
    qp_init_attr.cap.max_send_wr = 1024;
    qp_init_attr.cap.max_recv_wr = static_cast<uint32_t>(dev_attrs.max_qp_wr);
    qp_init_attr.cap.max_recv_wr = 1024;
    qp_init_attr.cap.max_send_sge = 20;
    qp_init_attr.cap.max_recv_sge = 20;
    qp_init_attr.sq_sig_all = sig_all;

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
        qp_attr::timeout(14),
        qp_attr::max_rd_atomic(16)
      );

    assert_p(ret == 0, "rts");
  }
}



std::string time_point_to_string(
    const std::chrono::high_resolution_clock::time_point& tp) {
  auto now_tm = std::chrono::high_resolution_clock::to_time_t(tp);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&now_tm), "%F %T");

  return ss.str();
}



}  // namespace control
}  // namespace slope
