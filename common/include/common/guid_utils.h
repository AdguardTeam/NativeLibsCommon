#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <guiddef.h>

namespace ag {
/**
 * Convert GUID to string representation
 * @note only for usage on Windows platform
 * @param guid guid structure
 * @return string in format: "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
 */
std::string guid_to_string(const GUID &guid);

/**
 * Convert string representation of guid to guid structure
 * @note only for usage on Windows platform
 * @param guid_str string representation of guid
 * @param guid guid structure
 * @return true if successful, else false
 */
std::optional<GUID> string_to_guid(std::string_view guid_str);

/**
 * Return a new version 4 UUID.
 */
GUID random_guid();

} // namespace ag
