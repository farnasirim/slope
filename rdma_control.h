#ifndef SLOPE_RDMA_CONTROL_H_
#define SLOPE_RDMA_CONTROL_H_

#include <memory>
#include <string>

#include "control.h"
#include "keyvalue.h"
#include "data.h"

#include "mig.h"

#include "allocator.h"

namespace slope {
namespace control {

class RdmaControlPlane: public ControlPlane {
 public:
  using ptr = std::shared_ptr<RdmaControlPlane>;
  virtual bool do_migrate(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>&) final override;

  template<typename T>
  bool do_migrate(const std::string& dest, const mig_ptr<T>& p) {
    return do_migrate(dest, p->get_pages());
  }

  RdmaControlPlane(std::string kv_prefix,
      slope::keyvalue::KeyValueService::ptr keyvalue_service,
      slope::dataplane::DataPlane::ptr dataplane);

  bool init_kvservice();

 private:
  void start_migrate_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  void transfer_ownership_ping_pong(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>& chunks);

  static inline const std::string migrate_in_progress_cas_name_ =
    "MIGRATE_IN_PROGRESS_CAS";

  slope::keyvalue::KeyValueService::ptr keyvalue_service_;
  slope::dataplane::DataPlane::ptr dataplane_;
};

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
