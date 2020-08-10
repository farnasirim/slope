#ifndef SLOPE_THREAD_JOINER_CHANNEL_H_
#define SLOPE_THREAD_JOINER_CHANNEL_H_

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <queue>
#include <thread>

#include "channel.h"

namespace slope {
namespace ds {

class ThreadJoiner {
  Channel<std::thread> threads;
  std::thread joiner;

  void join_all();

 public:
  ThreadJoiner();

  void add(std::thread t);

  ~ThreadJoiner();
};

}  // namespace ds
}  // namespace slope

#endif  // SLOPE_THREAD_JOINER_H_

