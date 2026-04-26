#pragma once

// (C) Copyright Takayama Fumihiko 2019.
// Distributed under the Boost Software License, Version 1.0.
// (See https://www.boost.org/LICENSE_1_0.txt)

#include <mutex>
#include <optional>
#include <unistd.h>

namespace pqrs {
namespace process {
class pipe final {
public:
  pipe(void) {
    file_descriptors_[0] = -1;
    file_descriptors_[1] = -1;

    if (::pipe(file_descriptors_) != 0) {
      file_descriptors_[0] = -1;
      file_descriptors_[1] = -1;
    }
  }

  ~pipe(void) {
    close_read_end();
    close_write_end();
  }

  std::optional<int> get_read_end(void) const {
    std::lock_guard<std::mutex> lock(mutex_);

    int fd = file_descriptors_[0];
    if (fd != -1) {
      return fd;
    }
    return std::nullopt;
  }

  std::optional<int> get_write_end(void) const {
    std::lock_guard<std::mutex> lock(mutex_);

    int fd = file_descriptors_[1];
    if (fd != -1) {
      return fd;
    }
    return std::nullopt;
  }

  void close_read_end(void) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (file_descriptors_[0] != -1) {
      close(file_descriptors_[0]);
      file_descriptors_[0] = -1;
    }
  }

  void close_write_end(void) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (file_descriptors_[1] != -1) {
      close(file_descriptors_[1]);
      file_descriptors_[1] = -1;
    }
  }

private:
  int file_descriptors_[2];
  mutable std::mutex mutex_;
};
} // namespace process
} // namespace pqrs
