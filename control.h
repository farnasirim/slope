#ifndef SLOPE_CONTROL_H_
#define SLOPE_CONTROL_H_

#include <memory>

namespace slope {
namespace control {

class ControlPlane {
 public:
  using ptr = std::shared_ptr<ControlPlane>;

  virtual bool do_migrate() = 0;
};

}  // namespace control
}  // namespace slope
#endif  // SLOPE_CONTROL_H_
