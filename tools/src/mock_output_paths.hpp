#pragma once

#include <filesystem>
#include <fmt/format.h>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>
#include <string>
#include <string_view>

namespace gentest::codegen {

[[nodiscard]] inline std::string sanitize_mock_domain_label(std::string value) {
    for (auto &ch : value) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            ch = '_';
        }
    }
    if (value.empty()) {
        return "domain";
    }
    return value;
}

[[nodiscard]] inline std::string zero_pad_mock_domain_index(std::size_t idx) { return fmt::format("{:04d}", static_cast<unsigned>(idx)); }

[[nodiscard]] inline std::string sha256_hex_prefix(std::string_view value, std::size_t hex_chars) {
    llvm::SHA256 sha256;
    sha256.update(llvm::StringRef(value.data(), value.size()));

    const auto  digest = sha256.final();
    std::string hex;
    hex.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        fmt::format_to(std::back_inserter(hex), "{:02x}", static_cast<unsigned>(byte));
    }

    if (hex_chars >= hex.size()) {
        return hex;
    }
    return hex.substr(0, hex_chars);
}

[[nodiscard]] inline std::string abbreviate_mock_domain_label(std::string value) {
    value = sanitize_mock_domain_label(std::move(value));
    if (value == "header" || value.size() <= 32) {
        return value;
    }
    return fmt::format("{}_{}", value.substr(0, 16), sha256_hex_prefix(value, 8));
}

[[nodiscard]] inline std::filesystem::path make_mock_domain_output_path(const std::filesystem::path &base, std::size_t idx,
                                                                        std::string_view label) {
    std::filesystem::path out  = base;
    const std::string     stem = base.stem().string();
    const std::string     ext  = base.extension().string();
    out.replace_filename(
        fmt::format("{}__domain_{}_{}{}", stem, zero_pad_mock_domain_index(idx), abbreviate_mock_domain_label(std::string(label)), ext));
    return out;
}

} // namespace gentest::codegen
