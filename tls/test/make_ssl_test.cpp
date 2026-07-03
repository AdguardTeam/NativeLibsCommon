#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include "common/tls/make_ssl.h"

#include "ja4.h"

// Build a ClientHello for the given profile and return its JA4 fingerprint.
static std::string client_hello_ja4(ag::tls::TlsClientProfile profile) {
    static constexpr uint8_t H2_ALPN[] = {2, 'h', '2'};

    ag::tls::SslInitParameters params;
    params.profile = profile;
    params.protocol = ag::tls::SslProtocol::TLS;
    params.alpn_protos = {H2_ALPN, std::size(H2_ALPN)};
    params.sni = "example.org";
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
    return ag::ja4::compute({hello.data(), hello.size()}, false);
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
            {ag::tls::TlsClientProfile::CHROME, "Chrome", "t13d1516h2_8daaf6152771_d8a2da3f94cd"},
            {ag::tls::TlsClientProfile::SAFARI, "Safari", "t13d2013h2_a09f3c656075_7f0f34a4126d"},
            {ag::tls::TlsClientProfile::FIREFOX, "Firefox", "t13d1717h2_5b57614c22b0_3cbfd9057e0d"},
            {ag::tls::TlsClientProfile::OKHTTP, "OkHttp", "t13d1613h2_46e7e9700bed_eca864cca44a"},
            // Library-default ClientHello (no mimicry). Informational: tracks the BoringSSL version.
            {ag::tls::TlsClientProfile::DEFAULT, "Default", ""},
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
