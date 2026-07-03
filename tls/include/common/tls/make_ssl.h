#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include <openssl/ssl.h>

#include "common/defs.h"

namespace ag::tls {

using SslPtr = UniquePtr<SSL, &SSL_free>;

/**
 * TLS ClientHello fingerprint profile.
 *
 * `CHROME`, `SAFARI` and `FIREFOX` reproduce the respective browser ClientHello (matching JA4);
 * `OKHTTP` reproduces the OkHttp Android HTTP client.
 * `DEFAULT` applies no mimicry and emits the plain BoringSSL/OpenSSL library-default ClientHello.
 */
enum class TlsClientProfile {
    CHROME,  ///< Chrome-like ClientHello (default)
    SAFARI,  ///< Safari-like ClientHello
    FIREFOX, ///< Firefox-like ClientHello
    OKHTTP,  ///< OkHttp (Android) client
    DEFAULT, ///< No mimicry — library defaults
};

/** How the produced `SSL` object will be used; affects QUIC-specific configuration. */
enum class SslProtocol {
    TLS,    ///< Plain TLS over TCP
    NGTCP2, ///< The `SSL` object will be used with ngtcp2
};

struct SslInitParameters {
    TlsClientProfile profile = TlsClientProfile::CHROME;
    SslProtocol protocol = SslProtocol::TLS;

    /** ALPN protocol list in wire format (each entry length-prefixed). */
    Uint8View alpn_protos;
    /** SNI host name. */
    const char *sni = nullptr;

    /** Server certificate verification callback. */
    int (*verify_callback)(X509_STORE_CTX *store_ctx, void *arg) = nullptr;
    void *verify_arg = nullptr;

    /**
     * Gates the post-quantum key-share group for the `CHROME` profile only.
     * Other profiles advertise their fixed group list regardless of this flag.
     */
    bool post_quantum = true;

    /** Custom ClientRandom. */
    Uint8View tls_client_random;
    /** Mask selecting which ClientRandom bits are fixed vs randomized. */
    Uint8View tls_client_random_mask;
    /** Opaque per-endpoint data. */
    Uint8View endpoint_data;

    /**
     * Session resumption. The caller owns persistence:
     * - `new_session_cb`, if set, is installed via `SSL_CTX_sess_set_new_cb` and enables the client
     *   session cache mode;
     * - `resume_session`, if set, is applied via `SSL_set_session`.
     */
    int (*new_session_cb)(SSL *ssl, SSL_SESSION *session) = nullptr;
    SSL_SESSION *resume_session = nullptr;
};

/**
 * Create a client `SSL` object whose ClientHello matches the requested fingerprint profile.
 * @return the `SSL` object on success, or an error string on failure.
 */
std::variant<SslPtr, std::string> make_ssl(const SslInitParameters &params);

} // namespace ag::tls
