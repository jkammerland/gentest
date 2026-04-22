import gentest;

auto main() -> int {
    auto *snapshot_registered_cases = &gentest::detail::snapshot_registered_cases;
    (void)snapshot_registered_cases;
    return 0;
}
