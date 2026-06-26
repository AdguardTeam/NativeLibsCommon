#pragma once

#include <openssl/x509.h>

#include <string>

namespace ag::tls {

/**
 * Format detailed certificate diagnostic info.
 * For the leaf cert and each cert in the chain, outputs:
 * Subject, Issuer, SHA256 fingerprint, NotBefore, NotAfter.
 * Returns an empty string if cert is null.
 */
std::string get_cert_diagnostic_info(X509 *cert, STACK_OF(X509) * chain);

} // namespace ag::tls
