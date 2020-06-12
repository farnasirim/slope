#include "keyvalue.h"

namespace slope {
namespace keyvalue {

KeyValuePrefixMiddleware::KeyValuePrefixMiddleware(KeyValueService::ptr impl,
    const std::string prefix):
  impl_(impl),
  prefix_(prefix) {

}

bool KeyValuePrefixMiddleware::compare_and_swap(const std::string& key,
    const std::string& oldv, const std::string& newv)  {
  return impl_->compare_and_swap(prefix_ + key, oldv, newv);
}

bool KeyValuePrefixMiddleware::set(const std::string& key, const std::string& val) {
  return impl_->compare_and_swap(prefix_ + key, key, val);
}

}  // namespace slope
}  // namespace keyvalue
