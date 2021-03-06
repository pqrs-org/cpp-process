#pragma once

// (C) Copyright Takayama Fumihiko 2019.
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)

#include "file_actions.hpp"
#include "pipe.hpp"
#include <csignal>
#include <nod/nod.hpp>
#include <optional>
#include <poll.h>
#include <pqrs/dispatcher.hpp>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>

namespace pqrs {
namespace process {
class process final : public dispatcher::extra::dispatcher_client {
public:
  // Signals (invoked from the dispatcher thread)

  nod::signal<void(std::shared_ptr<std::vector<uint8_t>>)> stdout_received;
  nod::signal<void(std::shared_ptr<std::vector<uint8_t>>)> stderr_received;
  nod::signal<void(void)> run_failed;
  nod::signal<void(int)> exited;

  // Methods

  process(std::weak_ptr<dispatcher::dispatcher> weak_dispatcher,
          const std::vector<std::string>& argv) : dispatcher_client(weak_dispatcher) {
    argv_buffer_ = make_argv_buffer(argv);
    argv_ = make_argv(argv_buffer_);

    stdout_pipe_ = std::make_unique<pipe>();
    stderr_pipe_ = std::make_unique<pipe>();
    file_actions_ = make_file_actions(*stdout_pipe_, *stderr_pipe_);
  }

  ~process(void) {
    detach_from_dispatcher([this] {
      kill(SIGKILL);
      wait();

      file_actions_ = nullptr;
      stderr_pipe_ = nullptr;
      stdout_pipe_ = nullptr;
    });
  }

  std::optional<pid_t> get_pid(void) const {
    std::lock_guard<std::mutex> lock(mutex_);

    return pid_;
  }

  void run(void) {
    std::lock_guard<std::mutex> lock(mutex_);

    pid_t pid;
    if (posix_spawn(&pid,
                    argv_[0],
                    file_actions_->get_actions(),
                    nullptr,
                    &(argv_[0]),
                    nullptr) != 0) {
      enqueue_to_dispatcher([this] {
        run_failed();
      });
      return;
    }

    pid_ = pid;

    stdout_pipe_->close_write_end();
    stderr_pipe_->close_write_end();

    // Start polling thread

    thread_ = std::make_unique<std::thread>([this] {
      std::vector<pollfd> poll_file_descriptors;
      {
        std::lock_guard<std::mutex> lock(mutex_);

        if (auto fd = stdout_pipe_->get_read_end()) {
          poll_file_descriptors.push_back({*fd, POLLIN, 0});
        }
        if (auto fd = stderr_pipe_->get_read_end()) {
          poll_file_descriptors.push_back({*fd, POLLIN, 0});
        }
      }

      if (!poll_file_descriptors.empty()) {
        std::vector<uint8_t> buffer(32 * 1024);
        int timeout = -1;
        while (poll(&(poll_file_descriptors[0]), poll_file_descriptors.size(), timeout) > 0) {
          int fd = 0;

          if (poll_file_descriptors[0].revents & POLLIN) {
            fd = poll_file_descriptors[0].fd;

          } else if (poll_file_descriptors[1].revents & POLLIN) {
            fd = poll_file_descriptors[1].fd;

          } else {
            break;
          }

          auto n = read(fd, &(buffer[0]), buffer.size());
          if (n > 0) {
            auto b = std::make_shared<std::vector<uint8_t>>(std::begin(buffer), std::begin(buffer) + n);

            if (fd == poll_file_descriptors[0].fd) {
              enqueue_to_dispatcher([this, b] {
                stdout_received(b);
              });
            } else if (fd == poll_file_descriptors[1].fd) {
              enqueue_to_dispatcher([this, b] {
                stderr_received(b);
              });
            }
          } else {
            break;
          }
        }
      }

      // Wait process

      {
        std::lock_guard<std::mutex> lock(mutex_);

        if (pid_) {
          int stat;
          if (waitpid(*pid_, &stat, 0) == *pid_) {
            pid_ = std::nullopt;

            enqueue_to_dispatcher([this, stat] {
              exited(stat);
            });
          }
        }
      }
    });
  }

  void kill(int signal) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (pid_) {
      ::kill(*pid_, signal);
    }
  }

  void wait(void) {
    std::shared_ptr<std::thread> t;

    {
      std::lock_guard<std::mutex> lock(mutex_);

      t = thread_;
    }

    if (t && t->joinable()) {
      t->join();
    }
  }

private:
  static std::vector<std::vector<char>> make_argv_buffer(const std::vector<std::string>& argv) {
    std::vector<std::vector<char>> buffer;

    for (const auto& a : argv) {
      std::vector<char> b(std::begin(a), std::end(a));
      b.push_back('\0');
      buffer.push_back(b);
    }

    return buffer;
  }

  static std::vector<char*> make_argv(std::vector<std::vector<char>>& buffer) {
    std::vector<char*> argv;

    for (auto&& b : buffer) {
      argv.push_back(&(b[0]));
    }

    argv.push_back(nullptr);

    return argv;
  }

  static std::unique_ptr<file_actions> make_file_actions(const pipe& stdout_pipe,
                                                         const pipe& stderr_pipe) {
    auto actions = std::make_unique<file_actions>();

    if (auto fd = stdout_pipe.get_read_end()) {
      actions->addclose(*fd);
    }

    if (auto fd = stdout_pipe.get_write_end()) {
      actions->adddup2(*fd, 1);
      actions->addclose(*fd);
    }

    if (auto fd = stderr_pipe.get_read_end()) {
      actions->addclose(*fd);
    }

    if (auto fd = stderr_pipe.get_write_end()) {
      actions->adddup2(*fd, 2);
      actions->addclose(*fd);
    }

    return actions;
  }

  std::vector<std::vector<char>> argv_buffer_;
  std::vector<char*> argv_;
  std::unique_ptr<pipe> stdout_pipe_;
  std::unique_ptr<pipe> stderr_pipe_;
  std::unique_ptr<file_actions> file_actions_;
  std::optional<pid_t> pid_;
  std::shared_ptr<std::thread> thread_;
  mutable std::mutex mutex_;
};
} // namespace process
} // namespace pqrs
