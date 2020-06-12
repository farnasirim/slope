#ifndef SLOPE_DATA_H_
#define SLOPE_DATA_H_

#include <memory>

namespace slope {
namespace dataplane {

class DataPlane {
 public:
  using ptr = std::shared_ptr<DataPlane>;
};

}  // namespace dataplane
}  // namespace slope

#endif // SLOPE_DATA_H_


