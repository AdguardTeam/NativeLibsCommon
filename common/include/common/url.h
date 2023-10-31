#pragma once

#include <string>
#include <string_view>

/** A collection of utilites for working with URLs. */
namespace ag::url {

/**
 * Normalize dots according to RFC 3986.
 * For example, `../a/b/../c/./d.html` becomes `/a/c/d.html`
 */
std::string normalize_path(std::string_view path);

} // namespace ag::url
