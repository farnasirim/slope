#include "cluster_time.h"

namespace slope {
namespace time {

decltype(std::chrono::high_resolution_clock::now()) calibrated_local_start_time;

}  // namespace time
}  // namespace slope
