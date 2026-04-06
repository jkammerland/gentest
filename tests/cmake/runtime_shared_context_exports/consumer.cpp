#include "gentest/context.h"

int main() {
    gentest::log("shared-runtime-consumer");
    gentest::set_log_policy(gentest::LogPolicy::Never);
    gentest::skip_if(false, "unused");
    gentest::xfail_if(false, "unused");
    return 0;
}
