#include "consumer_api.hpp"

auto main() -> int {
    if (!run_header_defined_from_module()) {
        return 1;
    }
    if (!run_provider_self()) {
        return 1;
    }
    return 0;
}
