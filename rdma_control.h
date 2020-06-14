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
extern "C" {
struct NodeInfo {
  std::string node_id;
};

struct DoMigrateRequest {

};
}


class RdmaControlPlane: public ControlPlane {

 public:
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

  bool init_kvservice();

  void attach_dataplane(slope::data::DataPlane::ptr );

 private:
  void init_cluster();

  std::map<std::string, NodeInfo> cluster_info_;

  void start_migrate_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  void transfer_ownership_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  static inline const std::string migrate_in_progress_cas_name_ =
    "MIGRATE_IN_PROGRESS_CAS";

  // ************ order is important here *********************
  static inline const std::string ib_device_name_ = "mlx5_1";
  const std::string self_name_;
  const std::vector<std::string> cluster_nodes_;
  slope::keyvalue::KeyValueService::ptr keyvalue_service_;
  slope::data::DataPlane::ptr dataplane_;

  std::mutex control_plane_polling_lock_;

  IbvDeviceContextByName ib_context_;
  int dev_attrs_result_;
  ibv_device_attr dev_attrs;
  int query_port_result_;
  static inline const uint8_t operating_port_num_ = 1;
  struct ibv_port_attr operating_port_attr_;
  IbvAllocPd global_pd_;
  static inline const int do_migrate_mr_flags_ =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
  DoMigrateRequest do_migrate_req_;
  IbvRegMr do_migrate_mr_;
  IbvCreateCq do_migrate_cq_;
  std::unordered_map<std::string, std::unique_ptr<IbvCreateQp>> do_migrate_qps_;
  // **********************************************************

  // std::vector<std::shared_ptr<
};


void to_json(json& j, const NodeInfo& inf);
void from_json(const json& j, NodeInfo& inf);

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
