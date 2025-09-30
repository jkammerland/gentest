#pragma once

#include <string>
#include <string_view>

namespace gentest {

/**
 * @brief Example class demonstrating library functionality
 * 
 * This class provides a simple example of a library interface.
 * It demonstrates proper C++ patterns including:
 * - Rule of Five (move/copy constructors and assignment operators)
 * - const correctness
 * - Modern C++ features (string_view, nodiscard)
 */
class example {
public:
    // Constructors
    example() = default;
    explicit example(std::string_view name);
    
    // Rule of Five
    example(const example&) = default;
    example(example&&) noexcept = default;
    auto operator=(const example&) -> example& = default;
    auto operator=(example&&) noexcept -> example& = default;
    ~example() = default;

    // Public interface
    [[nodiscard]] auto get_name() const noexcept -> std::string_view;
    [[nodiscard]] auto get_version() const noexcept -> std::string_view;
    
    auto set_name(std::string_view name) -> void;
    
    // Utility functions
    [[nodiscard]] auto format_info() const -> std::string;

private:
    std::string name_{"gentest"};
    static constexpr std::string_view version_{"1.0.0"};
};

// Free functions
[[nodiscard]] auto get_library_info() -> std::string;

} // namespace gentest