#include <cstdlib>
#include <iostream>
#include <string_view>

static void list_json() {
    std::cout << R"json([
        {"name":"demo/a","tags":["fast"],"skipped":false},
        {"name":"demo/b","tags":[],"skipped":false},
        {"name":"demo/skip","tags":[],"skipped":true},
        {"name":"demo/has [bracket]","tags":["fast"],"skipped":false},
        {"name":"demo/a;b","tags":[],"skipped":false},
        {"name":"demo/lone[","tags":[],"skipped":false},
        {"name":"demo/lone]","tags":[],"skipped":false},
        {"name":"demo/quote]==]","tags":[],"skipped":false},
        {"name":"demo/suffix]=","tags":[],"skipped":false},
        {"name":"demo/nested]=]tail]==","tags":[],"skipped":false},
        {"name":"demo/back\\slash$\"","tags":[],"skipped":false},
        {"name":"demo/death","tags":["death"],"skipped":false},
        {"name":"demo/death;[","tags":["death"],"skipped":false},
        {"name":"demo/death_skip","tags":["death"],"skipped":true}
    ])json";
}

static int run_one(std::string_view name) {
    if (name == "demo/abort") {
        std::abort();
    }
    if (name == "demo/fail") {
        std::cout << "[ FAIL ] ordinary assertion failure\n";
        return 1;
    }
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
    if (name == "demo/has [bracket]" || name == "demo/a;b" || name == "demo/lone[" || name == "demo/lone]" || name == "demo/quote]==]" ||
        name == "demo/back\\slash$\"" || name == "demo/suffix]=" || name == "demo/nested]=]tail]==") {
        std::cout << "[ PASS ] demo/has [bracket]\n";
        return 0;
    }
    if (name == "demo/death" || name == "demo/death;[") {
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
        const std::string_view arg = argv[i];
        if ((arg == "--verify-quoting" && (i + 1 >= argc || std::string_view(argv[i + 1]) != "--value=]=")) ||
            (arg == "--verify-nested-quoting" && (i + 1 >= argc || std::string_view(argv[i + 1]) != "--nested=]=]tail]=="))) {
            std::cerr << "Quoted discovery arguments changed\n";
            return 1;
        }
    }
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i] ? argv[i] : "";
        if (arg == "--list-json") {
            if (argc > 2 && std::string_view(argv[2]) == "--empty-inventory") {
                std::cout << "[]";
            } else {
                list_json();
            }
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
