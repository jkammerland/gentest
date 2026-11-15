// Regression: the JSON encoder shared by every report escapes exactly what JSON
// needs and replaces each byte that does not start well-formed UTF-8 with U+FFFD.
// It matches a byte-by-byte reference on every 1- and 2-byte input and on a
// deterministic pseudo-random corpus, including bytes that straddle the eight-byte
// words of its ASCII fast path. JsonWriter places commas and nesting correctly.
#include "../../src/runner_json.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAILED: " << what << '\n';
    }
}

std::string hex_bytes(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        out += fmt::format("{:02x} ", static_cast<unsigned>(static_cast<unsigned char>(ch)));
    }
    return out;
}

// SplitMix64: a fixed, portable sequence so every platform checks the same corpus.
std::uint64_t next_random(std::uint64_t &state) {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state;
    value               = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value               = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

// Decodes each sequence and checks it against the Unicode scalar value ranges.
std::string reference(std::string_view value) {
    std::string out = "\"";
    for (std::size_t index = 0; index < value.size();) {
        const auto ch = static_cast<unsigned char>(value[index]);
        switch (ch) {
        case '"':
            out += "\\\"";
            ++index;
            continue;
        case '\\':
            out += "\\\\";
            ++index;
            continue;
        case '\b':
            out += "\\b";
            ++index;
            continue;
        case '\f':
            out += "\\f";
            ++index;
            continue;
        case '\n':
            out += "\\n";
            ++index;
            continue;
        case '\r':
            out += "\\r";
            ++index;
            continue;
        case '\t':
            out += "\\t";
            ++index;
            continue;
        default: break;
        }
        if (ch < 0x20U) {
            out += fmt::format("\\u{:04x}", static_cast<unsigned>(ch));
            ++index;
            continue;
        }
        std::size_t   length     = 0;
        std::uint32_t code_point = 0;
        if (ch < 0x80U) {
            length     = 1;
            code_point = ch;
        } else if ((ch & 0xE0U) == 0xC0U) {
            length     = 2;
            code_point = ch & 0x1FU;
        } else if ((ch & 0xF0U) == 0xE0U) {
            length     = 3;
            code_point = ch & 0x0FU;
        } else if ((ch & 0xF8U) == 0xF0U) {
            length     = 4;
            code_point = ch & 0x07U;
        }
        bool valid = length != 0 && index + length <= value.size();
        for (std::size_t offset = 1; valid && offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            valid                   = (continuation & 0xC0U) == 0x80U;
            code_point              = (code_point << 6U) | (continuation & 0x3FU);
        }
        const bool overlong =
            (length == 2 && code_point < 0x80U) || (length == 3 && code_point < 0x800U) || (length == 4 && code_point < 0x10000U);
        if (!valid || overlong || (code_point >= 0xD800U && code_point <= 0xDFFFU) || code_point > 0x10FFFFU) {
            out += "\\uFFFD";
            ++index;
            continue;
        }
        out.append(value.substr(index, length));
        index += length;
    }
    out += '"';
    return out;
}

void expect_encoding(std::string_view input, std::string_view expected) {
    const std::string actual = gentest::runner::json_string(input);
    expect(actual == expected, "encode [" + hex_bytes(input) + "] gives " + actual + ", expected " + std::string(expected));
}

void expect_reference(std::string_view input) {
    const std::string actual = gentest::runner::json_string(input);
    const std::string wanted = reference(input);
    if (actual != wanted) {
        expect(false, "encode [" + hex_bytes(input) + "] gives " + actual + ", reference " + wanted);
    }
}

void known_inputs() {
    expect_encoding("", R"("")");
    expect_encoding("plain ascii text", R"("plain ascii text")");
    expect_encoding("q\"b\\", R"("q\"b\\")");
    expect_encoding("\b\f\n\r\t", R"("\b\f\n\r\t")");
    expect_encoding(std::string_view("\x00\x01\x1f", 3), R"("\u0000\u0001\u001f")");
    expect_encoding("\x7f", "\"\x7f\"");
    expect_encoding("\xc3\xa5 \xe2\x82\xac \xf0\x9f\x98\x80", "\"\xc3\xa5 \xe2\x82\xac \xf0\x9f\x98\x80\"");
    expect_encoding("\xc0\x80", R"("\uFFFD\uFFFD")");                     // overlong NUL
    expect_encoding("\xe0\x80\x80", R"("\uFFFD\uFFFD\uFFFD")");           // overlong three-byte form
    expect_encoding("\xed\xa0\x80", R"("\uFFFD\uFFFD\uFFFD")");           // UTF-16 surrogate
    expect_encoding("\xf4\x90\x80\x80", R"("\uFFFD\uFFFD\uFFFD\uFFFD")"); // above U+10FFFF
    expect_encoding("\xf5\xff", R"("\uFFFD\uFFFD")");
    expect_encoding("\x80", R"("\uFFFD")");
    expect_encoding("ok\xe2\x82", R"("ok\uFFFD\uFFFD")"); // truncated at the end
    expect_encoding("\xe2(\xa1", R"("\uFFFD(\uFFFD")");
    expect_encoding("\xf0\x90\x80\x80\xf4\x8f\xbf\xbf", "\"\xf0\x90\x80\x80\xf4\x8f\xbf\xbf\""); // first and last 4-byte scalars
}

// The fast path reads eight bytes at a time: put the byte of interest at every
// offset of a long string so it lands in each lane and across word boundaries.
void word_boundaries() {
    const std::array<std::string_view, 9> specials{
        "\"", "\\", "\n", std::string_view("\0", 1), "\x1f", "\xc3\xa5", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xff"};
    for (const std::string_view special : specials) {
        for (std::size_t position = 0; position < 40; ++position) {
            std::string text(48, 'a');
            text.replace(position, 1, special);
            expect_reference(text);
            text.resize(position + special.size() + 1);
            expect_reference(text);
        }
    }
    std::string long_ascii(4096, 'x');
    long_ascii[4095] = '"';
    expect_reference(long_ascii);
}

void exhaustive_short_inputs() {
    std::string text;
    for (unsigned first = 0; first < 256; ++first) {
        text.assign(1, static_cast<char>(first));
        expect_reference(text);
        for (unsigned second = 0; second < 256; ++second) {
            text.assign({static_cast<char>(first), static_cast<char>(second)});
            expect_reference(text);
        }
    }
}

void random_corpus() {
    constexpr std::array<unsigned char, 36> interesting{0x00, 0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x1F, 0x20, 0x22, 0x5C, 0x41, 0x7F,
                                                        0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xE1,
                                                        0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xF7, 0xF8, 0xFF};
    std::uint64_t                           state = 0x5EEDU;
    std::string                             text;
    for (int iteration = 0; iteration < 300000; ++iteration) {
        const std::size_t length = next_random(state) % 41;
        const auto        mode   = static_cast<unsigned>(next_random(state) % 3);
        text.resize(length);
        for (char &ch : text) {
            const std::uint64_t value = next_random(state);
            unsigned char       byte  = 0;
            if (mode == 0) {
                byte = static_cast<unsigned char>(value);
            } else if (mode == 1) {
                byte = interesting[value % interesting.size()];
            } else {
                byte = (value % 3 == 0) ? static_cast<unsigned char>(0x80U | ((value >> 8U) & 0x3FU))
                                        : interesting[(value >> 16U) % interesting.size()];
            }
            ch = static_cast<char>(byte);
        }
        expect_reference(text);
    }
}

void writer_layout() {
    fmt::memory_buffer          out;
    gentest::runner::JsonWriter json(out);
    json.begin_object();
    json.text_field("a", "x");
    json.key("b");
    json.begin_array();
    json.integer(-12);
    json.boolean(true);
    json.begin_object();
    json.text_field("c", "line\n");
    json.end_object();
    json.end_array();
    json.key("d");
    json.begin_array();
    json.end_array();
    json.integer_field("e", 1794675398901);
    json.end_object();
    const std::string expected = R"({"a":"x","b":[-12,true,{"c":"line\n"}],"d":[],"e":1794675398901})";
    expect(fmt::to_string(out) == expected, "JsonWriter layout: " + fmt::to_string(out));
}

} // namespace

int main() {
    try {
        known_inputs();
        word_boundaries();
        exhaustive_short_inputs();
        random_corpus();
        writer_layout();
    } catch (const std::exception &error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
