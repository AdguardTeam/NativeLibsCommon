#pragma once

#include <openssl/x509.h>

#include <string>

namespace ag::tls {

/**
 * Format certificate diagnostic info (Subject, Issuer, Chain length).
 * Returns an empty string if cert is null.
 */
std::string get_cert_diagnostic_info(X509 *cert, STACK_OF(X509) *chain);

} // namespace ag::tls
