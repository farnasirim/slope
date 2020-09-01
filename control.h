#ifndef SLOPE_CONTROL_H_
#define SLOPE_CONTROL_H_

#include <memory>
#include <vector>

#include "allocator.h"
#include "mig.h"

namespace slope {
namespace control {

class MigrationOperation {
 public:
  using ptr = std::unique_ptr<MigrationOperation>;
  virtual bool try_commit() = 0;
  virtual void collect() = 0;
  virtual int get_ready_state() = 0;
};

template<typename T>
class ControlPlane {
 public:
  using ptr = std::unique_ptr<ControlPlane>;

  virtual MigrationOperation::ptr init_migration(const std::string& dest,
      const mig_ptr<T>& ptr) = 0;

  virtual const std::string self_name() = 0;
  virtual const std::vector<std::string> cluster_nodes() = 0;

  virtual ~ControlPlane() = default;
};

}  // namespace control
}  // namespace slope
#endif  // SLOPE_CONTROL_H_
