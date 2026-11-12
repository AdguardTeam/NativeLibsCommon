#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/defs.h"

namespace ag::tls::test {

/** The server_name extension codepoint. */
constexpr uint16_t EXT_SERVER_NAME = 0;

/** The supported_groups extension codepoint. */
constexpr uint16_t EXT_SUPPORTED_GROUPS = 10;

/** The signature_algorithms extension codepoint. */
constexpr uint16_t EXT_SIGNATURE_ALGORITHMS = 13;

/** The application_layer_protocol_negotiation extension codepoint. */
constexpr uint16_t EXT_ALPN = 16;

/** The supported_versions extension codepoint. */
constexpr uint16_t EXT_SUPPORTED_VERSIONS = 43;

/** The key_share extension codepoint. */
constexpr uint16_t EXT_KEY_SHARE = 51;

/** One TLS extension, with its body. */
struct Extension {
    uint16_t type = 0;
    std::vector<uint8_t> data;
};

/** The ClientHello fields that the JA4 and profile tests use. */
struct ClientHello {
    /** Whether the ClientHello has a server_name extension. */
    bool sni_present = false;
    /** The first host_name in the server_name extension, or an empty string. */
    std::string sni;
    /** The legacy version from the ClientHello body. */
    uint16_t hello_version = 0;
    /** The supported_versions list, in wire order. GREASE values are not removed. */
    std::vector<uint16_t> supported_versions;
    /** The cipher suite list, in wire order. GREASE values are not removed. */
    std::vector<uint16_t> cipher_suites;
    /** The extension type list, in wire order. GREASE and padding are not removed. */
    std::vector<uint16_t> extensions;
    /** The extension list with bodies, in wire order. */
    std::vector<Extension> extension_details;
    /** The signature_algorithms list, in wire order. GREASE values are not removed. */
    std::vector<uint16_t> signature_algorithms;
    /** The ALPN protocol list, in wire order. */
    std::vector<std::string> alpn_protocols;
    /** The supported_groups list, in wire order. GREASE values are not removed. */
    std::vector<uint16_t> supported_groups;
    /** The key_share group list, in wire order. GREASE values are not removed. */
    std::vector<uint16_t> key_share_groups;
};

/**
 * Parse a ClientHello.
 * @param data The first TLS record in case of TCP, or the first TLS handshake message in case of QUIC.
 * @param quic Whether `data` is a QUIC handshake message.
 * @return The parsed ClientHello, or `std::nullopt` on an error or truncated input.
 */
std::optional<ClientHello> parse_client_hello(ag::Uint8View data, bool quic);

/** Return true if the value is a GREASE value (RFC 8701). */
bool is_grease_value(uint16_t value);

/** Return the values without GREASE values, preserving their order. */
std::vector<uint16_t> without_grease(const std::vector<uint16_t> &values);

} // namespace ag::tls::test
