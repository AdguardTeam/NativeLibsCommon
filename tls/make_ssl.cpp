#include "common/tls/make_ssl.h"

#include <algorithm>
#include <vector>

#include <brotli/decode.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "common/socket_address.h"

#ifdef OPENSSL_IS_BORINGSSL
#include <ngtcp2/ngtcp2_crypto_boringssl.h>
#else
#include <ngtcp2/ngtcp2_crypto_quictls.h>
#endif

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace ag::tls {

namespace {

bool is_browser_profile(TlsClientProfile p) {
    return p == TlsClientProfile::CHROME || p == TlsClientProfile::SAFARI || p == TlsClientProfile::FIREFOX;
}

#ifdef OPENSSL_IS_BORINGSSL

// Per-profile TLS 1.2 cipher list. The TLS 1.3 suites are always offered by BoringSSL, so only the
// legacy TLS 1.2 suites are listed here. Order is JA4-irrelevant.
const char *cipher_list_for(TlsClientProfile profile) {
    // clang-format off
    static const char *const CHROME =
            "ALL:!aPSK:!ECDSA+SHA1:!3DES";
    static const char *const SAFARI =
            "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-CHACHA20-POLY1305:"
            "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-CHACHA20-POLY1305:"
            "ECDHE-ECDSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA:ECDHE-RSA-AES128-SHA:"
            "AES256-GCM-SHA384:AES128-GCM-SHA256:AES256-SHA:AES128-SHA:"
            "ECDHE-ECDSA-DES-CBC3-SHA:ECDHE-RSA-DES-CBC3-SHA:DES-CBC3-SHA";
    static const char *const FIREFOX =
            "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:"
            "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
            "ECDHE-ECDSA-AES128-SHA:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:"
            "AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA:AES256-SHA";
    // OkHttp / Conscrypt default suite set.
    static const char *const OKHTTP =
            "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:"
            "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
            "ECDHE-RSA-AES128-SHA:ECDHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:"
            "AES128-SHA:AES256-SHA:DES-CBC3-SHA";
    // clang-format on
    switch (profile) {
    case TlsClientProfile::SAFARI:
        return SAFARI;
    case TlsClientProfile::FIREFOX:
        return FIREFOX;
    case TlsClientProfile::OKHTTP:
        return OKHTTP;
    case TlsClientProfile::CHROME:
    case TlsClientProfile::DEFAULT:
        break;
    }
    return CHROME;
}

// Per-profile supported groups.
std::pair<const uint16_t *, size_t> groups_for(TlsClientProfile profile) {
    static constexpr uint16_t CHROME[] = {
            SSL_GROUP_X25519_MLKEM768, SSL_GROUP_X25519, SSL_GROUP_SECP256R1, SSL_GROUP_SECP384R1};
    static constexpr uint16_t SAFARI[] = {
            SSL_GROUP_X25519_MLKEM768, SSL_GROUP_X25519, SSL_GROUP_SECP256R1, SSL_GROUP_SECP384R1,
            SSL_GROUP_SECP521R1};
    static constexpr uint16_t FIREFOX[] = {
            SSL_GROUP_X25519_MLKEM768, SSL_GROUP_X25519, SSL_GROUP_SECP256R1, SSL_GROUP_SECP384R1,
            SSL_GROUP_SECP521R1, SSL_GROUP_FFDHE2048, SSL_GROUP_FFDHE3072};
    static constexpr uint16_t OKHTTP[] = {SSL_GROUP_X25519, SSL_GROUP_SECP256R1, SSL_GROUP_SECP384R1};
    switch (profile) {
    case TlsClientProfile::SAFARI:
        return {SAFARI, std::size(SAFARI)};
    case TlsClientProfile::FIREFOX:
        return {FIREFOX, std::size(FIREFOX)};
    case TlsClientProfile::OKHTTP:
        return {OKHTTP, std::size(OKHTTP)};
    case TlsClientProfile::CHROME:
    case TlsClientProfile::DEFAULT:
        break;
    }
    return {CHROME, std::size(CHROME)};
}

// Per-profile signature algorithms. Order is significant for JA4 (kept verbatim).
std::pair<const uint16_t *, size_t> sigalgs_for(TlsClientProfile profile) {
    static constexpr uint16_t CHROME[] = {SSL_SIGN_ECDSA_SECP256R1_SHA256, SSL_SIGN_RSA_PSS_RSAE_SHA256,
            SSL_SIGN_RSA_PKCS1_SHA256, SSL_SIGN_ECDSA_SECP384R1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA384,
            SSL_SIGN_RSA_PKCS1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA512, SSL_SIGN_RSA_PKCS1_SHA512};
    // Safari repeats rsa_pss_rsae_sha384 verbatim.
    static constexpr uint16_t SAFARI[] = {SSL_SIGN_ECDSA_SECP256R1_SHA256, SSL_SIGN_RSA_PSS_RSAE_SHA256,
            SSL_SIGN_RSA_PKCS1_SHA256, SSL_SIGN_ECDSA_SECP384R1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA384,
            SSL_SIGN_RSA_PSS_RSAE_SHA384, SSL_SIGN_RSA_PKCS1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA512,
            SSL_SIGN_RSA_PKCS1_SHA512, SSL_SIGN_RSA_PKCS1_SHA1};
    static constexpr uint16_t FIREFOX[] = {SSL_SIGN_ECDSA_SECP256R1_SHA256, SSL_SIGN_ECDSA_SECP384R1_SHA384,
            SSL_SIGN_ECDSA_SECP521R1_SHA512, SSL_SIGN_RSA_PSS_RSAE_SHA256, SSL_SIGN_RSA_PSS_RSAE_SHA384,
            SSL_SIGN_RSA_PSS_RSAE_SHA512, SSL_SIGN_RSA_PKCS1_SHA256, SSL_SIGN_RSA_PKCS1_SHA384,
            SSL_SIGN_RSA_PKCS1_SHA512, SSL_SIGN_ECDSA_SHA1, SSL_SIGN_RSA_PKCS1_SHA1};
    static constexpr uint16_t OKHTTP[] = {SSL_SIGN_ECDSA_SECP256R1_SHA256, SSL_SIGN_RSA_PSS_RSAE_SHA256,
            SSL_SIGN_RSA_PKCS1_SHA256, SSL_SIGN_ECDSA_SECP384R1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA384,
            SSL_SIGN_RSA_PKCS1_SHA384, SSL_SIGN_RSA_PSS_RSAE_SHA512, SSL_SIGN_RSA_PKCS1_SHA512,
            SSL_SIGN_RSA_PKCS1_SHA1};
    switch (profile) {
    case TlsClientProfile::SAFARI:
        return {SAFARI, std::size(SAFARI)};
    case TlsClientProfile::FIREFOX:
        return {FIREFOX, std::size(FIREFOX)};
    case TlsClientProfile::OKHTTP:
        return {OKHTTP, std::size(OKHTTP)};
    case TlsClientProfile::CHROME:
    case TlsClientProfile::DEFAULT:
        break;
    }
    return {CHROME, std::size(CHROME)};
}

int DecompressBrotliCert(
        SSL *ssl, CRYPTO_BUFFER **out, size_t uncompressed_len, const uint8_t *in, size_t in_len) {
    uint8_t *data;
    CRYPTO_BUFFER *decompressed = CRYPTO_BUFFER_alloc(&data, uncompressed_len);
    if (!decompressed) {
        return 0;
    }

    size_t output_size = uncompressed_len;
    if (BROTLI_DECODER_RESULT_SUCCESS != BrotliDecoderDecompress(in_len, in, &output_size, data)
            || output_size != uncompressed_len) {
        CRYPTO_BUFFER_free(decompressed);
        return 0;
    }

    *out = decompressed;
    return 1;
}

#endif // OPENSSL_IS_BORINGSSL

} // namespace

std::variant<SslPtr, std::string> make_ssl(const SslInitParameters &params) {
    TlsClientProfile profile = params.profile;
    bool quic = params.protocol == SslProtocol::NGTCP2;
    bool browser = is_browser_profile(profile);

    UniquePtr<SSL_CTX, &SSL_CTX_free> ctx{SSL_CTX_new(TLS_client_method())};
    if (ctx == nullptr) {
        return "Failed to create SSL_CTX";
    }

    if (params.verify_callback != nullptr && params.verify_arg != nullptr) {
        SSL_CTX_set_verify(ctx.get(), SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_cert_verify_callback(ctx.get(), params.verify_callback, params.verify_arg);
    }
    if (0 != SSL_CTX_set_alpn_protos(ctx.get(), params.alpn_protos.data(), params.alpn_protos.size())) {
        return "Failed to set ALPN protocols";
    }

    // Session resumption persistence is owned by the caller.
    if (params.new_session_cb != nullptr) {
        SSL_CTX_set_session_cache_mode(ctx.get(), SSL_SESS_CACHE_CLIENT);
        SSL_CTX_sess_set_new_cb(ctx.get(), params.new_session_cb);
    }

// Mimic a browser's ClientHello if we are using BoringSSL.
#ifdef OPENSSL_IS_BORINGSSL
    if (browser) {
        if (SSL_CTX_add_cert_compression_alg(ctx.get(), TLSEXT_cert_compression_brotli, nullptr,
                    DecompressBrotliCert)
                != 1) {
            return "Failed to add certificate compression algorithm";
        }
    }

    // Chrome permutes ClientHello extensions; the other profiles keep a fixed order.
    SSL_CTX_set_permute_extensions(ctx.get(), profile == TlsClientProfile::CHROME);

    // GREASE is sent by the browser profiles.
    if (!quic && browser) {
        SSL_CTX_set_grease_enabled(ctx.get(), 1);
    }

    if (quic) {
        if (0 != ngtcp2_crypto_boringssl_configure_client_context(ctx.get())) {
            return "Couldn't configure SSL object for QUIC";
        }
    }
#endif // OPENSSL_IS_BORINGSSL

    SslPtr ssl{SSL_new(ctx.get())};
    if (ssl == nullptr) {
        return "Failed to create SSL";
    }
    if (!SocketAddress{params.sni}.valid()) {
        if (0 == SSL_set_tlsext_host_name(ssl.get(), params.sni)) {
            return "Failed to set SNI";
        }
    }

#ifdef SSL_set_user_data
    if (!params.endpoint_data.empty()) {
        SSL_set_user_data(ssl.get(), params.endpoint_data.data(), params.endpoint_data.size());
    }
#endif

#ifdef SSL_set_custom_client_random
    if (!params.tls_client_random.empty()) {
        std::vector<uint8_t> client_random_data(
                params.tls_client_random.begin(), params.tls_client_random.end());
        std::vector<uint8_t> mask_data(client_random_data.size(), 0xff);

        const size_t mask_size = std::min<size_t>(params.tls_client_random_mask.size(), mask_data.size());
        if (mask_size > 0) {
            std::copy_n(params.tls_client_random_mask.data(), mask_size, mask_data.begin());
        }

        // Generate random bytes for the parts not covered by the mask.
        std::vector<uint8_t> rand_bytes(mask_size);
        if (mask_size > 0 && 1 != RAND_bytes(rand_bytes.data(), mask_size)) {
            return "Failed to generate random bytes for SSL";
        }
        for (size_t i = 0; i < mask_size; ++i) {
            client_random_data[i] = (client_random_data[i] & mask_data[i]) | (rand_bytes[i] & ~mask_data[i]);
        }

        SSL_set_custom_client_random(ssl.get(), client_random_data.data(), client_random_data.size());
    }
#endif

// Mimic a browser's ClientHello if we are using BoringSSL.
#ifdef OPENSSL_IS_BORINGSSL
    if (profile != TlsClientProfile::DEFAULT) {
        // ECH GREASE: sent by Chrome and Firefox.
        if (profile == TlsClientProfile::CHROME || profile == TlsClientProfile::FIREFOX) {
            SSL_set_enable_ech_grease(ssl.get(), 1);
        }

        // ALPS (application settings): only Chrome sends it.
        if (profile == TlsClientProfile::CHROME) {
            const char *alps = quic ? "h3" : "h2";
            if (1 != SSL_add_application_settings(ssl.get(), (const uint8_t *) alps, 2, nullptr, 0)) {
                return "Failed to add ALPS extension";
            }
            SSL_set_alps_use_new_codepoint(ssl.get(), 1);
        }

        // Safari does not advertise the session_ticket extension.
        if (profile == TlsClientProfile::SAFARI) {
            SSL_set_options(ssl.get(), SSL_OP_NO_TICKET);
        }

        if (!SSL_set_strict_cipher_list(ssl.get(), cipher_list_for(profile))) {
            return "Failed to set strict cipher list";
        }

        if (!SSL_set_min_proto_version(ssl.get(), TLS1_2_VERSION)
                || !SSL_set_max_proto_version(ssl.get(), TLS1_3_VERSION)) {
            return "Failed to set SSL versions";
        }

        // The post-quantum toggle only gates the default Chrome group list; the other profiles
        // always advertise their fixed group list.
        if (profile != TlsClientProfile::CHROME || params.post_quantum) {
            auto [groups, groups_len] = groups_for(profile);
            if (!SSL_set1_group_ids(ssl.get(), groups, groups_len)) {
                return "Failed to set groups";
            }
        }

        // Firefox-specific extensions: record_size_limit (RFC 8449) and delegated_credentials (RFC 9345).
        if (profile == TlsClientProfile::FIREFOX) {
            SSL_set_record_size_limit(ssl.get(), 0x4001);
            static constexpr uint16_t FIREFOX_DC_SIGALGS[] = {SSL_SIGN_ECDSA_SECP256R1_SHA256,
                    SSL_SIGN_ECDSA_SECP384R1_SHA384, SSL_SIGN_ECDSA_SECP521R1_SHA512, SSL_SIGN_ECDSA_SHA1};
            SSL_set_delegated_credentials(ssl.get(), FIREFOX_DC_SIGALGS, std::size(FIREFOX_DC_SIGALGS));
        }

        if (!quic) {
            // signed_certificate_timestamp: sent by the browser profiles, not by OkHttp.
            if (browser) {
                SSL_enable_signed_cert_timestamps(ssl.get());
            }
            if (SSL_set_tlsext_status_type(ssl.get(), TLSEXT_STATUSTYPE_ocsp) != 1) {
                return "Failed to set OCSP status extension";
            }
            auto [sigalgs, sigalgs_len] = sigalgs_for(profile);
            if (!SSL_set_verify_algorithm_prefs(ssl.get(), sigalgs, sigalgs_len)) {
                return "Failed to set signature algorithms";
            }
        }
    }
#endif // OPENSSL_IS_BORINGSSL

    SSL_set_connect_state(ssl.get());

    if (params.resume_session != nullptr) {
        SSL_set_session(ssl.get(), params.resume_session); // Callee up-refs the session.
    }

    return ssl;
}

} // namespace ag::tls
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
