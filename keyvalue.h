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
  virtual bool get(const std::string& key, std::string& ret) = 0;
  virtual bool wait_for(const std::string& key, std::string& ret) = 0;

  using ptr = std::unique_ptr<KeyValueService>;
};

class KeyValuePrefixMiddleware: public KeyValueService {
 public:

  KeyValuePrefixMiddleware(std::unique_ptr<KeyValueService> impl, const std::string prefix);

  virtual bool compare_and_swap(const std::string& key,
      const std::string& oldv, const std::string& newv) final override;
  virtual bool set(const std::string& key, const std::string& val) final override;
  virtual bool get(const std::string& key, std::string& ret) final override;
  virtual bool wait_for(const std::string& key, std::string& ret) final override;
  using ptr = std::unique_ptr<KeyValueService>;
 private:
  std::unique_ptr<KeyValueService> impl_;
  const std::string prefix_;
};

}  // namespace keyvalue
}  // namespace slope

#endif  // SLOPE_KEYVALUE_H_
