#pragma once

// Annotation guidance:
//
// Use standard C++11 attributes with a vendor namespace to tag tests and
// attach metadata:
//
//   [[using gentest : test("suite/case"), req("BUG-123"), slow, linux]]
//   void my_test();
//
// The `test("...")` attribute is required and defines the public case name.
// Additional attribute names (e.g. `slow`, `linux`) are collected as tags,
// while attributes such as `req("BUG-123")` or `skip("reason")` attach
// requirements or skipping instructions. All information is extracted by the
// code generator—no macros or compiler-specific annotations required.
