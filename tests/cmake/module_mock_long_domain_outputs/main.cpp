auto run_provider_self() -> bool;

auto main() -> int { return run_provider_self() ? 0 : 1; }
