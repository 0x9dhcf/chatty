#include "chatty.hpp"
#include <print>

int main() {
  try {
    Chatty().run();
    return 0;
  } catch (const std::exception &e) {
    std::println(stderr, "fatal: {}", e.what());
    return 1;
  }
}
