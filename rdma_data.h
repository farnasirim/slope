#ifndef SLOPE_RDMA_DATA_H_
#define SLOPE_RDMA_DATA_H_

#include <memory>

namespace slope {
namespace data {

class RdmaDataPlane {
 public:
  using ptr = std::unique_ptr<RdmaDataPlane>;

  void initialie() {

  }
};

}  // namespace data
}  // namespace slope

#endif // SLOPE_RDMA_DATA_H_


