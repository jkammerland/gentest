#include <iostream>
#include <string_view>

static void list_tests() {
  std::cout << "demo/a\n";
  std::cout << "demo/b\n";
  std::cout << "demo/skip\n";
  std::cout << "demo/has [bracket]\n";
  std::cout << "demo/death\n";
  std::cout << "demo/death_skip\n";
}

static void list_meta() {
  std::cout << "demo/a [gentest:tags=fast] (main.cpp:1)\n";
  std::cout << "demo/b (main.cpp:2)\n";
  std::cout << "demo/skip [gentest:skip=needs [linux]] (main.cpp:3)\n";
  std::cout << "demo/has [bracket] [gentest:tags=fast] (main.cpp:4)\n";
  std::cout << "demo/death [gentest:tags=death;owner=ci] (main.cpp:5)\n";
  std::cout << "demo/death_skip [gentest:tags=death;skip=disabled [debug]] (main.cpp:6)\n";
}

static int run_one(std::string_view name) {
  if (name == "demo/a") {
    std::cout << "[ PASS ] demo/a\n";
    return 0;
  }
  if (name == "demo/b") {
    std::cout << "[ PASS ] demo/b\n";
    return 0;
  }
  if (name == "demo/skip") {
    std::cout << "[ SKIP ] demo/skip :: needs [linux]\n";
    return 0;
  }
  if (name == "demo/has [bracket]") {
    std::cout << "[ PASS ] demo/has [bracket]\n";
    return 0;
  }
  if (name == "demo/death") {
    std::cout << "Case not found: unrelated-diagnostic\n";
    std::cout << "fatal path\n";
    return 3;
  }
  if (name == "demo/death_skip") {
    std::cout << "[ SKIP ] demo/death_skip :: disabled [debug]\n";
    return 0;
  }
  std::cerr << "Test not found: " << name << "\n";
  return 1;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i] ? argv[i] : "";
    if (arg == "--list-tests") {
      list_tests();
      return 0;
    }
    if (arg == "--list") {
      list_meta();
      return 0;
    }
    if (arg == "--list-death") {
      std::cout << "demo/death\n";
      return 0;
    }
    constexpr std::string_view kRun = "--run=";
    if (arg.rfind(kRun, 0) == 0) {
      return run_one(arg.substr(kRun.size()));
    }
  }
  // Default: run all
  (void)run_one("demo/a");
  (void)run_one("demo/b");
  (void)run_one("demo/skip");
  (void)run_one("demo/has [bracket]");
  (void)run_one("demo/death_skip");
  return 0;
}

