#pragma once

namespace fixture::validation {

template <typename T> struct TemplateRepo {
    virtual ~TemplateRepo() = default;
    virtual T load(int key) = 0;
};

} // namespace fixture::validation
