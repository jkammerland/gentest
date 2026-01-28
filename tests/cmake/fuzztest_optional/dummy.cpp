#include <cstddef>
#include <cstdint>

[[using gentest: fuzz("smoke/optional")]]
void fuzz_optional(const std::uint8_t *data, std::size_t size) {
    (void)data;
    (void)size;
}
