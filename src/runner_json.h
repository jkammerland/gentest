#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fmt/format.h>
#include <iterator>
#include <string>
#include <string_view>

namespace gentest::runner {

namespace json_detail {

// How a byte begins inside a JSON string: copied as is, escaped, the lead byte of
// a UTF-8 sequence of that many bytes (still to be validated), or never valid.
enum ByteKind : std::uint8_t { kCopy = 0, kEscape = 1, kLead2 = 2, kLead3 = 3, kLead4 = 4, kInvalid = 5 };

constexpr std::array<std::uint8_t, 256> make_byte_kinds() noexcept {
    std::array<std::uint8_t, 256> kinds{};
    for (unsigned byte = 0; byte < kinds.size(); ++byte) {
        // Bytes 0x80-0xC1 (continuations, and C0/C1, which only start overlong forms)
        // and 0xF5-0xFF (beyond U+10FFFF) never start a well-formed sequence.
        std::uint8_t kind = byte < 0x80U ? kCopy : kInvalid;
        if (byte < 0x20U || byte == '"' || byte == '\\') {
            kind = kEscape;
        } else if (byte >= 0xC2U && byte < 0xE0U) {
            kind = kLead2;
        } else if (byte >= 0xE0U && byte < 0xF0U) {
            kind = kLead3;
        } else if (byte >= 0xF0U && byte < 0xF5U) {
            kind = kLead4;
        }
        kinds[byte] = kind;
    }
    return kinds;
}

inline constexpr std::array<std::uint8_t, 256> kByteKinds = make_byte_kinds();

constexpr bool is_continuation(unsigned char byte) noexcept { return (byte & 0xC0U) == 0x80U; }

// Length of the well-formed UTF-8 sequence at `text` (Unicode Table 3-7), or 0 when
// it is truncated, overlong, a surrogate, or above U+10FFFF.
inline std::size_t utf8_sequence_length(const char *text, std::size_t available, std::uint8_t length) noexcept {
    if (available < length) {
        return 0;
    }
    const auto lead   = static_cast<unsigned char>(text[0]);
    const auto second = static_cast<unsigned char>(text[1]);
    if (length == kLead2) {
        return is_continuation(second) ? 2 : 0;
    }
    const auto third = static_cast<unsigned char>(text[2]);
    if (length == kLead3) {
        bool second_ok = is_continuation(second);
        if (lead == 0xE0U) {
            second_ok = second >= 0xA0U && second <= 0xBFU;
        } else if (lead == 0xEDU) {
            second_ok = second >= 0x80U && second <= 0x9FU;
        }
        return second_ok && is_continuation(third) ? 3 : 0;
    }
    bool second_ok = is_continuation(second);
    if (lead == 0xF0U) {
        second_ok = second >= 0x90U && second <= 0xBFU;
    } else if (lead == 0xF4U) {
        second_ok = second >= 0x80U && second <= 0x8FU;
    }
    return second_ok && is_continuation(third) && is_continuation(static_cast<unsigned char>(text[3])) ? 4 : 0;
}

// True when none of the eight bytes needs an escape or UTF-8 validation: every
// byte is ASCII, at least 0x20, and neither '"' nor '\\'. Each test is exact for
// "some byte matches"; a borrow only spreads from a byte that already matches.
constexpr bool plain_ascii_word(std::uint64_t word) noexcept {
    constexpr std::uint64_t ones      = 0x0101010101010101ULL;
    constexpr std::uint64_t high_bits = 0x8080808080808080ULL;
    const std::uint64_t     quote     = word ^ (ones * 0x22U);
    const std::uint64_t     backslash = word ^ (ones * 0x5CU);
    const std::uint64_t     below_20  = (word - (ones * 0x20U)) & ~word;
    const std::uint64_t     is_quote  = (quote - ones) & ~quote;
    const std::uint64_t     is_slash  = (backslash - ones) & ~backslash;
    return ((word | below_20 | is_quote | is_slash) & high_bits) == 0;
}

inline void append_escape(fmt::memory_buffer &out, unsigned char byte) {
    switch (byte) {
    case '"': out.append(std::string_view{"\\\""}); return;
    case '\\': out.append(std::string_view{"\\\\"}); return;
    case '\b': out.append(std::string_view{"\\b"}); return;
    case '\f': out.append(std::string_view{"\\f"}); return;
    case '\n': out.append(std::string_view{"\\n"}); return;
    case '\r': out.append(std::string_view{"\\r"}); return;
    case '\t': out.append(std::string_view{"\\t"}); return;
    default: break;
    }
    constexpr std::string_view kHex  = "0123456789abcdef";
    const unsigned             value = byte;
    const std::array<char, 6>  escaped{'\\', 'u', '0', '0', kHex[(value >> 4U) & 0xFU], kHex[value & 0xFU]};
    out.append(escaped.data(), escaped.data() + escaped.size());
}

} // namespace json_detail

// Appends `value` as a JSON string literal. The output is always valid UTF-8 JSON,
// even when caller-owned text holds malformed bytes: each byte that does not start a
// well-formed UTF-8 sequence becomes U+FFFD. Runs that need no escaping are copied in
// bulk, and plain ASCII is scanned eight bytes at a time.
inline void append_json_string(fmt::memory_buffer &out, std::string_view value) {
    out.reserve(out.size() + value.size() + 2);
    out.push_back('"');
    const char *const data  = value.data();
    const std::size_t size  = value.size();
    std::size_t       index = 0;
    std::size_t       run   = 0; // first byte not yet copied to `out`
    while (index < size) {
        while (size - index >= 8) {
            std::uint64_t word = 0;
            std::memcpy(&word, data + index, sizeof(word));
            if (!json_detail::plain_ascii_word(word)) {
                break;
            }
            index += 8;
        }
        // Classify one byte at a time across the word that stopped the fast scan.
        const std::size_t stop = (size - index > 8) ? index + 8 : size;
        while (index < stop) {
            const auto         byte = static_cast<unsigned char>(data[index]);
            const std::uint8_t kind = json_detail::kByteKinds[byte];
            if (kind == json_detail::kCopy) {
                ++index;
                continue;
            }
            if (kind >= json_detail::kLead2 && kind <= json_detail::kLead4) {
                const std::size_t length = json_detail::utf8_sequence_length(data + index, size - index, kind);
                if (length != 0) {
                    index += length;
                    continue;
                }
            }
            out.append(data + run, data + index);
            if (kind == json_detail::kEscape) {
                json_detail::append_escape(out, byte);
            } else {
                out.append(std::string_view{"\\uFFFD"});
            }
            ++index;
            run = index;
        }
    }
    out.append(data + run, data + size);
    out.push_back('"');
}

inline std::string json_string(std::string_view value) {
    fmt::memory_buffer out;
    append_json_string(out, value);
    return fmt::to_string(out);
}

// Writes compact JSON (no whitespace) into a buffer. Object members keep the order
// in which they are written. Callers keep the structure balanced and shallow.
class JsonWriter {
  public:
    explicit JsonWriter(fmt::memory_buffer &out) noexcept : out_(&out) {}

    void begin_object() { open('{'); }
    void end_object() { close('}'); }
    void begin_array() { open('['); }
    void end_array() { close(']'); }

    void key(std::string_view name) {
        separate();
        append_json_string(*out_, name);
        out_->push_back(':');
        after_key_ = true;
    }
    void text(std::string_view value) {
        separate();
        append_json_string(*out_, value);
    }
    void integer(std::int64_t value) {
        separate();
        fmt::format_to(std::back_inserter(*out_), "{}", value);
    }
    void boolean(bool value) {
        separate();
        out_->append(value ? std::string_view{"true"} : std::string_view{"false"});
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a member name and its value, in JSON order.
    void text_field(std::string_view name, std::string_view value) {
        key(name);
        text(value);
    }
    void integer_field(std::string_view name, std::int64_t value) {
        key(name);
        integer(value);
    }

  private:
    static constexpr std::size_t kMaxDepth = 16;

    void open(char bracket) {
        separate();
        out_->push_back(bracket);
        if (depth_ < kMaxDepth) {
            has_members_[depth_] = false;
            ++depth_;
        }
    }
    void close(char bracket) {
        if (depth_ > 0) {
            --depth_;
        }
        out_->push_back(bracket);
    }
    // Emits the comma before a value or member, except directly after its key.
    void separate() {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (depth_ == 0) {
            return;
        }
        if (has_members_[depth_ - 1]) {
            out_->push_back(',');
        }
        has_members_[depth_ - 1] = true;
    }

    fmt::memory_buffer         *out_;
    std::array<bool, kMaxDepth> has_members_{};
    std::size_t                 depth_     = 0;
    bool                        after_key_ = false;
};

} // namespace gentest::runner
