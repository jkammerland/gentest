#include "discovery.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <iostream>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using gentest::codegen::FixtureDeclInfo;
using gentest::codegen::FixtureScope;
using gentest::codegen::FreeFixtureUse;
using gentest::codegen::resolve_free_fixtures;
using gentest::codegen::TestCaseInfo;

namespace {

struct Run {
    int  failures = 0;
    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }
    void contains(std::string_view haystack, std::string_view needle, std::string_view msg) {
        expect(haystack.find(needle) != std::string_view::npos, msg);
    }
};

#if defined(_WIN32)
int  close_fd(int fd) { return _close(fd); }
int  dup_fd(int fd) { return _dup(fd); }
int  make_pipe(int fds[2]) { return _pipe(fds, 4096, _O_BINARY); }
int  read_fd(int fd, char *buffer, unsigned int size) { return _read(fd, buffer, size); }
bool redirect_fd(int from, int to) { return _dup2(from, to) == 0; }
#else
int  close_fd(int fd) { return close(fd); }
int  dup_fd(int fd) { return dup(fd); }
int  make_pipe(int fds[2]) { return pipe(fds); }
int  read_fd(int fd, char *buffer, std::size_t size) { return static_cast<int>(read(fd, buffer, size)); }
bool redirect_fd(int from, int to) { return dup2(from, to) >= 0; }
#endif

template <typename Fn> std::string capture_stderr(Fn &&fn) {
    std::fflush(stderr);
    llvm::errs().flush();

    int pipe_fds[2] = {-1, -1};
    if (make_pipe(pipe_fds) != 0) {
        std::cerr << "capture_stderr: pipe failed: " << std::strerror(errno) << "\n";
        return {};
    }

    const int saved_stderr = dup_fd(2);
    if (saved_stderr < 0) {
        std::cerr << "capture_stderr: dup failed: " << std::strerror(errno) << "\n";
        close_fd(pipe_fds[0]);
        close_fd(pipe_fds[1]);
        return {};
    }

    if (!redirect_fd(pipe_fds[1], 2)) {
        std::cerr << "capture_stderr: dup2 failed: " << std::strerror(errno) << "\n";
        close_fd(saved_stderr);
        close_fd(pipe_fds[0]);
        close_fd(pipe_fds[1]);
        return {};
    }
    close_fd(pipe_fds[1]);
    pipe_fds[1] = -1;

    std::string out;
    std::thread reader([&] {
        char buffer[512];
        while (true) {
            const int n = read_fd(pipe_fds[0], buffer, sizeof(buffer));
            if (n <= 0) {
                break;
            }
            out.append(buffer, static_cast<std::size_t>(n));
        }
        close_fd(pipe_fds[0]);
        pipe_fds[0] = -1;
    });

    const auto restore_stderr = [&] {
        std::fflush(stderr);
        llvm::errs().flush();
        redirect_fd(saved_stderr, 2);
        close_fd(saved_stderr);
    };

    try {
        std::forward<Fn>(fn)();
        restore_stderr();
    } catch (...) {
        restore_stderr();
        if (reader.joinable()) {
            reader.join();
        }
        throw;
    }
    if (reader.joinable()) {
        reader.join();
    }
    return out;
}

auto make_test(std::string display_name, std::vector<std::string> namespace_parts, std::string tu_name = "tu.cpp") -> TestCaseInfo {
    TestCaseInfo info{};
    info.display_name    = std::move(display_name);
    info.filename        = "tests.cpp";
    info.line            = 17;
    info.tu_filename     = std::move(tu_name);
    info.namespace_parts = std::move(namespace_parts);
    return info;
}

auto make_fixture(std::string qualified_name, std::vector<std::string> namespace_parts, FixtureScope scope, std::string tu_name,
                  std::string suite_name = {}) -> FixtureDeclInfo {
    FixtureDeclInfo info{};
    info.qualified_name  = std::move(qualified_name);
    info.namespace_parts = std::move(namespace_parts);
    info.scope           = scope;
    info.tu_filename     = std::move(tu_name);
    info.suite_name      = std::move(suite_name);

    const std::size_t pos = info.qualified_name.rfind("::");
    if (pos == std::string::npos) {
        info.base_name = info.qualified_name;
    } else {
        info.base_name = info.qualified_name.substr(pos + 2);
    }
    return info;
}

} // namespace

int main() {
    Run t;

    {
        std::vector<TestCaseInfo> cases;

        auto namespaced               = make_test("local-template", {"alpha", "beta"}, "tu_local.cpp");
        namespaced.free_fixture_types = {"LocalFx<std::pair<int, long>>"};
        namespaced.free_fixtures      = {FreeFixtureUse{.type_name = "stale::Fixture", .scope = FixtureScope::Global}};
        cases.push_back(std::move(namespaced));

        auto root               = make_test("root-local", {}, "tu_local.cpp");
        root.free_fixture_types = {"RootFx"};
        cases.push_back(std::move(root));

        t.expect(resolve_free_fixtures(cases, {}), "local fallback cases resolve");
        t.expect(cases[0].free_fixtures.size() == 1, "local fallback clears stale fixtures");
        t.expect(cases[0].free_fixtures[0].scope == FixtureScope::Local, "local fallback keeps local scope");
        t.expect(cases[0].free_fixtures[0].type_name == "alpha::beta::LocalFx<std::pair<int, long>>",
                 "local fallback qualifies templated fixture in namespace");
        t.expect(cases[1].free_fixtures.size() == 1, "root fallback resolves one fixture");
        t.expect(cases[1].free_fixtures[0].type_name == "RootFx", "local fallback keeps root fixture unqualified");
    }

    {
        const std::vector<FixtureDeclInfo> fixtures = {
            make_fixture("outer::inner::SharedFx", {"outer", "inner"}, FixtureScope::Suite, "tu_decl.cpp", "outer/inner"),
            make_fixture("outer::SharedFx", {"outer"}, FixtureScope::Suite, "tu_decl.cpp", "outer"),
        };

        std::vector<TestCaseInfo> cases;
        auto                      test = make_test("deep-shared", {"outer", "inner", "leaf"}, "tu_decl.cpp");
        test.free_fixture_types        = {"SharedFx<std::pair<int, int>>"};
        cases.push_back(std::move(test));

        t.expect(resolve_free_fixtures(cases, fixtures), "deepest visible fixture resolves");
        t.expect(cases[0].free_fixtures.size() == 1, "deepest visible fixture resolves one use");
        t.expect(cases[0].free_fixtures[0].scope == FixtureScope::Suite, "deepest visible fixture preserves suite scope");
        t.expect(cases[0].free_fixtures[0].suite_name == "outer/inner", "deepest visible fixture chooses nearest suite");
        t.expect(cases[0].free_fixtures[0].type_name == "outer::inner::SharedFx<std::pair<int, int>>",
                 "deepest visible fixture preserves template suffix");
    }

    {
        const std::vector<FixtureDeclInfo> fixtures = {
            make_fixture("outer::GlobalFx", {"outer"}, FixtureScope::Global, "tu_q.cpp"),
        };

        std::vector<TestCaseInfo> cases;
        auto                      qualified = make_test("qualified-shared", {"outer", "leaf"}, "tu_q.cpp");
        qualified.free_fixture_types        = {"::outer::GlobalFx"};
        cases.push_back(std::move(qualified));

        auto cross_tu               = make_test("cross-tu-local", {"outer", "leaf"}, "tu_other.cpp");
        cross_tu.free_fixture_types = {"GlobalFx"};
        cases.push_back(std::move(cross_tu));

        auto explicit_local               = make_test("qualified-local", {"outer"}, "tu_missing.cpp");
        explicit_local.free_fixture_types = {"::custom::LocalFx"};
        cases.push_back(std::move(explicit_local));

        t.expect(resolve_free_fixtures(cases, fixtures), "qualified and cross-tu fallbacks resolve");
        t.expect(cases[0].free_fixtures[0].type_name == "outer::GlobalFx", "qualified lookup strips leading colons");
        t.expect(cases[0].free_fixtures[0].scope == FixtureScope::Global, "qualified lookup preserves declared scope");
        t.expect(cases[1].free_fixtures[0].type_name == "outer::leaf::GlobalFx", "fixtures from another TU fall back to local");
        t.expect(cases[1].free_fixtures[0].scope == FixtureScope::Local, "cross-tu fallback uses local scope");
        t.expect(cases[2].free_fixtures[0].type_name == "::custom::LocalFx", "qualified local fallback preserves explicit qualification");
    }

    {
        const std::vector<FixtureDeclInfo> fixtures = {
            make_fixture("outer::SharedFx", {"outer"}, FixtureScope::Suite, "tu_err.cpp", "outer"),
            make_fixture("outer::inner::NestedFx", {"outer", "inner"}, FixtureScope::Suite, "tu_err.cpp", "outer/inner"),
            make_fixture("outer::GlobalFx", {"outer"}, FixtureScope::Global, "tu_req.cpp"),
        };

        std::vector<TestCaseInfo> error_cases;

        auto invisible_qualified               = make_test("invisible-qualified", {"different"}, "tu_err.cpp");
        invisible_qualified.free_fixture_types = {"::outer::SharedFx"};
        error_cases.push_back(std::move(invisible_qualified));

        auto invisible_unqualified               = make_test("invisible-unqualified", {"different"}, "tu_err.cpp");
        invisible_unqualified.free_fixture_types = {"SharedFx"};
        error_cases.push_back(std::move(invisible_unqualified));

        auto shorter_namespace               = make_test("shorter-namespace", {"outer"}, "tu_err.cpp");
        shorter_namespace.free_fixture_types = {"NestedFx"};
        error_cases.push_back(std::move(shorter_namespace));

        auto empty_type               = make_test("empty-type", {"outer"}, "tu_err.cpp");
        empty_type.free_fixture_types = {"   "};
        error_cases.push_back(std::move(empty_type));

        const std::string output =
            capture_stderr([&] { t.expect(!resolve_free_fixtures(error_cases, fixtures), "resolver reports invalid fixture cases"); });

        t.expect(error_cases[0].free_fixtures.empty(), "invisible qualified fixture is rejected");
        t.expect(error_cases[1].free_fixtures.empty(), "invisible unqualified fixture is rejected");
        t.expect(error_cases[2].free_fixtures.empty(), "deeper fixture namespace is rejected");
        t.expect(error_cases[3].free_fixtures.empty(), "empty fixture type is rejected");

        t.contains(output, "fixture 'outer::SharedFx' is not in an ancestor namespace of this test",
                   "resolver reports invisible qualified fixture");
        t.contains(output, "fixture 'SharedFx' is not in an ancestor namespace of this test",
                   "resolver reports invisible unqualified fixture");
        t.contains(output, "fixture 'NestedFx' is not in an ancestor namespace of this test", "resolver reports deeper fixture namespace");
        t.contains(output, "empty inferred fixture type from function signature", "resolver reports empty fixture type");
    }

    if (t.failures != 0) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
