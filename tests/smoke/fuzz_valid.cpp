// Valid fuzz target declarations for gentest_codegen smoke tests.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

[[using gentest: fuzz("smoke/typed_string")]]
void fuzz_typed_string(int value, const std::string &text) {
    (void)value;
    (void)text;
}

[[using gentest: fuzz("smoke/bytes_span")]]
void fuzz_bytes_span(std::span<const std::uint8_t> data) { (void)data; }

[[using gentest: fuzz("smoke/bytes_ptr_size")]]
void fuzz_bytes_ptr_size(const std::uint8_t *data, std::size_t size) {
    (void)data;
    (void)size;
}

