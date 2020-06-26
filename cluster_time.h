#ifndef SLOPE_CLUSTER_TIME_H_
#define SLOPE_CLUSTER_TIME_H_

#include <atomic>
#include <chrono>

namespace slope {
namespace time {

decltype(std::chrono::high_resolution_clock::now()) calibrated_local_start_time;

}  // namespace time
}  // namespace slope

#endif  // SLOPE_CLUSTER_TIME_H_



