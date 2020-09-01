#include "stat.h"

#include <iostream>
#include <mutex>
#include <vector>

#include "cluster_time.h"
#include "debug.h"

namespace slope {
namespace stat {

static std::unordered_map<std::string, std::vector<LogEntry>> logs;
static std::unordered_map<std::string, LogEntry> meta;

static std::mutex lk;

LogEntry::LogEntry(const std::string& value_):
  relative_timestamp(std::chrono::high_resolution_clock::now()
      - slope::time::calibrated_local_start_time),
  value(value_) { }

void add_value(const std::string& key, const std::string& val) {
  std::lock_guard<std::mutex> _(lk);
  logs[key].emplace_back(val);
  debout("add: " + key + "=" + val);
}

void set_param_meta(const std::string& key, const std::string& val) {
  set_meta("param_" + key, val);
}

void set_meta(const std::string& key, const std::string& val) {
  std::lock_guard<std::mutex> _(lk);
  debout("meta: " + key + val);
  meta.emplace(key, LogEntry(val));
}

void to_json(json& j, const LogEntry& e) noexcept {
  j = json{{"nanos", e.relative_timestamp.count()}, {"value", e.value}};
}

json get_all_logs() {
  return json{
      {"time_series", logs},
      {"meta", meta},
  };
}

namespace key {
  extern const std::string operation;
}  // namespace key

namespace value{
  extern const std::string done_time_calibrate;
}  // namespace value

}  // namespace time
}  // namespace slope
