#pragma once

namespace fixture::validation {

struct MemberTemplateService {
    template <typename T> T transform(T value) { return value; }
};

} // namespace fixture::validation
