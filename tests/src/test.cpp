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
    p.kill(SIGHUP);
    p.wait();
  }

  // Kill and wait automatically.

  {
    pqrs::process::process p(dispatcher,
                             std::vector<std::string>{
                                 "/usr/bin/yes",
                             });
    p.run();
    std::cout << "pid: " << *(p.get_pid()) << std::endl;
  }

  dispatcher->terminate();
  dispatcher = nullptr;
}
