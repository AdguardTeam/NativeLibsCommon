#include <fmt/ranges.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include "common/format.h"
#include "common/tls/make_ssl.h"
#include "common/utils.h"

#include "client_hello.h"
#include "ja4.h"

// Integration test that captures the ClientHello of real browsers and compares it against
// the ClientHello that `make_ssl` generates for the matching `TlsClientProfile`.
//
// It needs macOS, installed browsers and a GUI session, so it is built only with
// `-DNLC_CLIENTHELLO_TESTS=ON` and is not part of `make test`. See `tls/test/README.md`.

namespace ag::tls::test {

constexpr auto CAPTURE_TIMEOUT = std::chrono::seconds(120);
constexpr auto SOCKET_TIMEOUT = std::chrono::seconds(5);
constexpr auto PEEK_TIMEOUT = std::chrono::seconds(5);
constexpr size_t MAX_TLS_RECORD = 64 * 1024;

constexpr uint8_t TLS_RECORD_HANDSHAKE = 0x16;
constexpr uint8_t TLS_HANDSHAKE_CLIENT_HELLO = 0x01;

constexpr int KILL_WAIT_STEP_MS = 100;
constexpr int KILL_WAIT_STEPS = 50;

/** The ClientHello fields that the test compares. */
struct ClientHelloView {
    std::string sni;
    std::vector<uint16_t> cipher_suites;
    std::vector<uint16_t> extensions;
    std::vector<uint16_t> groups;
    std::vector<uint16_t> sigalgs;
    std::vector<uint16_t> supported_versions;
    std::vector<uint16_t> key_share_groups;
    std::vector<std::string> alpn;
};

/** A browser that the test captures. */
struct BrowserSpec {
    std::string_view id;
    std::string_view display_name;
    std::string_view app_path;
    std::string_view exec_path;
    TlsClientProfile profile;
    bool open_with_launch_services;
};

const BrowserSpec CHROME = {
        "chrome",
        "Google Chrome",
        "/Applications/Google Chrome.app",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        TlsClientProfile::CHROME,
        false,
};

const BrowserSpec CHROME_CANARY = {
        "chrome-canary",
        "Google Chrome Canary",
        "/Applications/Google Chrome Canary.app",
        "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
        TlsClientProfile::CHROME_CANARY,
        false,
};

const BrowserSpec FIREFOX = {
        "firefox",
        "Mozilla Firefox",
        "/Applications/Firefox.app",
        "/Applications/Firefox.app/Contents/MacOS/firefox",
        TlsClientProfile::FIREFOX,
        false,
};

const BrowserSpec SAFARI = {
        "safari",
        "Safari",
        "/Applications/Safari.app",
        "/Applications/Safari.app/Contents/MacOS/Safari",
        TlsClientProfile::SAFARI,
        true,
};

static std::vector<uint16_t> without_grease_and_padding(const std::vector<uint16_t> &values) {
    std::vector<uint16_t> result = without_grease(values);
    std::erase_if(result, [](uint16_t value) {
        return value == TLSEXT_TYPE_padding;
    });
    return result;
}

/** Remove the values that JA4 ignores, so the diff shows only meaningful differences. */
static ClientHelloView clean_client_hello(const ag::tls::test::ClientHello &parsed) {
    ClientHelloView hello;
    hello.sni = parsed.sni;
    hello.cipher_suites = without_grease(parsed.cipher_suites);
    hello.extensions = without_grease_and_padding(parsed.extensions);
    hello.groups = without_grease(parsed.supported_groups);
    hello.sigalgs = without_grease(parsed.signature_algorithms);
    hello.supported_versions = without_grease(parsed.supported_versions);
    hello.key_share_groups = without_grease(parsed.key_share_groups);
    hello.alpn = parsed.alpn_protocols;
    return hello;
}

static std::string to_hex(uint16_t value) {
    return ag::format("0x{:04x}", value);
}

static std::string extension_name(uint16_t type) {
    switch (type) {
    case TLSEXT_TYPE_server_name:
        return "server_name";
    case TLSEXT_TYPE_status_request:
        return "status_request";
    case TLSEXT_TYPE_supported_groups:
        return "supported_groups";
    case TLSEXT_TYPE_ec_point_formats:
        return "ec_point_formats";
    case TLSEXT_TYPE_signature_algorithms:
        return "signature_algorithms";
    case TLSEXT_TYPE_application_layer_protocol_negotiation:
        return "alpn";
    case TLSEXT_TYPE_certificate_timestamp:
        return "signed_certificate_timestamp";
    case TLSEXT_TYPE_padding:
        return "padding";
    case TLSEXT_TYPE_encrypt_then_mac:
        return "encrypt_then_mac";
    case TLSEXT_TYPE_extended_master_secret:
        return "extended_master_secret";
    case TLSEXT_TYPE_cert_compression:
        return "certificate_compression";
    case TLSEXT_TYPE_record_size_limit:
        return "record_size_limit";
    case TLSEXT_TYPE_delegated_credential:
        return "delegated_credentials";
    case TLSEXT_TYPE_session_ticket:
        return "session_ticket";
    case TLSEXT_TYPE_supported_versions:
        return "supported_versions";
    case TLSEXT_TYPE_psk_key_exchange_modes:
        return "psk_key_exchange_modes";
    case TLSEXT_TYPE_key_share:
        return "key_share";
    case TLSEXT_TYPE_renegotiate:
        return "renegotiation_info";
    case TLSEXT_TYPE_encrypted_client_hello:
        return "encrypted_client_hello";
    case TLSEXT_TYPE_trust_anchors:
        return "trust_anchors";
    case TLSEXT_TYPE_server_padding:
        return "server_padding";
    case TLSEXT_TYPE_application_settings:
        return "application_settings";
    case TLSEXT_TYPE_application_settings_old:
        return "application_settings_old";
    case TLSEXT_TYPE_pre_shared_key:
        return "pre_shared_key";
    case TLSEXT_TYPE_early_data:
        return "early_data";
    case TLSEXT_TYPE_certificate_authorities:
        return "certificate_authorities";
    case TLSEXT_TYPE_signature_algorithms_cert:
        return "signature_algorithms_cert";
    case TLSEXT_TYPE_tls_flags:
        return "tls_flags";
    case TLSEXT_TYPE_quic_transport_parameters:
        return "quic_transport_parameters";
    default:
        return to_hex(type);
    }
}

static std::string group_name(uint16_t group) {
    switch (group) {
    case SSL_GROUP_SECP256R1:
        return "secp256r1";
    case SSL_GROUP_SECP384R1:
        return "secp384r1";
    case SSL_GROUP_SECP521R1:
        return "secp521r1";
    case SSL_GROUP_X25519:
        return "x25519";
    case 30:
        return "x448";
    case SSL_GROUP_FFDHE2048:
        return "ffdhe2048";
    case SSL_GROUP_FFDHE3072:
        return "ffdhe3072";
    case SSL_GROUP_X25519_MLKEM768:
        return "x25519mlkem768";
    case SSL_GROUP_MLKEM1024:
        return "mlkem1024";
    default:
        return to_hex(group);
    }
}

static std::string sigalg_name(uint16_t sigalg) {
    switch (sigalg) {
    case SSL_SIGN_RSA_PKCS1_SHA256:
        return "rsa_pkcs1_sha256";
    case SSL_SIGN_ECDSA_SECP256R1_SHA256:
        return "ecdsa_secp256r1_sha256";
    case SSL_SIGN_RSA_PKCS1_SHA384:
        return "rsa_pkcs1_sha384";
    case SSL_SIGN_ECDSA_SECP384R1_SHA384:
        return "ecdsa_secp384r1_sha384";
    case SSL_SIGN_RSA_PKCS1_SHA512:
        return "rsa_pkcs1_sha512";
    case SSL_SIGN_ECDSA_SECP521R1_SHA512:
        return "ecdsa_secp521r1_sha512";
    case SSL_SIGN_RSA_PSS_RSAE_SHA256:
        return "rsa_pss_rsae_sha256";
    case SSL_SIGN_RSA_PSS_RSAE_SHA384:
        return "rsa_pss_rsae_sha384";
    case SSL_SIGN_RSA_PSS_RSAE_SHA512:
        return "rsa_pss_rsae_sha512";
    case SSL_SIGN_ED25519:
        return "ed25519";
    case 0x0808:
        return "ed448";
    case 0x0809:
        return "rsa_pss_pss_sha256";
    case 0x080a:
        return "rsa_pss_pss_sha384";
    case 0x080b:
        return "rsa_pss_pss_sha512";
    case SSL_SIGN_ML_DSA_44:
        return "mldsa44";
    case SSL_SIGN_ML_DSA_65:
        return "mldsa65";
    case SSL_SIGN_ML_DSA_87:
        return "mldsa87";
    default:
        return to_hex(sigalg);
    }
}

static std::string cipher_name(uint16_t cipher) {
    switch (cipher) {
    case 0x0004:
        return "TLS_RSA_WITH_RC4_128_MD5";
    case 0x0005:
        return "TLS_RSA_WITH_RC4_128_SHA";
    case 0x000a:
        return "TLS_RSA_WITH_3DES_EDE_CBC_SHA";
    case 0x0013:
        return "TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA";
    case 0x0016:
        return "TLS_DHE_RSA_WITH_AES_128_CBC_SHA";
    case 0x002f:
        return "TLS_RSA_WITH_AES_128_CBC_SHA";
    case 0x0033:
        return "TLS_DHE_RSA_WITH_AES_128_CBC_SHA256";
    case 0x0035:
        return "TLS_RSA_WITH_AES_256_CBC_SHA";
    case 0x0039:
        return "TLS_DHE_RSA_WITH_AES_256_CBC_SHA256";
    case 0x009c:
        return "TLS_RSA_WITH_AES_128_GCM_SHA256";
    case 0x009d:
        return "TLS_RSA_WITH_AES_256_GCM_SHA384";
    case 0x1301:
        return "TLS_AES_128_GCM_SHA256";
    case 0x1302:
        return "TLS_AES_256_GCM_SHA384";
    case 0x1303:
        return "TLS_CHACHA20_POLY1305_SHA256";
    case 0xc02b:
        return "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
    case 0xc02c:
        return "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
    case 0xc02f:
        return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
    case 0xc030:
        return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
    case 0xcca8:
        return "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256";
    case 0xcca9:
        return "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256";
    default:
        return to_hex(cipher);
    }
}

template <typename T, typename NameFn>
static std::string format_sequence(const std::vector<T> &values, NameFn name_fn) {
    std::vector<std::string> names;
    names.reserve(values.size());
    std::ranges::transform(values, std::back_inserter(names), name_fn);
    return ag::format("[{}]", fmt::join(names, ", "));
}

template <typename T>
static std::vector<T> set_difference_sorted(const std::vector<T> &left, const std::vector<T> &right) {
    std::vector<T> result;
    for (const T &value : left) {
        if (std::ranges::find(right, value) == right.end()) {
            result.push_back(value);
        }
    }
    std::ranges::sort(result);
    return result;
}

template <typename T, typename NameFn>
static std::string diff_sequence(const char *label, const std::vector<T> &actual, std::string_view actual_name,
        const std::vector<T> &expected, std::string_view expected_name, NameFn name_fn) {
    if (actual == expected) {
        return {};
    }

    std::ostringstream out;
    out << label << " differ:\n";
    out << "    " << actual_name << ": " << format_sequence(actual, name_fn) << "\n";
    out << "    " << expected_name << ": " << format_sequence(expected, name_fn) << "\n";

    const size_t common = std::min(actual.size(), expected.size());
    for (size_t i = 0; i < common; i++) {
        if (actual[i] != expected[i]) {
            out << "    first difference at index " << i << ": " << actual_name << " has " << name_fn(actual[i]) << ", "
                << expected_name << " has " << name_fn(expected[i]) << "\n";
            break;
        }
    }
    if (actual.size() != expected.size()) {
        out << "    count: " << actual_name << " has " << actual.size() << ", " << expected_name << " has "
            << expected.size() << "\n";
    }

    std::vector<T> only_actual = set_difference_sorted(actual, expected);
    std::vector<T> only_expected = set_difference_sorted(expected, actual);
    if (!only_actual.empty()) {
        out << "    only in " << actual_name << ": " << format_sequence(only_actual, name_fn) << "\n";
    }
    if (!only_expected.empty()) {
        out << "    only in " << expected_name << ": " << format_sequence(only_expected, name_fn) << "\n";
    }
    return out.str();
}

static std::string format_alpn(const std::vector<std::string> &alpn) {
    return ag::format("[{}]", fmt::join(alpn, ", "));
}

static std::string diff_alpn(const std::vector<std::string> &actual, std::string_view actual_name,
        const std::vector<std::string> &expected, std::string_view expected_name) {
    if (actual == expected) {
        return {};
    }
    std::ostringstream out;
    out << "ALPN differ:\n";
    out << "    " << actual_name << ": " << format_alpn(actual) << "\n";
    out << "    " << expected_name << ": " << format_alpn(expected) << "\n";
    return out.str();
}

static std::string diff_client_hellos(const ClientHelloView &actual, std::string_view actual_name,
        const ClientHelloView &expected, std::string_view expected_name, bool permuted_extensions) {
    std::string out;
    if (actual.sni != expected.sni) {
        out += ag::format(
                "SNI differ: {} has \"{}\", {} has \"{}\"\n", actual_name, actual.sni, expected_name, expected.sni);
    }
    out += diff_sequence(
            "cipher suites", actual.cipher_suites, actual_name, expected.cipher_suites, expected_name, cipher_name);

    std::vector<uint16_t> actual_extensions = actual.extensions;
    std::vector<uint16_t> expected_extensions = expected.extensions;
    const char *extensions_label = "extensions (wire order)";
    if (permuted_extensions) {
        // Chrome permutes the extension order on every handshake, so only the set is meaningful.
        std::ranges::sort(actual_extensions);
        std::ranges::sort(expected_extensions);
        extensions_label = "extensions (set; this profile permutes the wire order)";
    }
    out += diff_sequence(
            extensions_label, actual_extensions, actual_name, expected_extensions, expected_name, extension_name);

    out += diff_sequence("supported groups", actual.groups, actual_name, expected.groups, expected_name, group_name);
    out += diff_sequence(
            "signature algorithms", actual.sigalgs, actual_name, expected.sigalgs, expected_name, sigalg_name);
    out += diff_sequence("supported versions", actual.supported_versions, actual_name, expected.supported_versions,
            expected_name, to_hex);
    out += diff_sequence("key share groups", actual.key_share_groups, actual_name, expected.key_share_groups,
            expected_name, group_name);
    out += diff_alpn(actual.alpn, actual_name, expected.alpn, expected_name);
    return out;
}

static std::string read_command_output(const std::vector<std::string> &args) {
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        return {};
    }

    std::vector<std::string> arguments = args;
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return {};
    }
    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(pipe_fds[1]);
    std::string output;
    char buffer[4096];
    for (;;) {
        const ssize_t n = read(pipe_fds[0], buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        output.append(buffer, (size_t) n);
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return std::string(ag::utils::trim(output));
}

static std::string read_bundle_version(std::string_view app_path) {
    const std::string info_plist = ag::format("{}/Contents/Info", app_path);
    std::string version = read_command_output({"/usr/bin/defaults", "read", info_plist, "CFBundleShortVersionString"});
    if (version.empty()) {
        return "unknown";
    }
    return version;
}

static bool has_gui_session() {
    const std::string manager = read_command_output({"/bin/launchctl", "managername"});
    return manager == "Aqua";
}

static std::string env_or(const char *name, const std::string &fallback = {}) {
    const char *value = getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value;
}

static bool env_flag(const char *name) {
    const std::string value = env_or(name);
    return value == "1" || value == "true" || value == "yes";
}

static std::vector<std::string> split(const std::string &value, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : value) {
        if (c == separator) {
            if (!current.empty()) {
                parts.push_back(current);
            }
            current.clear();
        } else if (c != ' ' && c != '\t') {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

static bool is_required_browser(std::string_view id) {
    const std::vector<std::string> required = split(env_or("AG_CLIENTHELLO_REQUIRED_BROWSERS"), ',');
    return std::ranges::any_of(required, [id](const std::string &value) {
        return std::string_view(value) == id;
    });
}

static std::filesystem::path artifacts_dir() {
    return env_or("AG_CLIENTHELLO_ARTIFACTS_DIR");
}

static void write_artifact(const std::string &name, const void *data, size_t size) {
    const std::filesystem::path dir = artifacts_dir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream file(dir / name, std::ios::binary);
    if (file) {
        file.write(static_cast<const char *>(data), (std::streamsize) size);
    }
}

static void write_artifact(const std::string &name, const std::string &text) {
    write_artifact(name, text.data(), text.size());
}

static void append_artifact(const std::string &name, const std::string &text) {
    const std::filesystem::path dir = artifacts_dir();
    if (dir.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream file(dir / name, std::ios::binary | std::ios::app);
    if (file) {
        file << text;
    }
}

static std::string describe_client_hello(const ClientHelloView &hello, const std::string &ja4) {
    return ag::format("ja4: {}\nsni: {}\ncipher_suites: {}\nextensions: {}\nsupported_groups: {}\n"
                      "signature_algorithms: {}\nsupported_versions: {}\nkey_share_groups: {}\nalpn: {}\n",
            ja4, hello.sni.empty() ? std::string_view("(none)") : std::string_view(hello.sni),
            format_sequence(hello.cipher_suites, cipher_name), format_sequence(hello.extensions, extension_name),
            format_sequence(hello.groups, group_name), format_sequence(hello.sigalgs, sigalg_name),
            format_sequence(hello.supported_versions, to_hex), format_sequence(hello.key_share_groups, group_name),
            format_alpn(hello.alpn));
}

static std::string ja4_of(const std::vector<uint8_t> &record) {
    if (record.empty()) {
        return {};
    }
    return ag::ja4::compute({record.data(), record.size()}, false);
}

static std::optional<std::vector<uint8_t>> build_profile_client_hello(
        TlsClientProfile profile, const std::string &sni, const std::vector<std::string> &alpn, std::string &error) {
    std::vector<uint8_t> alpn_wire;
    for (const std::string &protocol : alpn) {
        alpn_wire.push_back((uint8_t) protocol.size());
        alpn_wire.insert(alpn_wire.end(), protocol.begin(), protocol.end());
    }

    SslInitParameters params;
    params.profile = profile;
    params.protocol = SslProtocol::TLS;
    if (!alpn_wire.empty()) {
        params.alpn_protos = {alpn_wire.data(), alpn_wire.size()};
    }
    params.sni = sni.empty() ? nullptr : sni.c_str();
    params.post_quantum = true;

    auto result = make_ssl(params);
    if (!std::holds_alternative<SslPtr>(result)) {
        error = std::get<std::string>(result);
        return std::nullopt;
    }
    SslPtr ssl = std::move(std::get<SslPtr>(result));

    SSL_set0_wbio(ssl.get(), BIO_new(BIO_s_mem()));
    SSL_connect(ssl.get());
    std::vector<uint8_t> hello(MAX_TLS_RECORD);
    const int n = BIO_read(SSL_get_wbio(ssl.get()), hello.data(), (int) hello.size());
    if (n <= 0) {
        error = "make_ssl produced no ClientHello";
        return std::nullopt;
    }
    hello.resize((size_t) n);
    return hello;
}

/** A TLS server that records the first ClientHello of every connection.
 *
 * The server peeks the first TLS record before OpenSSL reads it, so the captured bytes are exactly
 * what the browser sent. After the capture it completes the handshake with a self-signed certificate
 * (the browser rejects the certificate, which is expected) and advertises `h2` and `http/1.1` over ALPN.
 */
class ClientHelloServer {
public:
    ClientHelloServer() = default;

    ~ClientHelloServer() {
        stop();
    }

    ClientHelloServer(const ClientHelloServer &) = delete;
    ClientHelloServer &operator=(const ClientHelloServer &) = delete;

    bool start(std::string &error) {
        if (!create_context(error)) {
            return false;
        }

        m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_fd < 0) {
            error = ag::format("socket() failed: {}", strerror(errno));
            return false;
        }

        int reuse = 1;
        setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(m_listen_fd, (sockaddr *) &address, sizeof(address)) != 0) {
            error = ag::format("bind() failed: {}", strerror(errno));
            return false;
        }
        if (listen(m_listen_fd, 8) != 0) {
            error = ag::format("listen() failed: {}", strerror(errno));
            return false;
        }

        sockaddr_in bound = {};
        socklen_t bound_length = sizeof(bound);
        if (getsockname(m_listen_fd, (sockaddr *) &bound, &bound_length) != 0) {
            error = ag::format("getsockname() failed: {}", strerror(errno));
            return false;
        }
        m_port = ntohs(bound.sin_port);

        m_thread = std::thread(&ClientHelloServer::accept_loop, this);
        return true;
    }

    uint16_t port() const {
        return m_port;
    }

    bool wait_for_capture(std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        return m_condition.wait_for(lock, timeout, [this] {
            return !m_captures.empty();
        });
    }

    std::vector<uint8_t> first_capture() const {
        std::lock_guard lock(m_mutex);
        if (m_captures.empty()) {
            return {};
        }
        return m_captures.front();
    }

private:
    bool create_context(std::string &error) {
        const std::filesystem::path data_dir = std::filesystem::path(CLIENTHELLO_TEST_DATA_DIR);
        const std::string cert_path = (data_dir / "clienthello_test_cert.pem").string();
        const std::string key_path = (data_dir / "clienthello_test_key.pem").string();

        m_ctx = SSL_CTX_new(TLS_server_method());
        if (m_ctx == nullptr) {
            error = "SSL_CTX_new() failed";
            return false;
        }

        SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);
        SSL_CTX_set_alpn_select_cb(m_ctx, &ClientHelloServer::select_alpn, nullptr);

        if (SSL_CTX_use_certificate_chain_file(m_ctx, cert_path.c_str()) != 1) {
            error = ag::format("cannot load the test certificate: {}", cert_path);
            return false;
        }
        if (SSL_CTX_use_PrivateKey_file(m_ctx, key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
            error = ag::format("cannot load the test private key: {}", key_path);
            return false;
        }
        if (SSL_CTX_check_private_key(m_ctx) != 1) {
            error = "the test certificate and private key do not match";
            return false;
        }
        return true;
    }

    static int select_alpn(SSL *, const unsigned char **out, unsigned char *out_length, const unsigned char *in,
            unsigned int in_length, void *) {
        // Advertise the same two protocols that the browsers advertise.
        static constexpr unsigned char ALPN[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
        unsigned char *selected = nullptr;
        if (SSL_select_next_proto(&selected, out_length, ALPN, sizeof(ALPN), in, in_length) == OPENSSL_NPN_NEGOTIATED) {
            *out = selected;
            return SSL_TLSEXT_ERR_OK;
        }
        return SSL_TLSEXT_ERR_NOACK;
    }

    void accept_loop() {
        while (!m_stopping) {
            pollfd poll_fd = {m_listen_fd, POLLIN, 0};
            const int ready = poll(&poll_fd, 1, 200);
            if (ready <= 0) {
                continue;
            }

            sockaddr_in peer = {};
            socklen_t peer_length = sizeof(peer);
            const int connection = accept(m_listen_fd, (sockaddr *) &peer, &peer_length);
            if (connection < 0) {
                continue;
            }
            handle_connection(connection);
        }
    }

    void handle_connection(int connection) {
        timeval timeout = {};
        timeout.tv_sec = (time_t) SOCKET_TIMEOUT.count();
        setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        std::vector<uint8_t> hello;
        if (peek_client_hello(connection, hello)) {
            {
                std::lock_guard lock(m_mutex);
                m_captures.push_back(hello);
            }
            m_condition.notify_all();
        }

        // Complete the handshake so the browser gets a TLS alert about the self-signed certificate
        // instead of a connection reset. The result does not matter for the captured bytes.
        SSL *ssl = SSL_new(m_ctx);
        if (ssl != nullptr) {
            SSL_set_fd(ssl, connection);
            SSL_accept(ssl);
            SSL_free(ssl);
        }
        close(connection);
    }

    /** Peek the first TLS record without consuming it, so OpenSSL can parse the same bytes later. */
    static bool peek_client_hello(int connection, std::vector<uint8_t> &hello) {
        std::vector<uint8_t> buffer(MAX_TLS_RECORD);
        const auto deadline = std::chrono::steady_clock::now() + PEEK_TIMEOUT;

        for (;;) {
            const ssize_t available = recv(connection, buffer.data(), buffer.size(), MSG_PEEK);
            if (available < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (available == 0) {
                return false;
            }

            if (available >= 5) {
                const size_t record_length = 5 + ((size_t) buffer[3] << 8) + (size_t) buffer[4];
                if (record_length > buffer.size()) {
                    return false;
                }
                if ((size_t) available >= record_length && record_length >= 6) {
                    hello.assign(buffer.begin(), buffer.begin() + (long) record_length);
                    return hello[0] == TLS_RECORD_HANDSHAKE && hello[5] == TLS_HANDSHAKE_CLIENT_HELLO;
                }
            }

            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            pollfd poll_fd = {connection, POLLIN, 0};
            poll(&poll_fd, 1, 100);
        }
    }

    void stop() {
        if (m_stopping) {
            return;
        }
        m_stopping = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_listen_fd >= 0) {
            close(m_listen_fd);
            m_listen_fd = -1;
        }
        if (m_ctx != nullptr) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
        }
    }

    int m_listen_fd = -1;
    uint16_t m_port = 0;
    SSL_CTX *m_ctx = nullptr;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::vector<std::vector<uint8_t>> m_captures;
    std::atomic<bool> m_stopping = false;
};

static pid_t spawn_process(std::string_view executable, const std::vector<std::string> &arguments) {
    std::vector<std::string> arguments_with_executable;
    arguments_with_executable.reserve(arguments.size() + 1);
    arguments_with_executable.emplace_back(executable);
    arguments_with_executable.insert(arguments_with_executable.end(), arguments.begin(), arguments.end());

    std::vector<char *> argv;
    argv.reserve(arguments_with_executable.size() + 1);
    for (std::string &argument : arguments_with_executable) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        const int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execv(argv[0], argv.data());
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
}

static void terminate_process(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    // Signal both the process and its group. The process may not have become a group
    // leader, and then the negative PID targets nothing.
    kill(-pid, SIGTERM);
    kill(pid, SIGTERM);
    for (int i = 0; i < KILL_WAIT_STEPS; i++) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(KILL_WAIT_STEP_MS));
    }
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
}

class TempDirectory {
public:
    explicit TempDirectory(const std::string &prefix) {
        std::string pattern = (std::filesystem::temp_directory_path() / ag::format("{}-XXXXXX", prefix)).string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char *created = mkdtemp(buffer.data());
        if (created != nullptr) {
            m_path = created;
        }
    }

    ~TempDirectory() {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }

    TempDirectory(const TempDirectory &) = delete;
    TempDirectory &operator=(const TempDirectory &) = delete;

    const std::filesystem::path &path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

struct CaptureResult {
    bool browser_found = false;
    bool captured = false;
    bool gui_missing = false;
    std::string version;
    std::string error;
    std::vector<uint8_t> hello;
};

static CaptureResult capture_browser(const BrowserSpec &browser) {
    CaptureResult result;
    result.browser_found = access(browser.app_path.data(), F_OK) == 0;
    if (!result.browser_found) {
        result.error = ag::format("{} is not installed at {}", browser.display_name, browser.app_path);
        return result;
    }
    result.version = read_bundle_version(browser.app_path);

    const bool headless = env_or("AG_CLIENTHELLO_BROWSER_MODE", "gui") == "headless";
    const bool gui_required = !headless || browser.open_with_launch_services;
    if (gui_required && !has_gui_session()) {
        result.gui_missing = true;
        result.error = "no GUI session is available (launchctl managername is not Aqua)";
        return result;
    }

    ClientHelloServer server;
    std::string error;
    if (!server.start(error)) {
        result.error = ag::format("cannot start the TLS server: {}", error);
        return result;
    }

    TempDirectory profile(ag::format("clienthello-{}", browser.id));
    if (!browser.open_with_launch_services && profile.path().empty()) {
        result.error = "cannot create a temporary browser profile directory";
        return result;
    }
    const std::string url = ag::format("https://localhost:{}/", server.port());

    pid_t pid = -1;
    if (browser.open_with_launch_services) {
        pid = spawn_process("/usr/bin/open", {"-a", std::string(browser.display_name), url});
    } else if (browser.id == "firefox") {
        std::vector<std::string> arguments = {"-no-remote", "-profile", profile.path().string()};
        if (headless) {
            arguments.emplace_back("-headless");
        }
        arguments.push_back(url);
        pid = spawn_process(browser.exec_path, arguments);
    } else {
        std::vector<std::string> arguments = {
                ag::format("--user-data-dir={}", profile.path().string()),
                "--no-first-run",
                "--no-default-browser-check",
                "--disable-background-networking",
                "--disable-component-update",
                "--disable-sync",
                "--disable-extensions",
                "--new-window",
        };
        if (headless) {
            arguments.emplace_back("--headless=new");
        }
        arguments.push_back(url);
        pid = spawn_process(browser.exec_path, arguments);
    }

    if (pid < 0) {
        result.error = ag::format("cannot start {}", browser.display_name);
        return result;
    }

    result.captured = server.wait_for_capture(CAPTURE_TIMEOUT);
    if (!result.captured) {
        result.error =
                ag::format("no ClientHello from {} within {} seconds", browser.display_name, CAPTURE_TIMEOUT.count());
    } else {
        result.hello = server.first_capture();
    }

    if (browser.open_with_launch_services) {
        int status = 0;
        waitpid(pid, &status, 0);
        // Safari is not terminated: it is the user's application and may have other windows.
        // The test leaves one tab with the captured URL open.
    } else {
        terminate_process(pid);
    }

    const std::string prefix = ag::format("clienthello-{}", browser.id);
    if (result.captured) {
        write_artifact(ag::format("{}.bin", prefix), result.hello.data(), result.hello.size());
    } else {
        write_artifact(ag::format("{}-error.txt", prefix), result.error);
    }
    return result;
}

static void check_browser(const BrowserSpec &browser) {
    CaptureResult capture = capture_browser(browser);
    if (!capture.browser_found) {
        if (is_required_browser(browser.id)) {
            FAIL() << capture.error << " (the browser is listed in AG_CLIENTHELLO_REQUIRED_BROWSERS)";
        }
        GTEST_SKIP() << capture.error;
    }
    if (capture.gui_missing) {
        if (is_required_browser(browser.id)) {
            FAIL() << capture.error;
        }
        GTEST_SKIP() << capture.error;
    }

    ASSERT_TRUE(capture.captured) << capture.error;

    std::optional<ag::tls::test::ClientHello> parsed_browser =
            ag::tls::test::parse_client_hello({capture.hello.data(), capture.hello.size()}, false);
    ASSERT_TRUE(parsed_browser.has_value()) << "cannot parse the " << browser.display_name << " ClientHello";
    ClientHelloView browser_hello = clean_client_hello(*parsed_browser);

    std::string build_error;
    // The browser profiles are expected to advertise these two protocols. Use the
    // canonical list, not the browser list, so the diff reports an ALPN difference.
    const std::vector<std::string> expected_alpn = {"h2", "http/1.1"};
    const std::optional<std::vector<uint8_t>> profile_record =
            build_profile_client_hello(browser.profile, browser_hello.sni, expected_alpn, build_error);
    ASSERT_TRUE(profile_record.has_value()) << "cannot build the profile ClientHello: " << build_error;

    std::optional<ag::tls::test::ClientHello> parsed_profile =
            ag::tls::test::parse_client_hello({profile_record->data(), profile_record->size()}, false);
    ASSERT_TRUE(parsed_profile.has_value()) << "cannot parse the profile ClientHello";
    ClientHelloView profile_hello = clean_client_hello(*parsed_profile);

    const std::string browser_ja4 = ja4_of(capture.hello);
    const std::string profile_ja4 = ja4_of(*profile_record);
    const bool permuted_extensions =
            browser.profile == TlsClientProfile::CHROME || browser.profile == TlsClientProfile::CHROME_CANARY;
    const std::string diff =
            diff_client_hellos(browser_hello, browser.display_name, profile_hello, "make_ssl", permuted_extensions);

    const std::string summary = ag::format("browser: {} {}\n{}", browser.display_name, capture.version,
            describe_client_hello(browser_hello, browser_ja4));
    write_artifact(ag::format("clienthello-{}.txt", browser.id), summary);
    append_artifact("ja4-browsers.txt", ag::format("{} {} {}\n", browser.id, capture.version, browser_ja4));
    if (!diff.empty()) {
        write_artifact(ag::format("diff-{}.txt", browser.id), diff);
    }

    // JA4 is the compatibility criterion for the fields it hashes: the cipher suite set, the
    // extension set and the signature algorithms. It does not hash the group lists or the
    // supported versions, so assert those separately.
    //
    // An empty fingerprint means that `ag::ja4::compute` rejected the bytes. Two empty strings
    // compare equal, so assert them first or the test passes without checking anything.
    ASSERT_FALSE(browser_ja4.empty()) << browser.display_name << ": cannot compute the browser JA4";
    ASSERT_FALSE(profile_ja4.empty()) << "cannot compute the make_ssl profile JA4";

    EXPECT_EQ(browser_hello.groups, profile_hello.groups) << diff;
    EXPECT_EQ(browser_hello.key_share_groups, profile_hello.key_share_groups) << diff;
    EXPECT_EQ(browser_hello.supported_versions, profile_hello.supported_versions) << diff;

    // A raw-order difference (for example, Safari puts TLS_AES_256_GCM_SHA384 first) is reported
    // in the diff artifact but does not fail the test.
    if (browser_ja4 != profile_ja4) {
        FAIL() << browser.display_name << " does not match the make_ssl profile\n"
               << "    browser JA4: " << browser_ja4 << "\n"
               << "    profile JA4: " << profile_ja4 << "\n"
               << diff;
    }
    if (!diff.empty()) {
        ag::print(stderr, "[NOTE] {}: JA4 matches, but the raw ClientHello differs (see diff-{}.txt)\n{}",
                browser.display_name, browser.id, diff);
    }
}

// Print the reference fingerprints. The scheduled workflow stores them as artifacts, so a drift
// report contains both the browser and the profile side.
TEST(ClientHelloProfiles, ProfileFingerprints) {
    const std::pair<TlsClientProfile, const char *> profiles[] = {
            {TlsClientProfile::CHROME, "CHROME"},
            {TlsClientProfile::CHROME_CANARY, "CHROME_CANARY"},
            {TlsClientProfile::FIREFOX, "FIREFOX"},
            {TlsClientProfile::SAFARI, "SAFARI"},
    };
    const std::vector<std::string> alpn = {"h2", "http/1.1"};

    std::string report;
    for (const auto &[profile, name] : profiles) {
        std::string error;
        const std::optional<std::vector<uint8_t>> record =
                build_profile_client_hello(profile, "localhost", alpn, error);
        ASSERT_TRUE(record.has_value()) << name << ": " << error;
        const std::string ja4 = ja4_of(*record);
        EXPECT_FALSE(ja4.empty()) << name;
        ag::print(stderr, "[JA4 {:<13}] {}\n", name, ja4);
        report += ag::format("{} {}\n", name, ja4);
    }
    write_artifact("ja4-profiles.txt", report);
}

TEST(ClientHelloProfiles, Chrome) {
    check_browser(CHROME);
}

TEST(ClientHelloProfiles, ChromeCanary) {
    check_browser(CHROME_CANARY);
}

TEST(ClientHelloProfiles, Firefox) {
    check_browser(FIREFOX);
}

TEST(ClientHelloProfiles, Safari) {
    check_browser(SAFARI);
}

// Guards the diff machinery without a browser. A mismatched profile must produce a readable
// report, so a real mismatch in the tests above can never pass silently.
TEST(ClientHelloProfiles, MismatchDiffIsReported) {
    const std::vector<std::string> alpn = {"h2", "http/1.1"};

    std::string error;
    const std::optional<std::vector<uint8_t>> safari_record =
            build_profile_client_hello(TlsClientProfile::SAFARI, "localhost", alpn, error);
    ASSERT_TRUE(safari_record.has_value()) << error;
    const std::optional<std::vector<uint8_t>> chrome_record =
            build_profile_client_hello(TlsClientProfile::CHROME, "localhost", alpn, error);
    ASSERT_TRUE(chrome_record.has_value()) << error;

    std::optional<ag::tls::test::ClientHello> parsed_safari =
            ag::tls::test::parse_client_hello({safari_record->data(), safari_record->size()}, false);
    ASSERT_TRUE(parsed_safari.has_value());
    std::optional<ag::tls::test::ClientHello> parsed_chrome =
            ag::tls::test::parse_client_hello({chrome_record->data(), chrome_record->size()}, false);
    ASSERT_TRUE(parsed_chrome.has_value());

    ClientHelloView safari = clean_client_hello(*parsed_safari);
    ClientHelloView chrome = clean_client_hello(*parsed_chrome);

    const std::string diff = diff_client_hellos(safari, "SAFARI", chrome, "CHROME", false);
    EXPECT_FALSE(diff.empty());
    // These profiles differ in every field that the diff reports. Assert each section, so a
    // refactor cannot silently drop one and still pass because another section is non-empty.
    const char *expected_sections[] = {
            "cipher suites",
            "extensions",
            "supported groups",
            "signature algorithms",
    };
    for (const char *section : expected_sections) {
        EXPECT_NE(std::string::npos, diff.find(section)) << section << " is missing from:\n" << diff;
    }
}

} // namespace ag::tls::test
