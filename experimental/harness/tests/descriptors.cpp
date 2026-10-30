#include "../src/common.h"
#include "gentest/harness.h"

#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace {
using namespace gentest::harness;
using namespace gentest::harness::detail;
using gentest::awaitable;

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Directory {
    char path[64] = "/tmp/gentest-descriptors-XXXXXX";
    Directory() { systemCheck(::mkdtemp(path) != nullptr, "create descriptor test directory"); }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    auto file(std::string_view name, std::string_view contents = {}) const -> Fd {
        Fd fd(::open((std::string(path) + "/" + std::string(name)).c_str(), O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600));
        systemCheck(fd.value >= 0, "create descriptor test file");
        systemCheck(::write(fd.value, contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()), "write fixture file");
        return fd;
    }
    auto read(std::string_view name) const -> std::string {
        std::ifstream input(std::string(path) + "/" + std::string(name));
        require(input.good(), "read output fixture");
        return {std::istreambuf_iterator<char>(input), {}};
    }
};

auto specification(std::size_t count = 1) -> SessionSpec {
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "host"});
    spec.programs.resize(count);
    for (auto &program : spec.programs) {
        program.node = "host";
        program.argv = {HARNESS_DESCRIPTOR_PEER, "descriptors"};
    }
    return spec;
}

auto reject(SessionSpec spec, std::string_view text) -> awaitable<> {
    bool rejected = false;
    try {
        (void)co_await Session::start(std::move(spec));
    } catch (const std::exception &error) { rejected = std::string_view(error.what()).find(text) != std::string_view::npos; }
    require(rejected, "startup did not reject invalid input: " + std::string(text));
}

auto preflight() -> awaitable<> {
    Directory directory;
    auto      file = directory.file("input", "input");
    for (int target : {-1, 0, 2, 3, 8, 256}) {
        auto spec                     = specification();
        spec.programs[0].inheritedFds = {{file.value, target}};
        co_await reject(std::move(spec), "target");
    }
    auto spec                     = specification();
    spec.programs[0].inheritedFds = {{file.value, 72}, {file.value, 72}};
    co_await reject(std::move(spec), "duplicate");
    spec = specification();
    spec.programs[0].inheritedFds.resize(9, {file.value, 72});
    co_await reject(std::move(spec), "count");

    // Closed descriptors must be checked before channel creation can reuse them.
    const int closed = ::dup(file.value);
    systemCheck(closed >= 0, "duplicate validation fixture");
    ::close(closed);
    spec                          = specification();
    spec.programs[0].inheritedFds = {{closed, 72}};
    co_await reject(std::move(spec), "borrowed descriptor");
    spec                              = specification();
    spec.programs[0].standardOutputFd = -1;
    co_await reject(std::move(spec), "borrowed descriptor");
    Fd readOnly(::open((std::string(directory.path) + "/input").c_str(), O_RDONLY | O_CLOEXEC));
    spec                             = specification();
    spec.programs[0].standardErrorFd = readOnly.value;
    co_await reject(std::move(spec), "writable regular file");
    int pipes[2];
    systemCheck(::pipe2(pipes, O_CLOEXEC) == 0, "create output validation pipe");
    Fd pipeRead(pipes[0]), pipeWrite(pipes[1]);
    spec                              = specification();
    spec.programs[0].standardOutputFd = pipeWrite.value;
    co_await reject(std::move(spec), "writable regular file");
    Fd anchor(::open(directory.path, O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    spec                              = specification();
    spec.programs[0].standardOutputFd = anchor.value;
    co_await reject(std::move(spec), "writable regular file");

    for (auto limits : {ChildLimits{.openFiles = 8}, ChildLimits{.cpuSeconds = 0}, ChildLimits{.coreBytes = UINT64_MAX}}) {
        spec                         = specification();
        spec.programs[0].childLimits = limits;
        co_await reject(std::move(spec), "resource limit");
    }
    spec                          = specification();
    spec.programs[0].inheritedFds = {{file.value, 91}};
    spec.programs[0].childLimits  = ChildLimits{.openFiles = 91};
    co_await reject(std::move(spec), "resource limit");
    spec = specification();
    spec.programs[0].argv.push_back(std::string(3000, 'a'));
    spec.programs[0].environment.emplace_back("PAYLOAD", std::string(1500, 'b'));
    co_await reject(std::move(spec), "payload");
    spec                                 = specification();
    spec.topology.nodes[0].bridgeAddress = std::string(4090, 'x');
    co_await reject(std::move(spec), "payload");
    spec = specification();
    spec.programs[0].environment.emplace_back("GENTEST_HARNESS_CHANNEL", "9,10,11");
    co_await reject(std::move(spec), "environment");
    spec = specification();
    spec.programs[0].argv.push_back(std::string("bad\0argument", 12));
    co_await reject(std::move(spec), "NUL");
}

auto ownership() -> awaitable<> {
    Directory directory;
    auto      alpha = directory.file("alpha", "alpha");
    auto      beta  = directory.file("beta", "beta");
    auto      spec  = specification(2);

    // Two programs put the retained table at 13. These mappings exchange its
    // first two entries, while 255 exercises the top supported child target.
    spec.programs[0].inheritedFds = {{alpha.value, 14}, {beta.value, 13}, {alpha.value, 255}};
    spec.programs[0].argv.insert(spec.programs[0].argv.end(), {"14", "alpha", "13", "beta", "255", "alpha"});
    spec.programs[1].inheritedFds = {{beta.value, 83}};
    spec.programs[1].argv.insert(spec.programs[1].argv.end(), {"83", "beta"});
    auto session = co_await Session::start(std::move(spec));
    alpha.reset();
    beta.reset();
    for (std::size_t i = 0; i != 2; ++i) {
        co_await session.launch(i);
        const auto result = co_await session.wait(i);
        require(result.succeeded(), "descriptor mapping/leak check failed: " + result.standardError);
    }
    co_await session.stop();

    // Maximum table size crosses the former fixed relocation floor of 128.
    auto input  = directory.file("many", "many");
    auto output = directory.file("log");
    spec        = specification(16);
    for (auto &program : spec.programs) {
        for (int target = 230; target != 238; ++target) {
            program.inheritedFds.push_back({input.value, target});
            program.argv.insert(program.argv.end(), {std::to_string(target), "many"});
        }
        program.standardOutputFd = output.value;
        program.standardErrorFd  = output.value;
    }
    session = co_await Session::start(std::move(spec));
    input.reset();
    output.reset();
    for (std::size_t i = 0; i != 16; ++i) {
        co_await session.launch(i);
        require((co_await session.wait(i)).succeeded(), "maximum descriptor table failed: " + directory.read("log"));
    }
    co_await session.stop();
}

auto anchorAndOutput() -> awaitable<> {
    Directory directory;
    std::filesystem::create_directory(std::string(directory.path) + "/anchor");
    { auto file = directory.file("anchor/token", "original"); }
    Fd   anchor(::open((std::string(directory.path) + "/anchor").c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    auto spec                     = specification(2);
    spec.programs[0].argv         = {HARNESS_DESCRIPTOR_PEER, "anchor", "103"};
    spec.programs[0].inheritedFds = {{anchor.value, 103}};
    auto output                   = directory.file("stdout", "prefix:");
    systemCheck(::fcntl(output.value, F_SETFL, O_APPEND) == 0, "set append output");
    systemCheck(::lseek(output.value, 0, SEEK_SET) == 0, "rewind append fixture");
    auto error                        = directory.file("stderr");
    spec.programs[1].argv             = {HARNESS_DESCRIPTOR_PEER, "output"};
    spec.programs[1].standardOutputFd = output.value;
    spec.programs[1].standardErrorFd  = error.value;
    auto session                      = co_await Session::start(std::move(spec));
    anchor.reset();
    output.reset();
    error.reset();
    std::filesystem::rename(std::string(directory.path) + "/anchor", std::string(directory.path) + "/old");
    std::filesystem::create_directory(std::string(directory.path) + "/anchor");
    { auto file = directory.file("anchor/token", "replacement"); }
    co_await session.launch(0);
    require((co_await session.wait(0)).succeeded(), "directory anchor did not survive replacement");
    co_await session.launch(1);
    const auto result = co_await session.wait(1);
    require(result.succeeded() && !result.outputTruncated && result.standardOutput.empty() && result.standardError.empty(),
            "redirected output reported a capture tail");
    require(directory.read("stdout") == "prefix:" + std::string(12000, 'a') + "stdout end\n", "stdout log is incomplete");
    require(directory.read("stderr") == std::string(9000, 'b') + "stderr end\n", "stderr log is incomplete");
    co_await session.stop();

    // Redirecting only stdout must retain the ordinary stderr capture semantics.
    output                            = directory.file("mixed");
    spec                              = specification();
    spec.programs[0].argv             = {HARNESS_DESCRIPTOR_PEER, "output"};
    spec.programs[0].standardOutputFd = output.value;
    session                           = co_await Session::start(std::move(spec));
    co_await session.launch(0);
    const auto mixed = co_await session.wait(0);
    require(mixed.succeeded() && mixed.outputTruncated && mixed.standardOutput.empty() && mixed.standardError.size() == 1500 &&
                mixed.standardError.ends_with("stderr end\n"),
            "mixed output capture differs");
    co_await session.stop();
}

auto resourceLimits() -> awaitable<> {
    Directory directory;
    auto      input = directory.file("limit-input", "bounded");
    Fd        high(::fcntl(input.value, F_DUPFD_CLOEXEC, 512));
    systemCheck(high.value >= 0, "create high descriptor source");
    auto mapped                     = specification();
    mapped.programs[0].inheritedFds = {{high.value, 62}};
    mapped.programs[0].argv.insert(mapped.programs[0].argv.end(), {"62", "bounded"});
    mapped.programs[0].childLimits = ChildLimits{.openFiles = 64};
    auto bounded                   = co_await Session::start(std::move(mapped));
    high.reset();
    co_await bounded.launch(0);
    const auto mappedResult = co_await bounded.wait(0);
    require(mappedResult.succeeded(), "mapping under reduced descriptor limit failed: " + mappedResult.standardError);
    co_await bounded.stop();

    auto spec = specification(2);
    for (auto &program : spec.programs) {
        program.argv = {HARNESS_DESCRIPTOR_PEER, "limits"};
    }
    spec.programs[0].childLimits = ChildLimits{.coreBytes = 0, .openFiles = 64, .fileBytes = 2048, .cpuSeconds = 3};
    for (const auto value : {0, 64, 2048, 3}) {
        spec.programs[0].argv.insert(spec.programs[0].argv.end(), 2, std::to_string(value));
    }
    for (const auto resource : limitResources) {
        rlimit limit{};
        systemCheck(::getrlimit(resource, &limit) == 0, "inspect inherited limits");
        spec.programs[1].argv.insert(spec.programs[1].argv.end(), {std::to_string(limit.rlim_cur), std::to_string(limit.rlim_max)});
    }
    auto session = co_await Session::start(std::move(spec));
    for (std::size_t i = 0; i != 2; ++i) {
        co_await session.launch(i);
        const auto result = co_await session.wait(i);
        require(result.succeeded(), "child limits mismatch: " + result.standardError);
    }
    co_await session.stop();

    auto output                       = directory.file("capped");
    spec                              = specification(2);
    spec.programs[0].argv             = {HARNESS_DESCRIPTOR_PEER, "file-cap"};
    spec.programs[0].standardOutputFd = output.value;
    spec.programs[0].childLimits      = ChildLimits{.coreBytes = 0, .fileBytes = 2048};
    spec.programs[1].argv             = {HARNESS_DESCRIPTOR_PEER, "cpu-cap"};
    spec.programs[1].childLimits      = ChildLimits{.coreBytes = 0, .cpuSeconds = 1};
    session                           = co_await Session::start(std::move(spec));
    co_await session.launch(0);
    const auto capped = co_await session.wait(0);
    require(capped.signal == SIGXFSZ && !capped.succeeded(), "file limit did not report SIGXFSZ");
    require(directory.read("capped") == std::string(2048, 'x'), "file limit did not bound output");
    co_await session.launch(1);
    require((co_await session.wait(1)).signal == SIGKILL, "CPU hard limit did not terminate the child");
    co_await session.stop();
}

auto messageBoundary() -> awaitable<> {
    auto spec = specification();
    // Independent accounting of the launch envelope: fixed integer and string
    // prefixes total 64 bytes, plus node, mode, executable and argument data.
    constexpr std::size_t framing = 64;
    const auto fixed = framing + spec.programs[0].node.size() + spec.programs[0].argv[0].size() + spec.programs[0].argv[1].size();
    require(fixed < Channel::maxPayload, "fixture path leaves no command payload");
    spec.programs[0].argv.push_back(std::string(Channel::maxPayload - fixed, 'z'));
    auto session = co_await Session::start(spec);
    co_await session.stop();
    spec.programs[0].argv.back().push_back('z');
    co_await reject(std::move(spec), "payload");
}

auto setupFailure() -> awaitable<> {
    Directory directory;
    auto      output                  = directory.file("setup-error");
    auto      spec                    = specification();
    spec.programs[0].standardErrorFd  = output.value;
    spec.programs[0].workingDirectory = std::string(directory.path) + "/missing";
    auto session                      = co_await Session::start(std::move(spec));
    bool failed                       = false;
    try {
        co_await session.launch(0);
    } catch (const std::exception &error) {
        failed = std::string_view(error.what()).find("child working directory") != std::string_view::npos;
    }
    require(failed, "redirected setup error was not reported to launch");
    require(directory.read("setup-error").find("child working directory") != std::string::npos, "redirected setup diagnostic missing");

    // Failed configuration after descriptor retention must release session fds.
    const auto countFds = [] {
        return std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{});
    };
    const auto before                 = countFds();
    spec                              = specification();
    spec.programs[0].standardOutputFd = output.value;
    spec.topology.nodes.push_back({.name = "invalid!"});
    co_await reject(std::move(spec), "network names");
    require(countFds() == before, "partial startup leaked descriptors");
}

auto run(std::string_view mode) -> awaitable<> {
    if (mode == "preflight") {
        co_await preflight();
    } else if (mode == "ownership") {
        co_await ownership();
    } else if (mode == "output") {
        co_await anchorAndOutput();
    } else if (mode == "limits") {
        co_await resourceLimits();
    } else if (mode == "cleanup") {
        co_await setupFailure();
    } else if (mode == "boundary") {
        co_await messageBoundary();
    } else {
        throw std::runtime_error("unknown descriptor regression mode");
    }
}
} // namespace

int main(int argc, char **argv) {
    std::string error;
    if (argc != 2 || !gentest::detail::run_async_task_blocking(run(argv[1]), "descriptors", error)) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
