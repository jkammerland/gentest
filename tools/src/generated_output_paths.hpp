#pragma once

#include <cstddef>
#include <filesystem>
#include <fmt/format.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/MD5.h>
#include <string>
#include <string_view>

namespace gentest::codegen {

inline auto sanitize_and_shorten_generated_stem(std::string value) -> std::string {
    for (auto &ch : value) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            ch = '_';
        }
    }
    if (value.empty()) {
        value = "tu";
    }
    if (value.size() <= 24) {
        return value;
    }

    llvm::MD5 hasher;
    hasher.update(value);
    llvm::MD5::MD5Result digest;
    hasher.final(digest);

    llvm::SmallString<32> digest_hex;
    llvm::MD5::stringifyResult(digest, digest_hex);
    return fmt::format("{}_{}", value.substr(0, 16), std::string_view{digest_hex.data(), 8});
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline auto resolve_module_wrapper_output(const std::filesystem::path &output_dir, const std::filesystem::path &source_path,
                                          std::size_t idx) -> std::filesystem::path {
    std::filesystem::path out  = output_dir;
    const std::string     stem = sanitize_and_shorten_generated_stem(source_path.stem().string());
    const std::string     ext  = source_path.extension().string();
    out /= fmt::format("tu_{:04d}_{}.module.gentest{}", static_cast<unsigned>(idx), stem, ext);
    return out;
}

} // namespace gentest::codegen
