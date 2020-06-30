#include "stat.h"

#include <vector>

#include "cluster_time.h"

namespace slope {
namespace stat {

static std::unordered_map<std::string, std::vector<LogEntry>> logs;

LogEntry::LogEntry(const std::string& value_):
  relative_timestamp(std::chrono::high_resolution_clock::now()
      - slope::time::calibrated_local_start_time),
  value(value_) { }

void add_value(const std::string& key, const std::string& val) {
  logs[key].emplace_back(val);
}

void to_json(json& j, const LogEntry& e) noexcept {
  j = json{
    {"nanos", e.relative_timestamp.count()},
    {"value", e.value}
  };
}

json get_all_logs() {
  return json(logs);
}

namespace key {
  extern const std::string operation;
}  // namespace key

namespace value{
  extern const std::string done_time_calibrate;
}  // namespace value

}  // namespace time
}  // namespace slope