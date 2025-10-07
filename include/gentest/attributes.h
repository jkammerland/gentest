#pragma once

// Annotation guidance:
//
// Use standard C++11 attributes with a vendor namespace to tag tests and
// attach metadata:
//
//   [[using gentest : test("suite/case"), group("core"), req("BUG-123"), slow, linux]]
//   void my_test();
//
// Fixture composition for free functions:
//   [[using gentest : test("suite/free"), fixtures(A, B, C)]]
//   void my_free_test(A& a, B& b, C& c);
//   - A/B/C are default-constructed per invocation and passed by reference.
//   - If a fixture derives from gentest::FixtureSetup/TearDown, setUp/tearDown are called automatically.
//   - Applies to free functions only (not member tests) and is always ephemeral.
//
// The `test("...")` attribute is required and defines the public case name.
// Additional attribute names (e.g. `group("name")`, `slow`, `linux`) are collected as tags,
// while attributes such as `req("BUG-123")` or `skip("reason")` attach
// requirements or skipping instructions. All information is extracted by the
// code generator—no macros or compiler-specific annotations required.

// This header intentionally declares no symbols; it documents the attribute
// format consumed by the generator and serves as a stable include for tests.
