#pragma once

namespace fixture::validation {

struct ConstRecordMember {
    int value;
};

struct ConstRecordCtorService {
    const ConstRecordMember member;

    virtual ~ConstRecordCtorService() = default;

    virtual int value() = 0;
};

} // namespace fixture::validation
