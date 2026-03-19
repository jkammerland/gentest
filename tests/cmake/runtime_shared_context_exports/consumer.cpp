#include "gentest/context.h"

int main() {
    gentest::log("shared-runtime-consumer");
    gentest::log_on_fail(false);
    gentest::skip_if(false, "unused");
    gentest::xfail_if(false, "unused");
    return 0;
}
