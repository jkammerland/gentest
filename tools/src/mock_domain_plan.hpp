#pragma once

#include "mock_output_paths.hpp"
#include "model.hpp"
#include "scan_utils.hpp"

#include <filesystem>
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

[[nodiscard]] inline std::vector<MockOutputDomainPlan> build_mock_output_domains(const CollectorOptions &options) {
    std::vector<MockOutputDomainPlan> domains;
    if (options.mock_registry_path.empty() && options.mock_impl_path.empty()) {
        return domains;
    }

    domains.push_back(MockOutputDomainPlan{
        .kind          = MockOutputDomainPlan::Kind::Header,
        .registry_path = options.mock_registry_path.empty() ? std::filesystem::path{}
                                                            : make_mock_domain_output_path(options.mock_registry_path, 0, "header"),
        .impl_path =
            options.mock_impl_path.empty() ? std::filesystem::path{} : make_mock_domain_output_path(options.mock_impl_path, 0, "header"),
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
            .kind          = MockOutputDomainPlan::Kind::NamedModule,
            .module_name   = *module_name,
            .registry_path = make_mock_domain_output_path(options.mock_registry_path, idx, *module_name),
            .impl_path     = make_mock_domain_output_path(options.mock_impl_path, idx, *module_name),
        });
        ++idx;
    }

    return domains;
}

} // namespace gentest::codegen
