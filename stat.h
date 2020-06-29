#ifndef SLOPE_STAT_H_
#define SLOPE_STAT_H_

#include <chrono>
#include <string>
#include <unordered_map>

#include "json.hpp"

namespace slope {
namespace stat {

using json = nlohmann::json;

struct LogEntry {
   std::chrono::nanoseconds relative_timestamp;
  std::string value;

  LogEntry(const std::string&);
};

// TODO: make this thread safe

void to_json(json& j, const LogEntry& e) noexcept;

void add_value(const std::string& key, const std::string& val);

json get_all_logs();

namespace key {
  const std::string operation = "operation";
  const std::string node = "node";
}  // namespace key

namespace value{
  const std::string done_time_calibrate = "done_time_calibrate";
}  // namespace value

}  // namespace time
}  // namespace slope

#endif  // SLOPE_STAT_H_


