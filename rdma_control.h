#ifndef SLOPE_RDMA_CONTROL_H_
#define SLOPE_RDMA_CONTROL_H_

#include <memory>

#include "control.h"
#include "keyvalue.h"

namespace slope {
namespace control {

class RdmaControlPlane: ControlPlane {
 public:
  using ptr = std::shared_ptr<RdmaControlPlane>;

  virtual bool do_migrate() final override;

 private:
  slope::keyvalue::KeyValueService::ptr keyvalue_service;
};

}  // namespace control
}  // namespace slope

#endif  // SLOPE_RDMA_CONTROL_H_
