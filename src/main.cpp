#include "chatty.hpp"
#include "chatty/version.hpp"
#include <print>
#include <string_view>

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string_view a{argv[i]};
    if (a == "--version") {
      std::println("chatty {}", CHATTY_VERSION_STRING);
      return 0;
    }
    std::println(stderr, "chatty: unknown argument '{}'", a);
    return 2;
  }
  try {
    Chatty().run();
    return 0;
  } catch (const std::exception &e) {
    std::println(stderr, "fatal: {}", e.what());
    return 1;
  }
}
