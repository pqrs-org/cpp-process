#include <boost/ut.hpp>
#include <pqrs/process.hpp>
#include <pqrs/string.hpp>

int main() {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "process"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    {
      std::string stdout;
      std::string stderr;
      pqrs::process::process p(dispatcher,
                               std::vector<std::string>{
                                   "/bin/echo",
                                   "hello",
                               });
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
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();

      expect(stdout == "hello\n");
      expect(stderr == "");
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
      std::string stdout;
      std::string stderr;
      auto p = std::make_unique<pqrs::process::process>(dispatcher,
                                                        std::vector<std::string>{
                                                            "/bin/sh",
                                                            "-c",
                                                            "echo hello; sleep 30; echo world",
                                                        });
      p->stdout_received.connect([&stdout](auto&& buffer) {
        for (const auto& c : *buffer) {
          stdout += c;
        }
      });
      p->stderr_received.connect([&stderr](auto&& buffer) {
        for (const auto& c : *buffer) {
          stderr += c;
        }
      });
      p->run();
      std::cout << "pid: " << *(p->get_pid()) << std::endl;

      // Wait until sleep is executed.
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));

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

    // SIGHUP will be ignored by program.

    {
      std::string stdout;
      std::string stderr;
      {
        pqrs::process::process p(dispatcher,
                                 std::vector<std::string>{
                                     "./build/hello",
                                 });
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
        p.run_failed.connect([] {
          std::cout << "run_failed" << std::endl;
        });
        p.exited.connect([](auto&& status) {
          std::cout << "exited: " << status << std::endl;
        });
        p.run();

        // Wait until `signal` in hello.cpp is called.
        for (int i = 0; i < 10; ++i) {
          std::cout << "." << std::flush;
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << std::endl;

        // SIGHUP is ignored.
        p.kill(SIGHUP);
      }

      expect(stdout == "hello\n");
      expect(stderr == "");
    }

    // stderr

    {
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
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();

      // Ensure stdout_received and stderr_received are called.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(stdout == "");
      expect(stderr == "/bin/sh: /not_found.sh: No such file or directory\n");
    }

    // stderr after stdout is closed

    {
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
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();

      // Ensure stdout_received and stderr_received are called.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      expect(stdout == "hello\n");
      expect(stderr == "world\n");
    }

    // Empty argv

    {
      auto wait = pqrs::make_thread_wait();
      bool run_failed = false;
      bool exited = false;

      pqrs::process::process p(dispatcher, std::vector<std::string>{});
      p.run_failed.connect([&run_failed, wait] {
        run_failed = true;
        wait->notify();
      });
      p.exited.connect([&exited](auto&&) {
        exited = true;
      });
      p.run();

      wait->wait_notice();

      expect(run_failed);
      expect(!exited);
      expect(!p.get_pid());
    }

    // Environment variable

    {
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
      p.run();
      std::cout << "pid: " << *(p.get_pid()) << std::endl;

      p.wait();

      // Ensure stdout_received and stderr_received are called.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

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
          "exit 3",
      });
      expect(3 == e.get_exit_code());
    }

    {
      pqrs::process::execute e(std::vector<std::string>{
          "/bin/sh",
          "-c",
          "kill -HUP $$",
      });
      expect(std::nullopt == e.get_exit_code());
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
