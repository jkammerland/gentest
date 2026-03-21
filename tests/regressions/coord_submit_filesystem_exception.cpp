#include <filesystem>
#include <iostream>

#define main coordd_embedded_main_for_submit_filesystem_exception_test
#include "../../coordd/main.cpp"
#undef main

int main() {
#ifdef _WIN32
    return 0;
#else
    coordd::SessionManager sessions("/dev/null", coord::TlsConfig{});

    coord::SessionSpec spec{};
    spec.session_id = "fs-exception";
    spec.group = "regressions";
    spec.mode = coord::ExecMode::A;
    spec.nodes.push_back(coord::NodeDef{
        .name = "probe",
        .exec = "/bin/true",
    });

    const std::string id = sessions.submit(spec, "");
    coord::SessionManifest manifest = sessions.wait(id);

    if (manifest.result != coord::ResultCode::Error) {
        std::cerr << "expected error manifest when session artifacts cannot be created\n";
        return 1;
    }
    if (manifest.fail_reason.empty()) {
        std::cerr << "expected filesystem failure reason in manifest\n";
        return 1;
    }

    return 0;
#endif
}
