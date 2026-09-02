#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace waos::observable {

  // Bounded FIFO. push returns false when full (no overwrite, no drop).
  template <typename T>
  class ring_buffer {
    std::vector<T> storage_;
    std::size_t cap_ = 0;
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;

  public:
    explicit ring_buffer(std::size_t capacity)
      : storage_(capacity), cap_(capacity) {}

    bool push(T v) {
      if(cap_ == 0 || size_ == cap_) return false;
      storage_[tail_] = std::move(v);
      tail_ = (tail_ + 1) % cap_;
      ++size_;
      return true;
    }

    std::optional<T> pop() {
      if(size_ == 0) return std::nullopt;
      T v = std::move(storage_[head_]);
      storage_[head_] = T{};
      head_ = (head_ + 1) % cap_;
      --size_;
      return v;
    }

    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return cap_; }
    bool full() const noexcept { return cap_ != 0 && size_ == cap_; }
    bool empty() const noexcept { return size_ == 0; }

    void clear() noexcept {
      while(size_ > 0) {
        pop();
      }
      head_ = 0;
      tail_ = 0;
    }
  };

}
