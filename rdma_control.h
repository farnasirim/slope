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
  FullMeshQpSet(const IbvCreateCq& cq);
  std::vector<QpInfo> prepare(const std::string& excl,
    const std::vector<std::string>& nodes, const IbvAllocPd& pd,
    struct ibv_port_attr& port_attrs, const struct ibv_device_attr& dev_attrs);
  void finalize(const std::vector<QpInfo>& remote_qps);

  const IbvCreateCq& cq_;
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

  // void calibrate_time();

  template<typename T>
  bool do_migrate(const std::string& dest, const mig_ptr<T>& p) {
    return do_migrate(dest, p->get_pages());
  }

  RdmaControlPlane(const std::string& self_name,
      const std::vector<std::string>& cluster_nodes,
      slope::keyvalue::KeyValueService::ptr keyvalue_service,
      int calibrate_time=0);

  bool is_leader();
  bool is_leader(const std::string& name);
  std::string get_leader();
  void post_calibrate_time(const std::string&);
  void do_calibrate_time();
  void do_calibrate_time_leader();
  void do_calibrate_time_follower();

  IbvCreateCq& get_cq(const std::string& str);

  template<typename T>
  mig_ptr<T> poll_migrate() {
    // TODO: repost the recvs to do_migrate_qp
    std::lock_guard<std::mutex> polling_guard(control_plane_polling_lock_);

    size_t peer_index;
    {
      struct ibv_wc completions[1];
      int ret_poll_cq = ibv_poll_cq(do_migrate_cq_, 1, completions);
      assert_p(ret_poll_cq >= 0 && ret_poll_cq <= 1, "poll_migrate: ibv_poll_cq");
      if(ret_poll_cq == 0) {
        return mig_ptr<T>::adopt(nullptr);
      }

      deb(do_migrate_req_.number_of_chunks);
      peer_index = ntohl(completions[0].imm_data);
    }
    deb(peer_index);
    ibv_qp *peer_qp = fullmesh_qps_[shared_address_qps_key_].qps_[cluster_nodes_[peer_index]].get()->get();

    std::vector<DoMigrateChunk> chunks_info(do_migrate_req_.number_of_chunks);
    auto chunks_info_sz = chunks_info.size() * sizeof(DoMigrateChunk);

    deb(chunks_info_sz);
    deb(chunks_info.size());

    IbvRegMr mr(global_pd_.get(), chunks_info.data(), chunks_info_sz,
        do_migrate_mr_flags_);

    {
      struct ibv_sge sge = {};
      sge.lkey = mr->lkey;
      sge.addr = reinterpret_cast<uint64_t>(chunks_info.data());
      sge.length = chunks_info_sz;

      struct ibv_recv_wr *bad_wr;
      struct ibv_recv_wr this_wr = {};
      this_wr.wr_id = chunks_info_wrid_;
      this_wr.num_sge = 1;
      this_wr.sg_list = &sge;

      int ret = ibv_post_recv(peer_qp, &this_wr, &bad_wr);
      assert_p(ret == 0, "ibv_post_recv");
    }


    {
      struct ibv_wc chunks_completions[1];
      while(true) {
        int chunks_ret_poll_cq = ibv_poll_cq(
            do_migrate_cq_, 1, chunks_completions);
        assert_p(chunks_ret_poll_cq >= 0 && chunks_ret_poll_cq <= 1,
            "ibv_poll_cq");
        if(chunks_ret_poll_cq > 0) {
          assert_p(chunks_completions[0].status == 0, "ibv_poll_cq");
          assert(chunks_completions[0].wr_id == chunks_info_wrid_);
          break;
        }
      }
    }

    std::vector<slope::alloc::memory_chunk> chunks;
    for(auto [addr, sz]: chunks_info) {
      deb2(addr, sz);
      chunks.emplace_back(addr, sz);
    }
    // {
    //   std::stringstream deb_ss;
    //   deb_ss << std::showbase << std::internal << std::setfill('0')
    //     << "addr: " << std::hex << std::setw(16) << static_cast<void*>(chunks_info.data());
    //   infoout(deb_ss.str());
    // }
    // debout("done");

    auto& t_allocator = alloc::allocator_instance<T>();
    // // HUUUUGE TODO: owner is not necessarily the first page
    auto raw = t_allocator.register_preowned(
        *min_element(chunks.begin(), chunks.end()), chunks);

    // confirm receiving
    {
      struct ibv_send_wr *bad_wr;
      struct ibv_send_wr this_wr = {};
      this_wr.wr_id = received_chunks_wrid_;
      this_wr.num_sge = 0;
      this_wr.sg_list = NULL;
      this_wr.opcode = IBV_WR_SEND_WITH_IMM;
      this_wr.send_flags = IBV_SEND_SIGNALED;
      this_wr.imm_data = htonl(self_index_);
      int ret_post_send = ibv_post_send(peer_qp, &this_wr, &bad_wr);
      assert_p(ret_post_send == 0, "ibv_post_send");

      while(true) {
        struct ibv_wc completions[1];
        int ret = ibv_poll_cq(do_migrate_cq_.get(), 1, completions);
        if(ret > 0) {
          assert_p(completions[0].status == 0, "ibv_poll_cq");
          assert(completions[0].wr_id == received_chunks_wrid_);
          break;
        }
      }
    }

    debout("before return");

    while(true) {

    }

    return mig_ptr<T>::adopt(raw);
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
  static inline const std::string time_calib_qps_key_ =
    "time_calib_qps";
  static inline const int time_calib_rounds_ = 10;
  static inline const uint64_t do_migrate_wrid_ = 0xd017;
  static inline const uint64_t calibrate_time_wrid_ = 0xd018;
  static inline const uint64_t chunks_info_wrid_= 0xd019;
  static inline const uint64_t received_chunks_wrid_ = 0xd01a;

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
  IbvCreateCq time_calib_cq_;
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

std::string time_point_to_string(const std::chrono::high_resolution_clock::time_point&);

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
