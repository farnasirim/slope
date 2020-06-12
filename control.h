#ifndef SLOPE_CONTROL_H_
#define SLOPE_CONTROL_H_

#include <memory>
#include <vector>

#include "allocator.h"

namespace slope {
namespace control {

class ControlPlane {
 public:
  using ptr = std::unique_ptr<ControlPlane>;

  virtual bool do_migrate(const std::string& dest,
      const std::vector<slope::alloc::memory_chunk>&) = 0;
};

}  // namespace control
}  // namespace slope
#endif  // SLOPE_CONTROL_H_
