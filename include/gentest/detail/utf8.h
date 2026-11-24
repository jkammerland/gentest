#pragma once

#include <cstddef>
#include <string_view>

namespace gentest::detail {

struct Utf8DecodeResult {
    char32_t    code_point = 0;
    std::size_t width      = 1;
    bool        valid      = false;
};

inline bool isUtf8Continuation(unsigned char ch) { return (ch & 0xC0U) == 0x80U; }

inline Utf8DecodeResult decodeUtf8(std::string_view s, std::size_t pos) {
    const auto remaining = s.size() - pos;
    const auto first     = static_cast<unsigned char>(s[pos]);
    if (first < 0x80U) {
        return {.code_point = first, .width = 1, .valid = true};
    }
    if (first < 0xC2U) {
        return {};
    }
    if (first <= 0xDFU) {
        if (remaining < 2) {
            return {};
        }
        const auto second = static_cast<unsigned char>(s[pos + 1]);
        if (!isUtf8Continuation(second)) {
            return {};
        }
        return {.code_point = static_cast<char32_t>(((first & 0x1FU) << 6U) | (second & 0x3FU)), .width = 2, .valid = true};
    }
    if (first <= 0xEFU) {
        if (remaining < 3) {
            return {};
        }
        const auto second = static_cast<unsigned char>(s[pos + 1]);
        const auto third  = static_cast<unsigned char>(s[pos + 2]);
        if (!isUtf8Continuation(second) || !isUtf8Continuation(third)) {
            return {};
        }
        if ((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second >= 0xA0U)) {
            return {};
        }
        return {.code_point = static_cast<char32_t>(((first & 0x0FU) << 12U) | ((second & 0x3FU) << 6U) | (third & 0x3FU)),
                .width      = 3,
                .valid      = true};
    }
    if (first <= 0xF4U) {
        if (remaining < 4) {
            return {};
        }
        const auto second = static_cast<unsigned char>(s[pos + 1]);
        const auto third  = static_cast<unsigned char>(s[pos + 2]);
        const auto fourth = static_cast<unsigned char>(s[pos + 3]);
        if (!isUtf8Continuation(second) || !isUtf8Continuation(third) || !isUtf8Continuation(fourth)) {
            return {};
        }
        if ((first == 0xF0U && second < 0x90U) || (first == 0xF4U && second >= 0x90U)) {
            return {};
        }
        return {.code_point = static_cast<char32_t>(((first & 0x07U) << 18U) | ((second & 0x3FU) << 12U) | ((third & 0x3FU) << 6U) |
                                                    (fourth & 0x3FU)),
                .width      = 4,
                .valid      = true};
    }
    return {};
}

} // namespace gentest::detail
