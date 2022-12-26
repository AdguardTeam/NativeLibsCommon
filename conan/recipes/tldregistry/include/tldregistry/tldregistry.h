#include <optional>
#include <string_view>

namespace tldregistry {

/**
 * Return `true` if `domain` is a registered top-level domain, or `false` otherwise.
 * `domain` must not contain any leading or trailing dots.
 */
bool is_tld(std::string_view domain);

/**
 * Return a substring of `domain` that is the registered top-level domain, without the leading dot,
 * or std::nullopt if `domain` does not belong to a registered top-level domain.
 * `domain` must not contain any leading or trailing dots.
 */
std::optional<std::string_view> get_tld(std::string_view domain);

/**
 * Return a substring of `domain`, excluding any leading or trailing dots,
 * that is the registered top-level domain, plus at most `n` labels.
 * If `domain` does not belong to a registered top-level domain, return
 * at most `n + 1` top-level labels.
 * @return
 */
std::string_view reduce_domain(std::string_view domain, unsigned int n);

} // namespace tldregistry
