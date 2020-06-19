#ifndef SLOPE_RDMA_CONTROL_H_
#define SLOPE_RDMA_CONTROL_H_

#include <memory>
#include <mutex>
#include <string>
#include <arpa/inet.h>
#include <map>

#include "control.h"
#include "keyvalue.h"
#include "data.h"
#include "ib.h"
#include "ib_container.h"
#include "modify_qp.h"

#include "mig.h"

#include "allocator.h"

#include "json.hpp"

namespace slope {
namespace control {

using json = nlohmann::json;

struct QpInfo {
  QpInfo() = default;
  QpInfo(short unsigned int host_port_lid,
      unsigned int host_qp_num,
      const std::string& host_node_id,
      const std::string& remote_end_node_id);
  short unsigned int host_port_lid;
  unsigned int host_qp_num;
  std::string host_node_id;
  std::string remote_end_node_id;
};

struct NodeInfo {
  NodeInfo() = default;
  NodeInfo(std::string node_id, const std::map<std::string, std::vector<QpInfo>>& qps);
  std::string node_id;
  std::map<std::string, std::vector<QpInfo>> qp_sets_;
};

extern "C" {

struct DoMigrateRequest {
  size_t number_of_chunks;
};

struct DoMigrateChunk {
  uintptr_t addr;
  size_t sz;
};
}

class FullMeshQpSet {
 public:
  FullMeshQpSet();
  std::vector<QpInfo> prepare(const std::string& excl,
    const std::vector<std::string>& nodes, const IbvAllocPd& pd,
    const IbvCreateCq& cq, struct ibv_port_attr& port_attrs);
  void finalize(const std::vector<QpInfo>& remote_qps);

  std::map<std::string, std::shared_ptr<IbvCreateQp>> qps_;
  std::map<std::string, std::shared_ptr<ibv_qp_init_attr>> qp_attrs_;
};

class RdmaControlPlane: public ControlPlane {

 public:
  const std::string self_name_;
  size_t self_index_;
  std::vector<std::string> cluster_nodes_;

  using ptr = std::unique_ptr<RdmaControlPlane>;
  virtual bool do_migrate(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>&) final override;

  template<typename T>
  bool do_migrate(const std::string& dest, const mig_ptr<T>& p) {
    return do_migrate(dest, p->get_pages());
  }

  RdmaControlPlane(const std::string& self_name,
      const std::vector<std::string>& cluster_nodes,
      slope::keyvalue::KeyValueService::ptr keyvalue_service);

  template<typename T>
  mig_ptr<T> poll_migrate() {
    // TODO: repost the recvs
    std::lock_guard<std::mutex> polling_guard(control_plane_polling_lock_);

    struct ibv_wc completions[1];
    int ret_poll_cq = ibv_poll_cq(do_migrate_cq_, 1, completions);
    assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "poll_migrate: ibv_poll_cq");
    if(ret_poll_cq == 0) {
      return mig_ptr<T>::adopt(nullptr);
    }

    deb(do_migrate_req_.number_of_chunks);
    size_t peer_index = ntohl(completions[0].imm_data);
    deb(peer_index);
    ibv_qp *peer_qp = fullmesh_qps_[do_migrate_qps_key_].qps_[cluster_nodes_[peer_index]].get()->get();
    DoMigrateChunk *chunks_info = new DoMigrateChunk[do_migrate_req_.number_of_chunks];
    auto chunks_info_sz = do_migrate_req_.number_of_chunks * sizeof(DoMigrateChunk);

    {
      IbvRegMr mr(global_pd_.get(), chunks_info,
          sizeof(DoMigrateRequest) * do_migrate_req_.number_of_chunks,
          do_migrate_mr_flags_);

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

    struct ibv_sge sge = {};
    IbvRegMr mr(global_pd_.get(), chunks_info, chunks_info_sz, do_migrate_mr_flags_);
    sge.lkey = mr->lkey;
    sge.addr = reinterpret_cast<uint64_t>(chunks_info);
    sge.length = chunks_info_sz;

    struct ibv_recv_wr *bad_wr;
    struct ibv_recv_wr this_wr = {};
    this_wr.wr_id = do_migrate_wrid_;
    this_wr.num_sge = 1;
    this_wr.sg_list = &sge;

    int ret = ibv_post_recv(peer_qp, &this_wr, &bad_wr);

    assert_p(ret == 0, "ibv_post_recv");
    assert(false);

//   struct ibv_wc completions[1];
//   while(true) {
//     int ret_poll_cq = ibv_poll_cq(cq, 1, completions);
//     deb(ret_poll_cq);
//     assert_p(ret_poll_cq >= 0, "ibv_poll_cq");
//     if(ret_poll_cq > 0) {
//       assert_p(completions[0].status == 0, "ibv_poll_cq");
//       then = std::chrono::system_clock::now();
//       break;
//     }
//     std::this_thread::sleep_for(std::chrono::milliseconds(100));
//   }
//   deb(*p);
//   auto last_addr = payload;
// 
// 


    // std::vector<slope::alloc::memory_chunk> chunks;
    // for(size_t i = 0; i < do_migrate_req_.number_of_chunks; i++) {
    //   deb(chunks_info[i].addr);
    //   deb(chunks_info[i].sz);
    //   chunks.emplace_back(chunks_info[i].addr, chunks_info[i].sz);
    // }
    // delete[] chunks_info;

    // auto t_allocator = alloc::allocator_instance<T>();
    // // HUUUUGE TODO: owner is not necessarily the first page
    // auto raw = t_allocator.register_preowned(
    //     *min_element(chunks.begin(), chunks.end()), chunks);

    // return mig_ptr<T>::adopt(raw);
//   }
  }

  bool init_kvservice();

  void attach_dataplane(slope::data::DataPlane::ptr );

  const std::string self_name() final override;
  const std::vector<std::string> cluster_nodes() final override;

  void simple_send();
  void simple_recv();

 private:
  void init_cluster();

  std::map<std::string, NodeInfo> cluster_info_;
  void prepost_do_migrate_qp(ibv_qp *qp);

  QpInfo find_self_qp(const std::vector<QpInfo>& infos);
  std::vector<QpInfo> find_self_qps(
      const std::map<std::string, NodeInfo>& node_infos,
      const std::string& qp_set_key);


  void start_migrate_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  void transfer_ownership_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  static inline const std::string migrate_in_progress_cas_name_ =
    "MIGRATE_IN_PROGRESS_CAS";
  static inline const std::string do_migrate_qps_key_ =
    "do_migrate_qps";
  static inline const std::string shared_address_qps_key_ =
    "shared_address_qps";
  static inline const uint64_t do_migrate_wrid_ = 0xd017;

  std::string peer_done_key(const std::string&);

  // ************ order is important here *********************
  static inline const std::string ib_device_name_ = "mlx5_1";
  slope::keyvalue::KeyValueService::ptr keyvalue_service_;
  slope::data::DataPlane::ptr dataplane_;

  std::mutex control_plane_polling_lock_;

  IbvDeviceContextByName ib_context_;
  int dev_attrs_result_;
  ibv_device_attr dev_attrs_;
  static inline const uint16_t operating_pkey_ = 0;
  int query_port_result_;
  static inline const uint8_t operating_port_num_ = 1;
  struct ibv_port_attr operating_port_attr_;
  IbvAllocPd global_pd_;
  static inline const int do_migrate_mr_flags_ =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
  static inline const int shared_address_mr_flags_ =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
  DoMigrateRequest do_migrate_req_;
  IbvRegMr do_migrate_mr_;
  IbvCreateCq do_migrate_cq_;
  // TODO: This is incorrect for more than 1 peer. Must have (wr,sge) set per
  // peer.
  ibv_recv_wr do_migrate_wr_;
  ibv_recv_wr *do_migrate_bad_wr_;
  ibv_sge do_migrate_sge_;

  std::map<std::string, FullMeshQpSet> fullmesh_qps_;
  // **********************************************************

  // std::vector<std::shared_ptr<
};

void to_rts(IbvCreateQp&, QpInfo);

void to_json(json& j, const NodeInfo& inf) noexcept;
void from_json(const json& j, NodeInfo& inf) noexcept;
void to_json(json& j, const QpInfo& inf) noexcept;
void from_json(const json& j, QpInfo& inf) noexcept;

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
