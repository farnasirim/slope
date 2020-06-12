#ifndef SLOPE_KEYVALUE_H_
#define SLOPE_KEYVALUE_H_

#include <string>
#include <memory>

namespace slope {
namespace keyvalue {

class KeyValueService {
 public:
  virtual bool compare_and_swap(const std::string& key,
      const std::string& oldv, const std::string& newv) = 0;
  virtual bool set(const std::string& key, const std::string& val) = 0;
  using ptr = std::shared_ptr<KeyValueService>;
};

class KeyValuePrefixMiddleware: public KeyValueService {
 public:

  KeyValuePrefixMiddleware(KeyValueService::ptr, const std::string prefix);

  virtual bool compare_and_swap(const std::string& key,
      const std::string& oldv, const std::string& newv) final override;
  virtual bool set(const std::string& key, const std::string& val) final override;
  using ptr = std::shared_ptr<KeyValueService>;
 private:
  KeyValueService::ptr impl_;
  const std::string prefix_;
};

}  // namespace keyvalue
}  // namespace slope

#endif  // SLOPE_KEYVALUE_H_
