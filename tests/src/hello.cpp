#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

int main(void) {
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);

  std::cout << "hello" << std::endl;

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }

  return 0;
}
