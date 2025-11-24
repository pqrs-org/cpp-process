#include <boost/ut.hpp>
#include <pqrs/process.hpp>
#include <pqrs/string.hpp>

int main(void) {
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

    // The execution of the shell failed
    {
      auto exit_code = pqrs::process::system("/not_found >& /dev/null");
      expect(std::nullopt == exit_code);
    }
  };

  return 0;
}
