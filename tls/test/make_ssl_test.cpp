#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include "common/tls/make_ssl.h"

#include "ja4.h"

// Build the ClientHello bytes (the first TLS record) for the given profile.
static std::vector<uint8_t> build_client_hello(ag::tls::TlsClientProfile profile, const char *sni = "example.org") {
    static constexpr uint8_t H2_ALPN[] = {2, 'h', '2'};

    ag::tls::SslInitParameters params;
    params.profile = profile;
    params.protocol = ag::tls::SslProtocol::TLS;
    // The default `openssl s_client` sends no ALPN; the browser/OkHttp profiles do.
    if (profile != ag::tls::TlsClientProfile::OPENSSL_DEFAULT) {
        params.alpn_protos = {H2_ALPN, std::size(H2_ALPN)};
    }
    params.sni = sni;
    params.post_quantum = true;

    auto r = ag::tls::make_ssl(params);
    if (!std::holds_alternative<ag::tls::SslPtr>(r)) {
        ADD_FAILURE() << "make_ssl failed: " << std::get<std::string>(r);
        return {};
    }
    ag::tls::SslPtr ssl = std::move(std::get<ag::tls::SslPtr>(r));

    // Drive the handshake against an in-memory BIO and read out the ClientHello bytes.
    SSL_set0_wbio(ssl.get(), BIO_new(BIO_s_mem()));
    SSL_connect(ssl.get());
    std::vector<uint8_t> hello(UINT16_MAX);
    int n = BIO_read(SSL_get_wbio(ssl.get()), hello.data(), (int) hello.size());
    EXPECT_GT(n, 0);
    hello.resize(n > 0 ? (size_t) n : 0);
    return hello;
}

// Build a ClientHello for the given profile and return its JA4 fingerprint.
static std::string client_hello_ja4(ag::tls::TlsClientProfile profile) {
    std::vector<uint8_t> hello = build_client_hello(profile);
    if (hello.empty()) {
        return {};
    }
    return ag::ja4::compute({hello.data(), hello.size()}, false);
}

static bool is_grease_value(uint16_t v) {
    return (v & 0x0f0f) == 0x0a0a && ((v >> 8) & 0xff) == (v & 0xff);
}

// Returns the ClientHello extension types for |profile| in wire order, with
// GREASE (RFC 8701) and padding stripped. Unlike JA4 — which sorts the extension
// list before hashing — this preserves order, so it guards the fixed extension
// ordering that the Safari and Firefox profiles must reproduce.
static std::vector<uint16_t> client_hello_extensions(
        ag::tls::TlsClientProfile profile, const char *sni = "example.org") {
    std::vector<uint16_t> out;
    std::vector<uint8_t> h = build_client_hello(profile, sni);
    if (h.size() < 44) {
        return out;
    }
    auto u16 = [&](size_t o) { return (uint16_t) ((h[o] << 8) | h[o + 1]); };
    size_t p = 5 + 4; // TLS record header + handshake header
    p += 2 + 32;      // client_version + random
    p += 1 + h[p];    // session_id
    p += 2 + u16(p);  // cipher_suites
    p += 1 + h[p];    // compression_methods
    if (p + 2 > h.size()) {
        return out;
    }
    size_t ext_end = p + 2 + u16(p);
    p += 2;
    while (p + 4 <= ext_end && p + 4 <= h.size()) {
        uint16_t type = u16(p);
        uint16_t len = u16(p + 2);
        if (!is_grease_value(type) && type != TLSEXT_TYPE_padding) {
            out.push_back(type);
        }
        p += 4 + (size_t) len;
    }
    return out;
}

// Sanity-checks the make_ssl against the JA4 fingerprints captured from the reference
// wreq/browser implementations.
TEST(MakeSsl, Ja4Profiles) {
    struct Case {
        ag::tls::TlsClientProfile profile;
        const char *name;
        const char *expected;
    };
    const Case cases[] = {
            // Reference captured from Chrome stable (November 2026). The resumption `pre_shared_key`
            // extension is excluded, because the test ClientHello does not resume a session.
            {ag::tls::TlsClientProfile::CHROME, "Chrome", "t13d1517h2_8daaf6152771_cb7bf5808d99"},
            // Reference captured from Chrome Canary (November 2026): the same as Chrome stable.
            {ag::tls::TlsClientProfile::CHROME_CANARY, "ChromeCanary", "t13d1517h2_8daaf6152771_cb7bf5808d99"},
            {ag::tls::TlsClientProfile::SAFARI, "Safari26", "t13d2013h2_a09f3c656075_7f0f34a4126d"},
            {ag::tls::TlsClientProfile::FIREFOX, "Firefox151", "t13d1717h2_5b57614c22b0_3cbfd9057e0d"},
            {ag::tls::TlsClientProfile::OKHTTP, "OkHttp5", "t13d1613h2_46e7e9700bed_eca864cca44a"},
            // Reference captured from `openssl s_client -servername ... ` (OpenSSL 3.6.2).
            {ag::tls::TlsClientProfile::OPENSSL_DEFAULT, "OpenSSL3.6", "t13d301100_1d37bd780c83_8e6e362c5eac"},
            // Library-default ClientHello (no mimicry). Informational: tracks the BoringSSL version,
            // so it is printed but not asserted — a bump is expected to move it. The next move to
            // watch for is ML-DSA: boring-2026-05-08 can verify it but does not advertise it here
            // (see the note in make_ssl.cpp), so when a later BoringSSL adds the codepoints to its
            // default verify list, this fingerprint gains them without any change on our side.
            {ag::tls::TlsClientProfile::BORINGSSL_DEFAULT, "BoringSSLDefault", ""},
    };
    for (const auto &c : cases) {
        std::string ja4 = client_hello_ja4(c.profile);
        fprintf(stderr, "[JA4 %-8s] %s\n", c.name, ja4.c_str());
        EXPECT_FALSE(ja4.empty()) << c.name;
        if (c.expected[0] != '\0') {
            EXPECT_EQ(c.expected, ja4) << c.name << ": JA4 mismatch vs reference";
        }
    }
}

// Returns the body of the first ClientHello extension of |type| for |profile|, or nullopt if the
// extension is not sent.
static std::optional<std::vector<uint8_t>> client_hello_extension(ag::tls::TlsClientProfile profile, uint16_t type) {
    std::vector<uint8_t> h = build_client_hello(profile);
    if (h.size() < 44) {
        return std::nullopt;
    }
    auto u16 = [&](size_t o) {
        return (uint16_t) ((h[o] << 8) | h[o + 1]);
    };
    size_t p = 5 + 4; // TLS record header + handshake header
    p += 2 + 32;      // client_version + random
    p += 1 + h[p];    // session_id
    p += 2 + u16(p);  // cipher_suites
    p += 1 + h[p];    // compression_methods
    if (p + 2 > h.size()) {
        return std::nullopt;
    }
    size_t ext_end = std::min(p + 2 + u16(p), h.size());
    p += 2;
    while (p + 4 <= ext_end) {
        uint16_t len = u16(p + 2);
        if (u16(p) == type && p + 4 + len <= ext_end) {
            auto begin = h.begin() + (std::ptrdiff_t) (p + 4);
            return std::vector<uint8_t>(begin, begin + len);
        }
        p += 4 + (size_t) len;
    }
    return std::nullopt;
}

// Both Chrome profiles send `trust_anchors` with Chrome's ID list and a GREASE value at the head of
// signature_algorithms. No profile sends `server_padding`: Chrome Canary stopped sending it. The other
// profiles send none of these.
TEST(MakeSsl, ChromeExtensions) {
    // The whole `trust_anchors` extension body (16-bit list length, then 28 length-prefixed IDs),
    // copied verbatim from a captured Chrome ClientHello. It is kept as hex, separately from the
    // table in make_ssl.cpp, so that an error in either copy makes this test fail.
    static constexpr std::string_view TRUST_ANCHORS_HEX =
            "00b80582df1302010582df1302060582df13020d0582df13020e0582df13020f0582df1302120582"
            "df1302130582df13021408839a648c9b2d010708839a648c9b2d010808839a648c9b2d010908839a"
            "648c9b2d010a08839a648c9b2d010b08839a648c9b2d010c08839a648c9b2d010d08839a648c9b2d"
            "011208839a648c9b2d011304d679090104d679090404d679090504d679090604d679090704d67909"
            "0804d679090a04d679090b04d679090c04d679090d04d679090f";
    std::vector<uint8_t> trust_anchors_expected;
    for (size_t i = 0; i + 1 < TRUST_ANCHORS_HEX.size(); i += 2) {
        trust_anchors_expected.push_back(
                (uint8_t) std::stoul(std::string(TRUST_ANCHORS_HEX.substr(i, 2)), nullptr, 16));
    }
    ASSERT_EQ(2 + 184, trust_anchors_expected.size());

    for (auto profile : {ag::tls::TlsClientProfile::CHROME, ag::tls::TlsClientProfile::CHROME_CANARY}) {
        EXPECT_FALSE(client_hello_extension(profile, TLSEXT_TYPE_server_padding).has_value());

        auto anchors = client_hello_extension(profile, TLSEXT_TYPE_trust_anchors);
        ASSERT_TRUE(anchors.has_value());
        EXPECT_EQ(trust_anchors_expected, *anchors);

        auto sigalgs = client_hello_extension(profile, TLSEXT_TYPE_signature_algorithms);
        ASSERT_TRUE(sigalgs.has_value());
        ASSERT_GE(sigalgs->size(), 4);
        EXPECT_TRUE(is_grease_value((uint16_t) (((*sigalgs)[2] << 8) | (*sigalgs)[3])));
    }

    for (auto profile :
            {ag::tls::TlsClientProfile::SAFARI, ag::tls::TlsClientProfile::FIREFOX, ag::tls::TlsClientProfile::OKHTTP,
                    ag::tls::TlsClientProfile::OPENSSL_DEFAULT, ag::tls::TlsClientProfile::BORINGSSL_DEFAULT}) {
        EXPECT_FALSE(client_hello_extension(profile, TLSEXT_TYPE_trust_anchors).has_value());
        EXPECT_FALSE(client_hello_extension(profile, TLSEXT_TYPE_server_padding).has_value());
    }
}

// SNI is optional: a null host name must produce a working `SSL`
// object with no server_name extension, rather than an error or a crash.
TEST(MakeSsl, NoSni) {
    auto exts = client_hello_extensions(ag::tls::TlsClientProfile::CHROME, nullptr);
    EXPECT_FALSE(exts.empty());
    EXPECT_EQ(exts.end(), std::find(exts.begin(), exts.end(), TLSEXT_TYPE_server_name));

    // A regular host name still gets one.
    std::vector<uint16_t> exts_chrome = client_hello_extensions(ag::tls::TlsClientProfile::CHROME);
    EXPECT_NE(exts_chrome.end(), std::find(exts_chrome.begin(), exts_chrome.end(), TLSEXT_TYPE_server_name));
}

// The Safari and Firefox profiles must emit ClientHello extensions in a fixed,
// browser-authentic order (Chrome permutes instead, the rest keep BoringSSL's
// default order). JA4 sorts extensions before hashing and would not catch a
// regression here, so assert the raw wire order explicitly. Orders mirror the
// wreq/BoringSSL browser-imitation reference.
TEST(MakeSsl, ExtensionOrder) {
    const std::vector<uint16_t> safari_expected = {
            TLSEXT_TYPE_server_name,
            TLSEXT_TYPE_extended_master_secret,
            TLSEXT_TYPE_renegotiate,
            TLSEXT_TYPE_supported_groups,
            TLSEXT_TYPE_ec_point_formats,
            TLSEXT_TYPE_application_layer_protocol_negotiation,
            TLSEXT_TYPE_status_request,
            TLSEXT_TYPE_signature_algorithms,
            TLSEXT_TYPE_certificate_timestamp,
            TLSEXT_TYPE_key_share,
            TLSEXT_TYPE_psk_key_exchange_modes,
            TLSEXT_TYPE_supported_versions,
            TLSEXT_TYPE_cert_compression,
    };
    EXPECT_EQ(safari_expected, client_hello_extensions(ag::tls::TlsClientProfile::SAFARI));

    const std::vector<uint16_t> firefox_expected = {
            TLSEXT_TYPE_server_name,
            TLSEXT_TYPE_extended_master_secret,
            TLSEXT_TYPE_renegotiate,
            TLSEXT_TYPE_supported_groups,
            TLSEXT_TYPE_ec_point_formats,
            TLSEXT_TYPE_session_ticket,
            TLSEXT_TYPE_application_layer_protocol_negotiation,
            TLSEXT_TYPE_status_request,
            TLSEXT_TYPE_delegated_credential,
            TLSEXT_TYPE_certificate_timestamp,
            TLSEXT_TYPE_key_share,
            TLSEXT_TYPE_supported_versions,
            TLSEXT_TYPE_signature_algorithms,
            TLSEXT_TYPE_psk_key_exchange_modes,
            TLSEXT_TYPE_record_size_limit,
            TLSEXT_TYPE_cert_compression,
            TLSEXT_TYPE_encrypted_client_hello,
    };
    EXPECT_EQ(firefox_expected, client_hello_extensions(ag::tls::TlsClientProfile::FIREFOX));

    // OkHttp / Conscrypt (btls default / pristine-upstream BoringSSL order).
    const std::vector<uint16_t> okhttp_expected = {
            TLSEXT_TYPE_server_name,
            TLSEXT_TYPE_extended_master_secret,
            TLSEXT_TYPE_renegotiate,
            TLSEXT_TYPE_supported_groups,
            TLSEXT_TYPE_ec_point_formats,
            TLSEXT_TYPE_session_ticket,
            TLSEXT_TYPE_application_layer_protocol_negotiation,
            TLSEXT_TYPE_status_request,
            TLSEXT_TYPE_signature_algorithms,
            TLSEXT_TYPE_key_share,
            TLSEXT_TYPE_psk_key_exchange_modes,
            TLSEXT_TYPE_supported_versions,
    };
    EXPECT_EQ(okhttp_expected, client_hello_extensions(ag::tls::TlsClientProfile::OKHTTP));

    // `openssl s_client` order (captured from OpenSSL 3.6).
    const std::vector<uint16_t> openssl_expected = {
            TLSEXT_TYPE_renegotiate,
            TLSEXT_TYPE_server_name,
            TLSEXT_TYPE_ec_point_formats,
            TLSEXT_TYPE_supported_groups,
            TLSEXT_TYPE_session_ticket,
            TLSEXT_TYPE_encrypt_then_mac,
            TLSEXT_TYPE_extended_master_secret,
            TLSEXT_TYPE_signature_algorithms,
            TLSEXT_TYPE_supported_versions,
            TLSEXT_TYPE_psk_key_exchange_modes,
            TLSEXT_TYPE_key_share,
    };
    EXPECT_EQ(openssl_expected, client_hello_extensions(ag::tls::TlsClientProfile::OPENSSL_DEFAULT));
}
