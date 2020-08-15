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
void set_meta(const std::string& key, const std::string& val);

json get_all_logs();

namespace key {
const std::string operation = "operation";
const std::string meta = "logging";
const std::string warn = "warning";
const std::string log = "logging";
}  // namespace key

namespace metakey {
const std::string workload_name = "workload_name";
}  // namespace metakey

namespace value{
const std::string done_time_calibrate = "done_time_calibrate";
}  // namespace value

}  // namespace time
}  // namespace slope

#endif  // SLOPE_STAT_H_


