#pragma once

// jkammerland/doctest@397ce008 uses std::tuple_cat without including <tuple>.
// Keep this compatibility include adjacent to the fork include so the benchmark
// measures what a consumer must include for this fork on this toolchain.
// clang-format off
#include <tuple>
#include <doctest/doctest.h>
// clang-format on
