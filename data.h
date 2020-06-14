#ifndef SLOPE_DATA_H_
#define SLOPE_DATA_H_

#include <memory>

namespace slope {
namespace data {

class DataPlane {
 public:
  using ptr = std::unique_ptr<DataPlane>;
};

}  // namespace dataplane
}  // namespace slope

#endif // SLOPE_DATA_H_


