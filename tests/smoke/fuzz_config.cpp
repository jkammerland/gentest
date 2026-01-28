// Fuzz target with gentest fuzz configuration attributes.

#include <gentest/attributes.h>

#include <string_view>

[[using gentest: fuzz("smoke/config/basic"), domains(positive, ascii_string, in_range(0, 10)), seed(1, "alpha", 2),
  seed(3, "beta", 4)]]
void fuzz_config(int value, std::string_view text, int count) {
    (void)value;
    (void)text;
    (void)count;
}
