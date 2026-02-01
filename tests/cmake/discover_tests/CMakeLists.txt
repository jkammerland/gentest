cmake_minimum_required(VERSION 3.31)
project(gentest_discover_tests_fixture LANGUAGES CXX)

include(CTest)
enable_testing()

# Include the in-tree helper under test.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/GentestCodegen.cmake")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" [==[
#include <iostream>
#include <string_view>

static void list_tests() {
    std::cout << "demo/a\n";
    std::cout << "demo/b\n";
    std::cout << "demo/skip\n";
    std::cout << "demo/has [bracket]\n";
    std::cout << "demo/death\n";
}

static void list_meta() {
    std::cout << "demo/a [tags=fast] (main.cpp:1)\n";
    std::cout << "demo/b (main.cpp:2)\n";
    std::cout << "demo/skip [skip=not ready] (main.cpp:3)\n";
    std::cout << "demo/has [bracket] [tags=fast] (main.cpp:4)\n";
    std::cout << "demo/death [tags=death;owner=ci] (main.cpp:5)\n";
}

static int run_one(std::string_view name) {
    if (name == "demo/a") { std::cout << "[ PASS ] demo/a\n"; return 0; }
    if (name == "demo/b") { std::cout << "[ PASS ] demo/b\n"; return 0; }
    if (name == "demo/skip") { std::cout << "[ SKIP ] demo/skip :: not ready\n"; return 0; }
    if (name == "demo/has [bracket]") { std::cout << "[ PASS ] demo/has [bracket]\n"; return 0; }
    if (name == "demo/death") { std::cout << "fatal path\n"; return 1; }
    std::cerr << "Test not found: " << name << "\n";
    return 1;
}

int main(int argc, char** argv) {
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
        constexpr std::string_view kRun = "--run-test=";
        if (arg.rfind(kRun, 0) == 0) {
            return run_one(arg.substr(kRun.size()));
        }
    }
    // Default: run all
    (void)run_one("demo/a");
    (void)run_one("demo/b");
    (void)run_one("demo/skip");
    (void)run_one("demo/has [bracket]");
    return 0;
}
]==])

add_executable(demo_tests "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
target_compile_features(demo_tests PRIVATE cxx_std_20)

gentest_discover_tests(demo_tests
  DISCOVERY_MODE PRE_TEST
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  EXPECT_SUBSTRING "fatal path")
