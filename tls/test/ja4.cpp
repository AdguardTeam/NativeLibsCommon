#include "ja4.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <openssl/sha.h>

#include "client_hello.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
using U8View = ag::Uint8View;

template <typename It>
static std::string to_hex_list(It begin, It end) {
    if (begin == end) {
        return {};
    }
    std::string ret;
    ret.reserve((end - begin) * 2);
    for (; begin != end; ++begin) {
        fmt::format_to(std::back_inserter(ret), "{:0>4x},", *begin);
    }
    ret.pop_back();
    return ret;
}

static std::string to_hex(U8View data) {
    std::string ret;
    ret.reserve(data.size() * 2);
    for (uint8_t d : data) {
        fmt::format_to(std::back_inserter(ret), "{:0>2x}", d);
    }
    return ret;
}

// https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md
static std::string to_ja4(ag::tls::test::ClientHello &hello, bool quic) {
    // Remove GREASE values from ciphers, extension types, supported versions and signature algorithms.
    for (std::vector<uint16_t> *v :
            {&hello.cipher_suites, &hello.extensions, &hello.signature_algorithms, &hello.supported_versions}) {
        // clang-format off
        v->erase(std::remove_if(v->begin(), v->end(), [](uint16_t value) { return ag::tls::test::is_grease_value(value); }), v->end());
        // clang-format on
    }

    // Sort ciphers and extension types. Signature algorithms remain in original order.
    std::sort(hello.cipher_suites.begin(), hello.cipher_suites.end());
    std::sort(hello.extensions.begin(), hello.extensions.end());

    std::string ja4;
    ja4.reserve(36);

    ja4 += quic ? 'q' : 't';

    uint16_t version = hello.hello_version;
    if (!hello.supported_versions.empty()) {
        version = *std::max_element(hello.supported_versions.begin(), hello.supported_versions.end());
    }
    // clang-format off
    switch (version) {
        case 0x0304: ja4 += "13"; break;
        case 0x0303: ja4 += "12"; break;
        case 0x0302: ja4 += "11"; break;
        case 0x0301: ja4 += "10"; break;
        case 0x0300: ja4 += "s3"; break;
        case 0x0200: ja4 += "s2"; break;
        case 0x0100: ja4 += "s1"; break;
        default: ja4 += "00"; break;
    }
    // clang-format on

    ja4 += hello.sni_present ? 'd' : 'i';
    fmt::format_to(std::back_inserter(ja4), "{:0>2}", std::min((size_t) 99, hello.cipher_suites.size()));
    fmt::format_to(std::back_inserter(ja4), "{:0>2}", std::min((size_t) 99, hello.extensions.size()));

    if (!hello.alpn_protocols.empty() && !hello.alpn_protocols.front().empty()) {
        ja4 += hello.alpn_protocols.front().front();
        ja4 += hello.alpn_protocols.front().back();
    } else {
        ja4 += "00";
    }

    uint8_t sha_output[SHA256_DIGEST_LENGTH]{};

    ja4 += '_';

    std::string sha_input = to_hex_list(hello.cipher_suites.begin(), hello.cipher_suites.end());
    SHA256((uint8_t *) sha_input.data(), sha_input.size(), sha_output);
    std::string sha_output_hex = to_hex({sha_output, sizeof(sha_output)});
    sha_output_hex.resize(12);
    ja4 += sha_output_hex;

    ja4 += '_';

    hello.extensions.erase(std::remove_if(hello.extensions.begin(), hello.extensions.end(),
                                   [](uint16_t ext) {
                                       return ext == ag::tls::test::EXT_SERVER_NAME || ext == ag::tls::test::EXT_ALPN;
                                   }),
            hello.extensions.end());

    sha_input = to_hex_list(hello.extensions.begin(), hello.extensions.end());
    if (!hello.signature_algorithms.empty()) {
        sha_input += '_';
        sha_input += to_hex_list(hello.signature_algorithms.begin(), hello.signature_algorithms.end());
    }
    SHA256((uint8_t *) sha_input.data(), sha_input.size(), sha_output);
    sha_output_hex = to_hex({sha_output, sizeof(sha_output)});
    sha_output_hex.resize(12);
    ja4 += sha_output_hex;

    return ja4;
}

std::string ag::ja4::compute(ag::Uint8View data, bool quic) {
    std::optional<ag::tls::test::ClientHello> hello = ag::tls::test::parse_client_hello(data, quic);
    if (!hello.has_value()) {
        return "";
    }
    return to_ja4(*hello, quic);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
