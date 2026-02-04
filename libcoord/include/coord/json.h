#pragma once

#include <string>

#include "coord/types.h"

namespace coord {

#if COORD_ENABLE_JSON
bool load_session_spec_json(const std::string &path, SessionSpec &out, std::string *error = nullptr);
bool write_manifest_json(const SessionManifest &manifest, const std::string &path, std::string *error = nullptr);
#else
inline bool load_session_spec_json(const std::string &, SessionSpec &, std::string *error = nullptr) {
    if (error) *error = "JSON support disabled";
    return false;
}
inline bool write_manifest_json(const SessionManifest &, const std::string &, std::string *error = nullptr) {
    if (error) *error = "JSON support disabled";
    return false;
}
#endif

} // namespace coord
