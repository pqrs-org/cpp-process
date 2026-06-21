#include <atomic>
#include <boost/ut.hpp>
#include <chrono>
#include <csignal>
#include <pqrs/process.hpp>
#include <pqrs/string.hpp>
#include <pthread.h>
#include <thread>

namespace {

void handle_sigusr1(int) {
}

void set_sigusr1_blocked(bool blocked) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(blocked ? SIG_BLOCK : SIG_UNBLOCK, &set, nullptr);
}

class scoped_sigusr1_handler final {
public:
  scoped_sigusr1_handler() {
    sigset_t empty_set;
    sigemptyset(&empty_set);
    pthread_sigmask(SIG_BLOCK, &empty_set, &old_mask_);

    struct sigaction action{};
    action.sa_handler = handle_sigusr1;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, &old_action_);
  }

  ~scoped_sigusr1_handler() {
    sigaction(SIGUSR1, &old_action_, nullptr);
    pthread_sigmask(SIG_SETMASK, &old_mask_, nullptr);
  }

private:
  struct sigaction old_action_{};
  sigset_t old_mask_{};
};

template <typename F>
bool wait_until(F&& predicate,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return predicate();
}

} // namespace

int main() {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "process"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      const auto wait = pqrs::make_thread_wait();
      std::string stdout;
      std::string stderr;
      bool stdout_called_on_dispatcher_thread = false;
      bool stderr_called_on_dispatcher_thread = false;
      bool exited_called_on_dispatcher_thread = false;
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "echo hello; echo error >&2",
                               });
      p.stdout_received.connect([&p, &stdout, &stdout_called_on_dispatcher_thread](auto&& buffer) {
        stdout_called_on_dispatcher_thread = p.dispatcher_thread();
        for (const auto& c : *buffer) {
          stdout += c;
        }
      });
      p.stderr_received.connect([&p, &stderr, &stderr_called_on_dispatcher_thread](auto&& buffer) {
        stderr_called_on_dispatcher_thread = p.dispatcher_thread();
        for (const auto& c : *buffer) {
          stderr += c;
        }
      });
      p.exited.connect([&p, &exited_called_on_dispatcher_thread, wait](auto&&) {
        exited_called_on_dispatcher_thread = p.dispatcher_thread();
        wait->notify();
      });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();
      wait->wait_notice();

      expect(stdout == "hello\n");
      expect(stderr == "error\n");
      expect(stdout_called_on_dispatcher_thread);
      expect(stderr_called_on_dispatcher_thread);
      expect(exited_called_on_dispatcher_thread);
    }

    {
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/usr/bin/yes",
                               });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.kill(SIGHUP);
      p.wait();
    }

    // Sleep and kill

    {
      const auto hello_wait = pqrs::make_thread_wait();
      std::string stdout;
      std::string stderr;
      auto p = std::make_unique<pqrs::process::process>(dispatcher,
                                                        std::vector<std::string>{
                                                            "/bin/sh",
                                                            "-c",
                                                            "echo hello; sleep 30; echo world",
                                                        });
      p->stdout_received.connect([&stdout, hello_wait](auto&& buffer) {
        for (const auto& c : *buffer) {
          stdout += c;
        }
        if (stdout == "hello\n") {
          hello_wait->notify();
        }
      });
      p->stderr_received.connect([&stderr](auto&& buffer) {
        for (const auto& c : *buffer) {
          stderr += c;
        }
      });
      p->run();
      std::cout << "pid: " << *(p->get_pid()) << std::endl;

      hello_wait->wait_notice();

      auto start = std::chrono::system_clock::now();

      p->kill(SIGKILL);
      p = nullptr;

      auto end = std::chrono::system_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

      expect(stdout == "hello\n");
      expect(stderr == "");
      expect(elapsed < 3000);
    }

    // Kill and wait automatically at destructor.

    {
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/usr/bin/yes",
                               });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;
    }

    // Kill without wait (wait automatically at destructor)

    {
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/usr/bin/yes",
                               });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.kill(SIGHUP);
    }

    // Multiple run is not allowed.

    {
      auto wait = pqrs::make_thread_wait();
      int run_failed_count = 0;

      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/echo",
                                   "hello",
                               });
      p.run_failed.connect([&run_failed_count, wait] {
        ++run_failed_count;
        wait->notify();
      });
      p.run();
      p.wait();

      p.run();
      wait->wait_notice();

      expect(run_failed_count == 1_i);
    }

    // Multiple wait calls are allowed.

    {
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "sleep 1",
                               });
      p.run();

      std::thread t1([&p] {
        p.wait();
      });
      std::thread t2([&p] {
        p.wait();
      });

      t1.join();
      t2.join();
    }

    // Destructor fallback after dispatcher is already terminated.

    {
      auto local_time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
      auto local_dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(local_time_source);
      auto p = std::make_unique<pqrs::process::process>(local_dispatcher,
                                                        std::vector<std::string>{
                                                            "/bin/sh",
                                                            "-c",
                                                            "sleep 30",
                                                        });
      p->run();
      expect(static_cast<bool>(p->get_pid()));

      local_dispatcher->terminate();

      auto start = std::chrono::system_clock::now();
      p = nullptr;
      auto end = std::chrono::system_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

      expect(elapsed < 3000);
    }

    // EINTR while poll is waiting should not stop reading stdout.

    {
      scoped_sigusr1_handler sigusr1_handler;

      set_sigusr1_blocked(true);
      auto local_time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
      auto local_dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(local_time_source);

      std::string stdout;
      std::atomic<bool> exited = false;

      set_sigusr1_blocked(false);
      pqrs::process::process p(local_dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "sleep 0.2; kill -USR1 $PPID; sleep 0.2; echo poll-eintr",
                               });
      p.stdout_received.connect([&stdout](auto&& buffer) {
        for (const auto& c : *buffer) {
          stdout += c;
        }
      });
      p.exited.connect([&exited](auto&&) {
        exited = true;
      });
      p.run();
      set_sigusr1_blocked(true);

      p.wait();
      expect(wait_until([&exited] {
        return exited.load();
      }));
      expect(stdout == "poll-eintr\n");

      local_dispatcher->terminate();
    }

    // EINTR while waitpid is waiting should not drop the exited signal.

    {
      scoped_sigusr1_handler sigusr1_handler;

      set_sigusr1_blocked(true);
      auto local_time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
      auto local_dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(local_time_source);

      std::atomic<int> exit_code = -1;

      set_sigusr1_blocked(false);
      pqrs::process::process p(local_dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "exec 1>&-; exec 2>&-; sleep 0.2; kill -USR1 $PPID; sleep 0.2; exit 7",
                               });
      p.exited.connect([&exit_code](auto&& status) {
        exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -2;
      });
      p.run();
      set_sigusr1_blocked(true);

      p.wait();
      expect(wait_until([&exit_code] {
        return exit_code.load() != -1;
      }));
      expect(exit_code.load() == 7_i);

      local_dispatcher->terminate();
    }

    // SIGHUP will be ignored by program.

    {
      const auto hello_wait = pqrs::make_thread_wait();
      std::string stdout;
      std::string stderr;
      {
        pqrs::process::process p(dispatcher,
                                 std::vector<std::string>{
                                     "./build/hello",
                                 });
        p.stdout_received.connect([&stdout, hello_wait](auto&& buffer) {
          for (const auto& c : *buffer) {
            stdout += c;
          }
          if (stdout == "hello\n") {
            hello_wait->notify();
          }
        });
        p.stderr_received.connect([&stderr](auto&& buffer) {
          for (const auto& c : *buffer) {
            stderr += c;
          }
        });
        p.run_failed.connect([] {
          std::cout << "run_failed" << std::endl;
        });
        p.exited.connect([](auto&& status) {
          std::cout << "exited: " << status << std::endl;
        });
        p.run();

        hello_wait->wait_notice();

        // SIGHUP is ignored.
        p.kill(SIGHUP);
      }

      expect(stdout == "hello\n");
      expect(stderr == "");
    }

    // stderr

    {
      const auto wait = pqrs::make_thread_wait();
      std::string stdout;
      std::string stderr;
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "/not_found.sh"});
      p.stdout_received.connect([&stdout](auto&& buffer) {
        for (const auto& c : *buffer) {
          stdout += c;
        }
      });
      p.stderr_received.connect([&stderr](auto&& buffer) {
        for (const auto& c : *buffer) {
          stderr += c;
        }
      });
      p.exited.connect([wait](auto&&) {
        wait->notify();
      });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();
      wait->wait_notice();

      expect(stdout == "");
      expect(stderr == "/bin/sh: /not_found.sh: No such file or directory\n");
    }

    // stderr after stdout is closed

    {
      const auto wait = pqrs::make_thread_wait();
      std::string stdout;
      std::string stderr;
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "echo hello; exec 1>&-; sleep 1; echo world >&2"});
      p.stdout_received.connect([&stdout](auto&& buffer) {
        for (const auto& c : *buffer) {
          stdout += c;
        }
      });
      p.stderr_received.connect([&stderr](auto&& buffer) {
        for (const auto& c : *buffer) {
          stderr += c;
        }
      });
      p.exited.connect([wait](auto&&) {
        wait->notify();
      });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();
      wait->wait_notice();

      expect(stdout == "hello\n");
      expect(stderr == "world\n");
    }

    // Empty argv

    {
      auto wait = pqrs::make_thread_wait();
      bool run_failed = false;
      bool exited = false;
      bool run_failed_called_on_dispatcher_thread = false;

      pqrs::process::process p(dispatcher, std::vector<std::string>{});
      p.run_failed.connect([&p, &run_failed, &run_failed_called_on_dispatcher_thread, wait] {
        run_failed = true;
        run_failed_called_on_dispatcher_thread = p.dispatcher_thread();
        wait->notify();
      });
      p.exited.connect([&exited](auto&&) {
        exited = true;
      });
      p.run();

      wait->wait_notice();

      expect(run_failed);
      expect(run_failed_called_on_dispatcher_thread);
      expect(!exited);
      expect(!p.get_pid());
    }

    // Environment variable

    {
      const auto wait = pqrs::make_thread_wait();
      std::string stdout;
      std::string stderr;
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/sh",
                                   "-c",
                                   "echo $HOME"});
      p.stdout_received.connect([&stdout](auto&& buffer) {
        for (const auto& c : *buffer) {
          stdout += c;
        }
      });
      p.stderr_received.connect([&stderr](auto&& buffer) {
        for (const auto& c : *buffer) {
          stderr += c;
        }
      });
      p.exited.connect([wait](auto&&) {
        wait->notify();
      });
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();
      wait->wait_notice();

      pqrs::string::trim(stdout);
      std::cout << "stdout: `" << stdout << "`" << std::endl;

      expect(stdout != "");
      expect(stderr == "");
    }

    dispatcher->terminate();
    dispatcher = nullptr;
  };

  "execute"_test = [] {
    {
      pqrs::process::execute e(std::vector<std::string>{
          "/bin/sh",
          "-c",
          "echo hello; echo error >&2; exit 3",
      });
      expect(3 == e.get_exit_code());
      expect("hello\n" == e.get_stdout());
      expect("error\n" == e.get_stderr());
    }

    {
      pqrs::process::execute e(std::vector<std::string>{
          "/bin/sh",
          "-c",
          "kill -HUP $$",
      });
      expect(std::nullopt == e.get_exit_code());
    }

    {
      pqrs::process::execute e(std::vector<std::string>{});
      expect(std::nullopt == e.get_exit_code());
      expect("" == e.get_stdout());
      expect("" == e.get_stderr());
    }
  };

  "system"_test = [] {
    // exit(0)
    {
      auto exit_code = pqrs::process::system("/bin/ls / > /dev/null");
      expect(0 == exit_code);
    }

    // exit(1)
    {
      auto exit_code = pqrs::process::system("/bin/ls /not_found >& /dev/null");
      expect(1 == exit_code);
    }

    // exit(127)
    {
      auto exit_code = pqrs::process::system("/not_found >& /dev/null");
      expect(127 == exit_code);
    }
  };

  return 0;
}
