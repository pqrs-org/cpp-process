#include <iostream>
#include <pqrs/process.hpp>

int main(void) {
  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

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
  p.run();

  dispatcher->terminate();
  dispatcher = nullptr;

  return 0;
}
