#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--version") {
            std::cout << "fake clang-scan-deps version 1\n";
            return 0;
        }
    }

    const char *fixture = std::getenv("GENTEST_FAKE_SCAN_DEPS_JSON");
    if (fixture == nullptr || *fixture == '\0') {
        std::cerr << "GENTEST_FAKE_SCAN_DEPS_JSON is not set\n";
        return 2;
    }
    std::ifstream input(fixture, std::ios::binary);
    if (!input) {
        std::cerr << "failed to open fake scan-deps fixture\n";
        return 3;
    }
    std::cout << input.rdbuf();
    return input.bad() || std::cout.bad() ? 4 : 0;
}
