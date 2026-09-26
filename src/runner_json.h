#pragma once

#include <cstdint>
#include <fmt/format.h>
#include <iterator>
#include <string>
#include <string_view>

namespace gentest::runner {

// Emit valid UTF-8 JSON even when caller-owned metadata contains malformed bytes.
inline void append_json_string(fmt::memory_buffer &out, std::string_view value) {
    out.push_back('"');
    for (std::size_t index = 0; index < value.size();) {
        const auto ch = static_cast<unsigned char>(value[index]);
        switch (ch) {
        case '"':
            out.push_back('\\');
            out.push_back('"');
            ++index;
            break;
        case '\\':
            out.push_back('\\');
            out.push_back('\\');
            ++index;
            break;
        case '\b':
            out.push_back('\\');
            out.push_back('b');
            ++index;
            break;
        case '\f':
            out.push_back('\\');
            out.push_back('f');
            ++index;
            break;
        case '\n':
            out.push_back('\\');
            out.push_back('n');
            ++index;
            break;
        case '\r':
            out.push_back('\\');
            out.push_back('r');
            ++index;
            break;
        case '\t':
            out.push_back('\\');
            out.push_back('t');
            ++index;
            break;
        default:
            if (ch < 0x20) {
                fmt::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<unsigned>(ch));
                ++index;
            } else {
                std::size_t   sequence_length = 0;
                std::uint32_t code_point      = 0;
                if (ch < 0x80U) {
                    sequence_length = 1;
                    code_point      = ch;
                } else if ((ch & 0xE0U) == 0xC0U) {
                    sequence_length = 2;
                    code_point      = ch & 0x1FU;
                } else if ((ch & 0xF0U) == 0xE0U) {
                    sequence_length = 3;
                    code_point      = ch & 0x0FU;
                } else if ((ch & 0xF8U) == 0xF0U) {
                    sequence_length = 4;
                    code_point      = ch & 0x07U;
                }

                bool valid = sequence_length != 0 && index + sequence_length <= value.size();
                for (std::size_t offset = 1; valid && offset < sequence_length; ++offset) {
                    const auto continuation = static_cast<unsigned char>(value[index + offset]);
                    if ((continuation & 0xC0U) != 0x80U) {
                        valid = false;
                        break;
                    }
                    code_point = (code_point << 6U) | (continuation & 0x3FU);
                }
                if (valid && ((sequence_length == 2 && code_point < 0x80U) || (sequence_length == 3 && code_point < 0x800U) ||
                              (sequence_length == 4 && code_point < 0x10000U) || (code_point >= 0xD800U && code_point <= 0xDFFFU) ||
                              code_point > 0x10FFFFU)) {
                    valid = false;
                }
                if (!valid) {
                    out.append(std::string_view{"\\uFFFD"});
                    ++index;
                    break;
                }
                for (std::size_t offset = 0; offset < sequence_length; ++offset) {
                    out.push_back(value[index + offset]);
                }
                index += sequence_length;
            }
        }
    }
    out.push_back('"');
}

inline std::string json_string(std::string_view value) {
    fmt::memory_buffer out;
    append_json_string(out, value);
    return fmt::to_string(out);
}

} // namespace gentest::runner
