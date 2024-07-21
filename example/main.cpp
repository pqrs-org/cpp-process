#include <iostream>
#include <pqrs/process.hpp>

int main(void) {
  //
  // /bin/ls
  //

  {
    pqrs::process::execute e(std::vector<std::string>{
        "/bin/ls",
        "/",
        "/not_found",
    });
    std::cout << "stdout: " << e.get_stdout() << std::endl;
    std::cout << "stderr: " << e.get_stderr() << std::endl;
    if (auto exit_code = e.get_exit_code()) {
      std::cout << "exit_code: " << *exit_code << std::endl;
    }
  }

  //
  // /usr/bin/top
  //

  {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    pqrs::process::process p(dispatcher,
                             std::vector<std::string>{
                                 "/usr/bin/top",
                                 "-l",
                                 "0",
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
    });
    p.exited.connect([](auto&& status) {
      std::cout << "exited: " << status << std::endl;
    });
    p.run();
    p.wait();

    dispatcher->terminate();
    dispatcher = nullptr;
  }

  return 0;
}
