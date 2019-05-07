#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <pqrs/process.hpp>

TEST_CASE("process") {
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

    REQUIRE(stdout == "hello\n");
    REQUIRE(stderr == "");
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

    REQUIRE(stdout == "hello\n");
    REQUIRE(stderr == "");
  }

  dispatcher->terminate();
  dispatcher = nullptr;
}
