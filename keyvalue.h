#ifndef SLOPE_KEYVALUE_H_
#define SLOPE_KEYVALUE_H_

#include <string>
#include <memory>

namespace slope {
namespace keyvalue {

class KeyValueService {
 public:
  virtual bool compare_and_swap(const std::string& oldv, const std::string& newv) = 0;
  using ptr = std::shared_ptr<KeyValueService>;
};

}  // namespace keyvalue
}  // namespace slope

#endif  // SLOPE_KEYVALUE_H_
