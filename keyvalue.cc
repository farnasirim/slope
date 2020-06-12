#include "keyvalue.h"

namespace slope {
namespace keyvalue {

KeyValuePrefixMiddleware::KeyValuePrefixMiddleware(
    std::unique_ptr<KeyValueService> impl, const std::string prefix):
  impl_(std::move(impl)),
  prefix_(prefix) {

}

bool KeyValuePrefixMiddleware::compare_and_swap(const std::string& key,
    const std::string& oldv, const std::string& newv)  {
  return impl_->compare_and_swap(prefix_ + key, oldv, newv);
}

bool KeyValuePrefixMiddleware::set(const std::string& key, const std::string& val) {
  return impl_->compare_and_swap(prefix_ + key, key, val);
}


bool KeyValuePrefixMiddleware::get(const std::string& key, std::string& ret) {
  return impl_->get(prefix_ + key, ret);
}

bool KeyValuePrefixMiddleware::wait_for(const std::string& key, std::string& ret) {
  return impl_->wait_for(prefix_ + key, ret);
}

}  // namespace slope
}  // namespace keyvalue
