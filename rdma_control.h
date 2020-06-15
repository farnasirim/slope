#ifndef SLOPE_RDMA_CONTROL_H_
#define SLOPE_RDMA_CONTROL_H_

#include <memory>
#include <mutex>
#include <string>

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
      const std::string& remote_end_node_id);
  short unsigned int host_port_lid;
  unsigned int host_qp_num;
  std::string remote_end_node_id;
};

struct NodeInfo {
  NodeInfo() = default;
  NodeInfo(std::string node_id, const std::vector<QpInfo>& qps);
  std::string node_id;
  std::vector<QpInfo> do_migrate_qps;
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

class RdmaControlPlane: public ControlPlane {

 public:
  const std::string self_name_;
  const std::vector<std::string> cluster_nodes_;

  using ptr = std::unique_ptr<RdmaControlPlane>;
  virtual bool do_migrate(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>&) final override;

  bool poll_migrate();

  template<typename T>
  bool do_migrate(const std::string& dest, const mig_ptr<T>& p) {
    return do_migrate(dest, p->get_pages());
  }

  RdmaControlPlane(const std::string& self_name,
      const std::vector<std::string>& cluster_nodes,
      slope::keyvalue::KeyValueService::ptr keyvalue_service);

  bool init_kvservice();

  void attach_dataplane(slope::data::DataPlane::ptr );

  const std::string self_name() final override;
  const std::vector<std::string> cluster_nodes() final override;

  void simple_send();
  void simple_recv();

 private:
  void init_cluster();

  std::map<std::string, NodeInfo> cluster_info_;

  void start_migrate_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  void transfer_ownership_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  static inline const std::string migrate_in_progress_cas_name_ =
    "MIGRATE_IN_PROGRESS_CAS";

  std::string peer_done_key(const std::string&);

  void to_rts(IbvCreateQp&, QpInfo);

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
  DoMigrateRequest do_migrate_req_;
  IbvRegMr do_migrate_mr_;
  IbvCreateCq do_migrate_cq_;
  ibv_recv_wr do_migrate_wr_;
  ibv_recv_wr *do_migrate_bad_wr_;
  ibv_sge do_migrate_sge_;
  std::map<std::string, std::unique_ptr<IbvCreateQp>> do_migrate_qps_;
  // **********************************************************

  // std::vector<std::shared_ptr<
};


void to_json(json& j, const NodeInfo& inf) noexcept;
void from_json(const json& j, NodeInfo& inf) noexcept;
void to_json(json& j, const QpInfo& inf) noexcept;
void from_json(const json& j, QpInfo& inf) noexcept;

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
