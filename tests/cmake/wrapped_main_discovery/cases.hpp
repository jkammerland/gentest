#pragma once

#include <gentest/attributes.h>

[[using gentest: test("wrapped_main/main_case")]]
void main_case();

[[using gentest: test("wrapped_main/other_case")]]
void other_case();
