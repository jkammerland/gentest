#include "gentest/context.h"

auto main() -> int {
    // No run is active, so the runtime must return a disabled portable notifier.
    const auto notifier = gentest::currentNotifier();
    return !notifier.active() && notifier.progress("outside a run") == gentest::NotifyResult::disabled ? 0 : 1;
}
