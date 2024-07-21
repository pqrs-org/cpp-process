#include <iostream>
#include <pqrs/process.hpp>

namespace {
auto global_wait = pqrs::make_thread_wait();
}

int main(void) {
  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  //
  // /bin/ls
  //

  {
    pqrs::process::process p(dispatcher,
                             std::vector<std::string>{
                                 "/bin/ls",
                                 "/",
                                 "/not_found",
                             });
    p.stdout_received.connect([](auto&& buffer) {
      std::cout << "stdout: ";
      for (const auto& c : *buffer) {
        std::cout << c;
      }
      std::cout << std::endl;
    });
    p.stderr_received.connect([](auto&& buffer) {
      std::cout << "stderr: ";
      for (const auto& c : *buffer) {
        std::cout << c;
      }
      std::cout << std::endl;
    });
    p.run_failed.connect([] {
      std::cout << "run_failed" << std::endl;
      global_wait->notify();
    });
    p.exited.connect([](auto&& status) {
      std::cout << "exited: " << status << std::endl;
      global_wait->notify();
    });
    p.run();
    p.wait();
  }

  // /usr/bin/top

  {
    pqrs::process::process p(dispatcher,
                             std::vector<std::string>{
                                 "/usr/bin/top",
                                 "-l",
                                 "3",
                                 "-n",
                                 "3",
                             });
    p.stdout_received.connect([](auto&& buffer) {
      std::cout << "stdout: ";
      for (const auto& c : *buffer) {
        std::cout << c;
      }
      std::cout << std::endl;
    });
    p.stderr_received.connect([](auto&& buffer) {
      std::cout << "stderr: ";
      for (const auto& c : *buffer) {
        std::cout << c;
      }
      std::cout << std::endl;
    });
    p.run_failed.connect([] {
      std::cout << "run_failed" << std::endl;
      global_wait->notify();
    });
    p.exited.connect([](auto&& status) {
      std::cout << "exited: " << status << std::endl;
      global_wait->notify();
    });
    p.run();
    p.wait();
  }

  // ============================================================

  global_wait->wait_notice();

  // ============================================================

  dispatcher->terminate();
  dispatcher = nullptr;

  return 0;
}
