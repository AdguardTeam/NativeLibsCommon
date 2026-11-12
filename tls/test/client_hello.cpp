#include "client_hello.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCRYPT
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#endif

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
namespace ag::tls::test {

enum TlsReaderState {
    I_REC,
    I_HSHAKE,
    I_CLIHEL,
    I_CLIHEL_EXTS,
    I_CLIHEL_EXT,
    I_CERTS,
};

using U8View = ag::Uint8View;

static uint32_t ntoh_24(uint32_t x) {
    const auto *b = (uint8_t *) &x;
    return (b[0] << 16) | (b[1] << 8) | b[2];
}

enum TlsParseResult {
    TLS_RERR = 1,
    TLS_RMORE,
    TLS_RDONE,
};

struct Parser {
    TlsReaderState state = I_REC;
    U8View in;
    U8View rec;
    U8View buf;
    ClientHello hello;
};

enum RecType {
    CT_HANDSHAKE = 22,
};

enum HshakeType {
    HS_CLIENT_HELLO = 1,
    HS_SERVER_HELLO = 2,
    HS_CERTIFICATE = 11,
    HS_SERVER_KEY_EXCHANGE = 12,
    HS_CERTIFICATE_REQUEST = 13,
    HS_SERVER_HELLO_DONE = 14,
};

#pragma pack(push, 1)

struct Rec {
    uint8_t type; // enum rec_type_t
    uint16_t ver; // 3,1 - TLSv1.0
    uint16_t len;
    uint8_t data[0];
};

struct Hshake {
    uint8_t type; // enum hshake_type_t
    uint8_t len[3];
    uint8_t data[0];
};

struct SessId {
    uint8_t len; // 0..32
    uint8_t data[0];
};

struct ClientHelloHead {
    uint16_t ver;
    uint8_t random[32];
    SessId session_id;
    // cipher_suites; 2-byte length + data
    // compression_methods; 2-byte length + data
    // exts; 2-byte length + data
};

struct Ext {
    uint16_t type;
    uint16_t len;
    uint8_t data[0];
};

#pragma pack(pop)

static int datalen8(const uint8_t *d, const uint8_t *end) {
    if (1 > end - d) {
        return -1;
    }

    int n = d[0];
    if (d + 1 + n > end) {
        return -1;
    }

    return n;
}

static int read_u16(const uint8_t *d, const uint8_t *end) {
    if (d + 2 > end) {
        return -1;
    }
    uint16_t x = 0;
    std::memcpy(&x, d, 2);
    return ntohs(x);
}

static int datalen16(const uint8_t *d, const uint8_t *end) {
    int n = read_u16(d, end);
    if (n < 0) {
        return -1;
    }
    if (d + 2 + n > end) {
        return -1;
    }
    return n;
}

/**
Return enum rec_type_t;  <=0 on error. */
static int rec_parse(Parser *parser, U8View data) {
    const auto *rec = (Rec *) data.data();
    if (data.size() >= 2 && rec->type != CT_HANDSHAKE) {
        return -1;
    }

    if (sizeof(Rec) > data.size()) {
        return 0;
    }

    int n = ntohs(rec->len);
    if (sizeof(Rec) + n > data.size()) {
        return 0;
    }

    int ver = ntohs(rec->ver);
    if (ver < 0x0301) {
        return -1;
    }

    parser->rec = {rec->data, size_t(n)};
    parser->in.remove_prefix(sizeof(Rec) + parser->rec.size());
    return rec->type;
}

/**
Return enum hshake_type_t;  <=0 on error. */
static int hshake_parse(Parser *parser, U8View data) {
    if (sizeof(Hshake) > data.size()) {
        return 0;
    }

    const auto *h = (Hshake *) data.data();
    uint32_t x = 0;
    static_assert(sizeof(std::declval<decltype(h)>()->len) == 3);
    std::memcpy(&x, (void *) h->len, 3);
    uint32_t n = ntoh_24(x);
    // The body starts at offset sizeof(Hshake), so the declared length must fit after the header.
    if (n > data.size() - sizeof(Hshake)) {
        return 0;
    }

    parser->rec.remove_prefix(sizeof(Hshake) + n);
    parser->buf = {h->data, size_t(n)};
    return h->type;
}

/**
Return 1 on success;  <=0 on error. */
static int hello_parse(Parser *parser, U8View data) {
    if (sizeof(ClientHelloHead) > data.size()) {
        return 0;
    }

    const auto *c = (ClientHelloHead *) data.data();

    parser->hello.hello_version = ntohs(c->ver);

    const uint8_t *end = data.data() + data.size();
    if (c->session_id.len > end - c->session_id.data) {
        return 0;
    }

    const uint8_t *d = c->session_id.data + c->session_id.len;

    // cipher_suite[]
    int size = datalen16(d, end);
    if (size < 0) {
        return 0;
    }

    d += 2;

    const uint8_t *ciphers_end = d + size;
    while (d != ciphers_end) {
        int cipher = read_u16(d, ciphers_end);
        if (cipher < 0) {
            return 0;
        }
        parser->hello.cipher_suites.emplace_back((uint16_t) cipher);
        d += 2;
    }

    // comp_meth[]
    size = datalen8(d, end);
    if (size < 0) {
        return 0;
    }

    d += 1 + size;

    parser->buf = {d, size_t(end - d)};
    return 1;
}

/** Return TLS_RDONE; 0 on error. */
static int ext_servname_parse(Parser *parser, const uint8_t *data, size_t len) {
    const uint8_t *end = data + len;
    int size = datalen16(data, end);
    if (size < 0) {
        return 0;
    }

    parser->hello.sni_present = true;

    data += 2;
    const uint8_t *names_end = data + size;
    while (data != names_end) {
        if (names_end - data < 3) {
            return 0;
        }
        uint8_t name_type = data[0];
        int name_length = read_u16(data + 1, names_end);
        if (name_length < 0 || data + 3 + name_length > names_end) {
            return 0;
        }
        if (name_type == 0 && parser->hello.sni.empty()) {
            parser->hello.sni.assign((const char *) data + 3, (size_t) name_length);
        }
        data += 3 + name_length;
    }

    return TLS_RDONE;
}

/** Get the list of ALPN protocols. Return TLS_RDONE on success;  <=0 on error. */
static int ext_alpn_parse(Parser *parser, const uint8_t *data, size_t len) {
    const uint8_t *end = data + len;
    int size = datalen16(data, end);
    if (size < 0) {
        return 0;
    }
    data += 2;
    end = data + size;

    while (data != end) {
        int proto_len = datalen8(data, end);
        if (proto_len < 0) {
            return 0;
        }
        data += 1;
        parser->hello.alpn_protocols.emplace_back((const char *) data, (size_t) proto_len);
        data += proto_len;
    }

    return TLS_RDONE;
}

/** Get the list of signature algorithms. Return TLS_RDONE on success;  <=0 on error. */
static int ext_sigalgs_parse(Parser *parser, const uint8_t *data, size_t len) {
    const uint8_t *end = data + len;
    int size = datalen16(data, end);
    if (size < 0) {
        return 0;
    }
    data += 2;
    end = data + size;

    while (data != end) {
        int alg = read_u16(data, end);
        if (alg < 0) {
            return 0;
        }
        parser->hello.signature_algorithms.emplace_back((uint16_t) alg);
        data += 2;
    }

    return TLS_RDONE;
}

/** Get the list of supported versions. Return TLS_RDONE on success;  <=0 on error. */
static int ext_suppvers_parse(Parser *parser, const uint8_t *data, size_t len) {
    const uint8_t *end = data + len;
    int size = datalen8(data, end);
    if (size < 0) {
        return 0;
    }
    data += 1;
    end = data + size;

    while (data != end) {
        int ver = read_u16(data, end);
        if (ver < 0) {
            return 0;
        }
        parser->hello.supported_versions.emplace_back((uint16_t) ver);
        data += 2;
    }

    return TLS_RDONE;
}

/** Get the list of supported groups. Return TLS_RDONE on success;  <=0 on error. */
static int ext_supported_groups_parse(Parser *parser, const uint8_t *data, size_t len) {
    const uint8_t *end = data + len;
    int size = datalen16(data, end);
    if (size < 0) {
        return 0;
    }
    data += 2;
    end = data + size;

    while (data != end) {
        int group = read_u16(data, end);
        if (group < 0) {
            return 0;
        }
        parser->hello.supported_groups.emplace_back((uint16_t) group);
        data += 2;
    }

    return TLS_RDONE;
}

/** Get the list of key share groups. Return TLS_RDONE on success;  <=0 on error. */
static int ext_key_share_parse(Parser *parser, const uint8_t *data, size_t len) {
    const uint8_t *end = data + len;
    int size = datalen16(data, end);
    if (size < 0) {
        return 0;
    }
    data += 2;
    end = data + size;

    while (data != end) {
        int group = read_u16(data, end);
        int key_length = read_u16(data + 2, end);
        if (group < 0 || key_length < 0 || data + 4 + key_length > end) {
            return 0;
        }
        parser->hello.key_share_groups.emplace_back((uint16_t) group);
        data += 4 + key_length;
    }

    return TLS_RDONE;
}

/** Parse TLS extension.
Return TLS_RCLIENT_HELLO_SNI or TLS_RDONE on success;  <=0 on error. */
static int ext_parse(Parser *parser, U8View &data) {
    const uint8_t *end = data.data() + data.size();
    if ((int) sizeof(Ext) > end - data.data()) {
        return TLS_RERR;
    }

    const auto *ext = (Ext *) data.data();
    uint16_t n = ntohs(ext->len);
    if (ext->data + n > end) {
        return TLS_RERR;
    }

    int r = TLS_RDONE;
    uint16_t type = ntohs(ext->type);
    switch (type) {
    case EXT_SERVER_NAME:
        r = ext_servname_parse(parser, ext->data, n);
        break;
    case EXT_SUPPORTED_GROUPS:
        r = ext_supported_groups_parse(parser, ext->data, n);
        break;
    case EXT_SIGNATURE_ALGORITHMS:
        r = ext_sigalgs_parse(parser, ext->data, n);
        break;
    case EXT_ALPN:
        r = ext_alpn_parse(parser, ext->data, n);
        break;
    case EXT_SUPPORTED_VERSIONS:
        r = ext_suppvers_parse(parser, ext->data, n);
        break;
    case EXT_KEY_SHARE:
        r = ext_key_share_parse(parser, ext->data, n);
        break;
    }
    parser->hello.extensions.emplace_back((uint16_t) type);
    parser->hello.extension_details.push_back({(uint16_t) type, std::vector<uint8_t>(ext->data, ext->data + n)});

    data = {ext->data + n, size_t(end - ext->data - n)};
    return r;
}

/** Get data for TLS extensions.
Return TLS_RDONE on success;  0 on error. */
static int exts_data(Parser *parser, U8View data) {
    const uint8_t *end = data.data() + data.size();

    int size = datalen16(data.data(), end);
    if (size < 0) {
        return 0;
    }

    data.remove_prefix(2);
    parser->buf = data.substr(0, size_t(size));

    return TLS_RDONE;
}

static TlsParseResult tls_parse(Parser *parser) {
    int r;
    for (;;) {
        switch (parser->state) {
        case I_REC:
            r = rec_parse(parser, parser->in);
            if (r == 0) {
                return TLS_RMORE;
            }
            if (r < 0) {
                return TLS_RERR;
            }

            switch (r) {
            case CT_HANDSHAKE:
                parser->state = I_HSHAKE;
                continue;
            default:
                return TLS_RERR; // not supported
            }

        case I_HSHAKE:
            if (parser->rec.empty()) {
                parser->state = I_REC;
                return TLS_RDONE;
            }
            r = hshake_parse(parser, parser->rec);
            if (r <= 0) {
                return TLS_RERR;
            }

            switch (r) {
            case HS_CLIENT_HELLO:
                parser->state = I_CLIHEL;
                continue;
            case HS_SERVER_HELLO:
                parser->state = I_HSHAKE;
                continue;
            case HS_CERTIFICATE:
                parser->state = I_CERTS;
                continue;
            case HS_SERVER_KEY_EXCHANGE:
            case HS_CERTIFICATE_REQUEST:
            case HS_SERVER_HELLO_DONE:
                parser->state = I_HSHAKE;
                return TLS_RDONE;
            default:
                return TLS_RERR; // not supported
            }

        case I_CLIHEL:
            r = hello_parse(parser, parser->buf);
            if (r <= 0) {
                return TLS_RERR;
            }

            parser->state = I_CLIHEL_EXTS;
            continue;

        case I_CLIHEL_EXTS:
            r = exts_data(parser, parser->buf);
            if (r <= 0) {
                return TLS_RERR;
            }

            parser->state = I_CLIHEL_EXT;
            continue;

        case I_CLIHEL_EXT:
            if (parser->buf.empty()) {
                parser->state = I_HSHAKE;
                continue;
            }

            r = ext_parse(parser, parser->buf);
            if (r <= 0) {
                return TLS_RERR;
            }
            if (r != TLS_RDONE) {
                return (TlsParseResult) r;
            }

            continue;

        case I_CERTS:
            return TLS_RDONE;
        }
    }
}

std::optional<ClientHello> parse_client_hello(ag::Uint8View data, bool quic) {
    Parser parser;
    parser.in = data;
    if (quic) {
        parser.rec = data;
        parser.state = I_HSHAKE;
    }

    if (tls_parse(&parser) != TLS_RDONE) {
        return std::nullopt;
    }

    return std::move(parser.hello);
}

bool is_grease_value(uint16_t value) {
    return (value & 0x0f0f) == 0x0a0a && ((value >> 8) & 0xff) == (value & 0xff);
}

std::vector<uint16_t> without_grease(const std::vector<uint16_t> &values) {
    std::vector<uint16_t> result;
    result.reserve(values.size());
    std::ranges::copy_if(values, std::back_inserter(result), [](uint16_t value) {
        return !is_grease_value(value);
    });
    return result;
}

} // namespace ag::tls::test
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
