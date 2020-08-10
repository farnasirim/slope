#include "thread_joiner.h"

namespace slope {
namespace ds {

void ThreadJoiner::join_all() {
  try {
    while (true) {
      auto thread = threads.block_to_pop();
      thread.join();
    }
  } catch (std::exception& ex) {
  }
}

ThreadJoiner::ThreadJoiner() : joiner([&] { this->join_all(); }) {}

ThreadJoiner::~ThreadJoiner() {
  threads.close();
  joiner.join();
}

void ThreadJoiner::add(std::thread t) { threads.push(std::move(t)); }

}  // namespace ds
}  // namespace slope

