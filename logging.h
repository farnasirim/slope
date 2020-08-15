#ifndef SLOPE_LOGGING_H_
#define SLOPE_LOGGING_H_

#include <string>

namespace slope {
namespace logging {

void warn(const std::string& str);
void log(const std::string& msg);

}  // namespace logging
}  // namespace slope

#endif  // SLOPE_LOGGING_H_
