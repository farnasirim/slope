#include "rdma_control.h"

#include <cassert>
#include <arpa/inet.h>
#include <thread>
#include <cstdlib>
#include <malloc.h>

#include "mig.h"
#include "data.h"
#include "discovery.h"

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
      slope::keyvalue::KeyValueService::ptr keyvalue_service):
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
                 static_cast<struct ibv_comp_channel *>(NULL), 0)
{
    if(self_name_ ==
        *min_element(cluster_nodes_.cbegin(), cluster_nodes_.cend())) {
      init_cluster();
    }

    std::vector<QpInfo> do_migrate_qps_list;
    for(auto peer_name: cluster_nodes_) {
      if(peer_name == self_name_) {
        continue;
      }

      // TODO extract constants away
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
      auto attr_ptr = std::make_unique<ibv_qp_init_attr>(qp_init_attr);

      auto qp = std::make_unique<IbvCreateQp>(global_pd_.get(), attr_ptr.get());
      do_migrate_qps_list.emplace_back(
          operating_port_attr_.lid, qp.get()->get()->qp_num, peer_name);
      do_migrate_qps_[peer_name] = std::move(qp);
      do_migrate_qp_attrs[peer_name] = std::move(attr_ptr);
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

      auto qp = std::move(do_migrate_qps_[peer_name]);
      to_rts(*(qp.get()), remote_qp);
      deb(static_cast<int>(qp.get()->get()->state));
      do_migrate_qps_[peer_name] = std::move(qp);
    }

    // prepost the do_migrate recv
    {
      for(auto& qp_ptr: do_migrate_qps_) {
        debout("posting recv to do_migrate_qps:");
        deb(qp_ptr.first);
        auto qp = qp_ptr.second.get()->get();
        do_migrate_sge_.lkey = do_migrate_mr_->lkey;
        do_migrate_sge_.addr = reinterpret_cast<uintptr_t>(do_migrate_mr_->addr);
        do_migrate_sge_.length = sizeof(do_migrate_req_);

        do_migrate_wr_ = ibv_recv_wr{};
        do_migrate_bad_wr_ = nullptr;
        do_migrate_wr_.wr_id = 1212;
        do_migrate_wr_.num_sge = 1;
        do_migrate_wr_.sg_list = &do_migrate_sge_;
        int ret_post_recv = ibv_post_recv(
            qp, &do_migrate_wr_, &do_migrate_bad_wr_);
        assert_p(ret_post_recv == 0, "ibv_post_recv");
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

bool RdmaControlPlane::poll_migrate() {
  std::lock_guard<std::mutex> polling_guard(control_plane_polling_lock_);

  struct ibv_wc completions[1];
  int ret_poll_cq = ibv_poll_cq(do_migrate_cq_, 1, completions);
  assert_p(ret_poll_cq >= 0, "poll_migrate: ibv_poll_cq");
  deb(ret_poll_cq);

  return ret_poll_cq == 1;
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

  {
    do_migrate_req_.number_of_chunks = chunks.size();

    struct ibv_sge sge = {};
    sge.lkey = do_migrate_mr_->lkey;
    sge.addr = reinterpret_cast<uintptr_t>(&do_migrate_req_);
    sge.length = sizeof (do_migrate_req_);

    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};
    this_wr.wr_id = 1212;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;
    this_wr.opcode = IBV_WR_SEND_WITH_IMM;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.imm_data = 123;

    int ret_post_send = ibv_post_send(dest_qp, &this_wr, &bad_wr);
    deb(ret_post_send);
    assert_p(ret_post_send == 0, "ibv_post_send");
    struct ibv_wc completions[1];
    while(true) {
      int ret = ibv_poll_cq(do_migrate_cq_.get(), 1, completions);
      if(ret > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        break;
      }
    }
  }
  debout("done");
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
    {"host_qp_num", inf.host_qp_num},
    {"remote_end_node_id", inf.remote_end_node_id}
  };
}

void from_json(const json& j, QpInfo& inf) noexcept {
  j.at("host_port_lid").get_to(inf.host_port_lid);
  j.at("host_qp_num").get_to(inf.host_qp_num);
  j.at("remote_end_node_id").get_to(inf.remote_end_node_id);
}


void RdmaControlPlane::simple_send() {
  int machine_id = 0;

  size_t amount_data_bytes_to_req = 100; // 10GB
  size_t to_alloc = amount_data_bytes_to_req + 10;
  char *mem = static_cast<char *>(memalign(4096, to_alloc));
  int *p = reinterpret_cast<decltype(p)>(mem);
  int flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  IbvRegMr mr(global_pd_.get(), mem, to_alloc, flags);

  std::cout << "before cq" << std::endl;

  IbvCreateCq cq(ib_context_.get(), dev_attrs_.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0);
  std::cout << "after cq" << std::endl;

  struct ibv_qp_init_attr qp_init_attr = {};
  qp_init_attr.send_cq = cq.get();
  qp_init_attr.recv_cq = cq.get();
  // qp_init_attr.cap.max_send_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr;
  qp_init_attr.cap.max_send_wr = 1;
  // qp_init_attr.cap.max_recv_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr);
  qp_init_attr.cap.max_recv_wr = 1;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  // qp_init.cap.max_inline_data = 60;
  qp_init_attr.qp_type = IBV_QPT_RC;

  IbvCreateQp qp(global_pd_.get(), &qp_init_attr);

  auto target_key = std::to_string(1 - machine_id) + "exch";
  auto adv_key = std::to_string(machine_id) + "exch";

  deb(adv_key);
  deb(target_key);

  auto local_qp = QpInfo(operating_port_attr_.lid, qp.get()->qp_num, target_key);
  keyvalue_service_->set(adv_key, json(local_qp).dump());
  std::string result;
  keyvalue_service_->wait_for(target_key , result);
  QpInfo remote_qp = json::parse(result);
  deb(json::parse(result).dump());
  assert(remote_qp.remote_end_node_id == adv_key);

  to_rts(qp, remote_qp);

  std::cout << "all done" << std::endl;

  char *payload = static_cast<char *>(mem) + 10;
  assert_p(payload != NULL, "payload alloc");

  std::chrono::system_clock::time_point then;


    size_t msg_size_to_req = 100;
    size_t num_messages_to_request = amount_data_bytes_to_req / msg_size_to_req;
    const size_t num_concurr = 1;

    std::cout << " ------------ in " << std::endl;
    // server
    // tell start
    *p = msg_size_to_req;

    {
    struct ibv_sge sge = {};
    sge.lkey = mr->lkey;
    sge.addr = reinterpret_cast<uint64_t>(mem);
    sge.length = sizeof (*p);


    struct ibv_send_wr *bad_wr;
    struct ibv_send_wr this_wr = {};
    this_wr.wr_id = 1212;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;
    this_wr.opcode = IBV_WR_SEND_WITH_IMM;
    this_wr.send_flags = IBV_SEND_SIGNALED;
    this_wr.imm_data = htonl(912999);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int ret_post_send = ibv_post_send(qp, &this_wr, &bad_wr);
    assert_p(ret_post_send >= 0, "polling");
    deb(*p);
    }

    struct ibv_wc completions[num_concurr];
    while(true) {
      int poll_cq_ret = ibv_poll_cq(cq, 1, completions);
      if(poll_cq_ret > 0) {
        assert_p(completions[0].status == 0, "ibv_poll_cq");
        then = std::chrono::system_clock::now();
        break;
      }
    }

}

void RdmaControlPlane::to_rts(IbvCreateQp& qp, QpInfo remote_qp) {
  {
    int ret = qp_attr::modify_qp(qp,
     qp_attr::qp_state(IBV_QPS_INIT),
     qp_attr::pkey_index(0),
     qp_attr::port_num(1),
     qp_attr::qp_access_flags(IBV_ACCESS_REMOTE_READ
       | IBV_ACCESS_REMOTE_WRITE
       | IBV_ACCESS_REMOTE_ATOMIC)
     );

    perror("init");
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
    std::cout << "doing rts" << std::endl;

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

void RdmaControlPlane::simple_recv() {
  int machine_id = 1;

  size_t amount_data_bytes_to_req = 100; // 10GB
  size_t to_alloc = amount_data_bytes_to_req + 10;
  char *mem = static_cast<char *>(memalign(4096, to_alloc));
  int *p = reinterpret_cast<decltype(p)>(mem);
  int flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  IbvRegMr mr(global_pd_.get(), mem, to_alloc, flags);

  std::cout << "before cq" << std::endl;

  IbvCreateCq cq(ib_context_.get(), dev_attrs_.max_cqe, static_cast<void *>(NULL),
                 static_cast<struct ibv_comp_channel *>(NULL), 0);
  std::cout << "after cq" << std::endl;

  struct ibv_qp_init_attr qp_init_attr = {};
  qp_init_attr.send_cq = cq.get();
  qp_init_attr.recv_cq = cq.get();
  // qp_init_attr.cap.max_send_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr;
  qp_init_attr.cap.max_send_wr = 1;
  // qp_init_attr.cap.max_recv_wr = static_cast<uint33_t>(dev_attrs.max_qp_wr);
  qp_init_attr.cap.max_recv_wr = 1;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  // qp_init.cap.max_inline_data = 60;
  qp_init_attr.qp_type = IBV_QPT_RC;

  IbvCreateQp qp(global_pd_.get(), &qp_init_attr);

  auto target_key = std::to_string(1 - machine_id) + "exch";
  auto adv_key = std::to_string(machine_id) + "exch";

  deb(adv_key);
  deb(target_key);

  auto local_qp = QpInfo(operating_port_attr_.lid, qp.get()->qp_num, target_key);
  keyvalue_service_->set(adv_key, json(local_qp).dump());
  std::string result;
  keyvalue_service_->wait_for(target_key , result);
  QpInfo remote_qp = json::parse(result);
  deb(json::parse(result).dump());
  assert(remote_qp.remote_end_node_id == adv_key);

  to_rts(qp, remote_qp);

  std::cout << "all done" << std::endl;

  // constexpr auto cc = f(at, 2);

  char *payload = static_cast<char *>(mem) + 10;
  assert_p(payload != NULL, "payload alloc");

  std::chrono::system_clock::time_point then;

  debout("start recv ###################### ");
  struct ibv_sge sge = {};
  sge.lkey = mr->lkey;
  sge.addr = reinterpret_cast<uint64_t>(mem);
  sge.length = sizeof (*p);

  struct ibv_recv_wr *bad_wr;
  struct ibv_recv_wr this_wr = {};
  this_wr.wr_id = 1212;
  this_wr.num_sge = 1;
  this_wr.sg_list = &sge;

  *p = 123;
  deb(*p);
  int ret = ibv_post_recv(qp, &this_wr, &bad_wr);

  assert_p(ret == 0, "ibv_post_recv");

  struct ibv_wc completions[1];
  while(true) {
    int ret_poll_cq = ibv_poll_cq(cq, 1, completions);
    deb(ret_poll_cq);
    assert_p(ret_poll_cq >= 0, "ibv_poll_cq");
    if(ret_poll_cq > 0) {
      assert_p(completions[0].status == 0, "ibv_poll_cq");
      then = std::chrono::system_clock::now();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  deb(*p);
  auto last_addr = payload;

}

}  // namespace control
}  // namespace slope
