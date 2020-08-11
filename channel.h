#ifndef SLOPE_CHANNEL_H_
#define SLOPE_CHANNEL_H_

#include <condition_variable>
#include <queue>

namespace slope {
namespace ds {

template <typename T>
class Channel {
  std::condition_variable cv_;
  std::queue<T> q_;
  std::mutex m_;
  bool is_closed_;

 public:
  Channel() : is_closed_(false) {}

  T block_to_pop() {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [&] {
      std::cout << q_.empty() << is_closed_ << std::endl;
      return !q_.empty() || is_closed_;
    });
    std::cout << "here" << std::endl;
    if (q_.empty()) {
      throw std::exception();
    }
    auto here = std::move(q_.front());
    q_.pop();
    lk.unlock();
    cv_.notify_one();
    return here;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lk(m_);
      is_closed_ = 1;
    }
    cv_.notify_one();
  }

  void push(T&& val) {
    {
      std::lock_guard<std::mutex> lk(m_);
      q_.push(std::forward<T>(val));
    }
    cv_.notify_one();
  }
};

}  // namespace ds
}  // namespace slope

#endif  // SLOPE_CHANNEL_H_

