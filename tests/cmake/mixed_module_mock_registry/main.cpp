#include "consumer_api.hpp"

auto main() -> int {
    if (!run_legacy_mock()) {
        return 1;
    }
    if (!run_module_mocks()) {
        return 1;
    }
    return 0;
}
