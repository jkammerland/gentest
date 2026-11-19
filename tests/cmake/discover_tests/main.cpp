#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

static void list_json() {
    std::cout << R"json([
        {"name":"demo/a","tags":["fast","owner=team-runtime","feature;parser"],"skipped":false,"is_death":false},
        {"name":"demo/b","tags":["death"],"skipped":false,"is_death":false},
        {"name":"demo/skip","tags":[],"skipped":true,"is_death":false},
        {"name":"demo/has [bracket]","tags":["fast"],"skipped":false,"is_death":false},
        {"name":"demo/a;b","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/lone[","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/lone]","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/quote]==]","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/suffix]=","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/nested]=]tail]==","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/back\\slash$\"","tags":[],"skipped":false,"is_death":false},
        {"name":"demo/death","tags":[],"skipped":false,"is_death":true},
        {"name":"demo/death;[","tags":["death"],"skipped":false,"is_death":true},
        {"name":"demo/death_runtime_skip","tags":[],"skipped":false,"is_death":true},
        {"name":"demo/death_missing","tags":[],"skipped":false,"is_death":true},
        {"name":"demo/death_skip","tags":["death"],"skipped":true,"is_death":true}
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
        return 77;
    }
    if (name == "demo/has [bracket]" || name == "demo/a;b" || name == "demo/lone[" || name == "demo/lone]" || name == "demo/quote]==]" ||
        name == "demo/back\\slash$\"" || name == "demo/suffix]=" || name == "demo/nested]=]tail]==") {
        std::cout << "[ PASS ] demo/has [bracket]\n";
        return 0;
    }
    if (name == "demo/death" || name == "demo/death;[") {
        // The punctuation-bearing case verifies that the host wrapper preserves the CTest environment and target launcher.
        if (name == "demo/death;[" && (std::getenv("GENTEST_FIXTURE_LAUNCHED") == nullptr || std::getenv("GENTEST_FIXTURE_ENV") == nullptr))
            return 0;
        std::cout << "Case not found: unrelated-diagnostic\n";
        std::cout << "fatal path: this is a death test\n";
        return 3;
    }
    if (name == "demo/death_skip" || name == "demo/death_runtime_skip") {
        std::cout << "[ SKIP ] " << name << " :: disabled [debug]\n";
        return 77;
    }
    std::cerr << "Test not found: " << name << "\n";
    return 1;
}

int main(int argc, char **argv) {
    std::string_view marker;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.starts_with("--gentest-death-marker="))
            marker = arg.substr(std::string_view("--gentest-death-marker=").size());
    }

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
            if (arg.substr(kRun.size()) == "demo/death_missing")
                return run_one("demo/death_missing");
            if (!marker.empty()) {
                std::ofstream file(std::string(marker), std::ios::binary);
                file << arg.substr(kRun.size());
            }
            const int result = run_one(arg.substr(kRun.size()));
            if (result == 77 && !marker.empty()) {
                std::ofstream file(std::string(marker), std::ios::binary | std::ios::trunc);
                file << arg.substr(kRun.size()) << "\nskipped";
            }
            return result;
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
