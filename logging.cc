#include "logging.h"

#include <iostream>

#include "stat.h"

namespace slope {
namespace logging {

void warn(const std::string& msg) {
  std::cerr << "[WAR] " << msg << std::endl;
  slope::stat::add_value(slope::stat::key::warn, msg);
}

void log(const std::string& msg) {
  std::cerr << "[LOG] " << msg << std::endl;
  slope::stat::add_value(slope::stat::key::log, msg);
}
}
}  // namespace slope
