#pragma once

#include "model.hpp"
#include "scan_utils.hpp"

#include <filesystem>
#include <fmt/format.h>
#include <set>
#include <string>
#include <vector>

namespace gentest::codegen {

struct MockOutputDomainPlan {
    enum class Kind {
        Header,
        NamedModule,
    };

    Kind                  kind = Kind::Header;
    std::string           module_name;
    std::filesystem::path registry_path;
    std::filesystem::path impl_path;
};

[[nodiscard]] inline std::vector<MockOutputDomainPlan> describe_mock_output_domains(const CollectorOptions &options) {
    std::vector<MockOutputDomainPlan> domains;
    if (options.mock_registry_path.empty() && options.mock_impl_path.empty()) {
        return domains;
    }

    domains.push_back(MockOutputDomainPlan{
        .kind = MockOutputDomainPlan::Kind::Header,
    });

    if (options.mock_registry_path.empty() || options.mock_impl_path.empty()) {
        return domains;
    }

    std::set<std::string> seen_modules;
    std::size_t           idx = 1;
    for (const auto &source : options.sources) {
        std::optional<std::string> module_name;
        if (const auto it = options.module_interface_names_by_source.find(source); it != options.module_interface_names_by_source.end()) {
            module_name = it->second;
        } else {
            module_name = scan::named_module_name_from_source_file(std::filesystem::path(source));
        }
        if (!module_name.has_value() || !seen_modules.insert(*module_name).second) {
            continue;
        }

        domains.push_back(MockOutputDomainPlan{
            .kind        = MockOutputDomainPlan::Kind::NamedModule,
            .module_name = *module_name,
        });
        ++idx;
    }

    return domains;
}

[[nodiscard]] inline bool validate_mock_output_domains(const CollectorOptions &options, std::string &error) {
    const bool has_base_registry     = !options.mock_registry_path.empty();
    const bool has_base_impl         = !options.mock_impl_path.empty();
    const bool has_explicit_registry = !options.mock_domain_registry_outputs.empty();
    const bool has_explicit_impl     = !options.mock_domain_impl_outputs.empty();
    if (!has_explicit_registry && !has_explicit_impl) {
        if (has_base_registry && has_base_impl) {
            error = "mock outputs require explicit --mock-domain-registry-output/--mock-domain-impl-output paths";
            return false;
        }
        return true;
    }
    if (has_explicit_registry != has_explicit_impl) {
        error = "explicit mock domain outputs require matching --mock-domain-registry-output and --mock-domain-impl-output lists";
        return false;
    }
    if (!has_base_registry || !has_base_impl) {
        error = "explicit mock domain outputs require both --mock-registry and --mock-impl";
        return false;
    }

    const auto expected_domains = describe_mock_output_domains(options);
    if (options.mock_domain_registry_outputs.size() != expected_domains.size() ||
        options.mock_domain_impl_outputs.size() != expected_domains.size()) {
        error = fmt::format("expected {} --mock-domain-registry-output/--mock-domain-impl-output value(s) for discovered mock output "
                            "domains, got {} and {}",
                            expected_domains.size(), options.mock_domain_registry_outputs.size(), options.mock_domain_impl_outputs.size());
        return false;
    }
    return true;
}

[[nodiscard]] inline std::vector<MockOutputDomainPlan> build_mock_output_domains(const CollectorOptions &options) {
    auto domains = describe_mock_output_domains(options);
    for (std::size_t idx = 0; idx < domains.size(); ++idx) {
        auto &domain = domains[idx];
        if (idx < options.mock_domain_registry_outputs.size() && idx < options.mock_domain_impl_outputs.size()) {
            domain.registry_path = options.mock_domain_registry_outputs[idx];
            domain.impl_path     = options.mock_domain_impl_outputs[idx];
        }
    }
    return domains;
}

} // namespace gentest::codegen
