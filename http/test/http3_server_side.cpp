#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>

#ifdef _WIN32
#define NOCRYPT
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <netdb.h>
#include <sys/fcntl.h>
#endif

#include <event2/event.h>
#include <event2/util.h>
#include <gtest/gtest.h>
#include <ngtcp2/ngtcp2_crypto_boringssl.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "common/http/http3.h"
#include "common/logger.h"
#include "common/net_utils.h"
#include "common/socket_address.h"
#include "http3_test_common.h"

// CN = `SERVER_NAME`
static const ag::UniquePtr<X509, &X509_free> CERTIFICATE = []() {
    constexpr const char *CERTIFICATE_STR = "-----BEGIN CERTIFICATE-----\n"
                                            "MIIBWzCCAQKgAwIBAgIJAPMJtfZE26GgMAoGCCqGSM49BAMCMBoxGDAWBgNVBAMM\n"
                                            "D3d3dy5leGFtcGxlLmNvbTAeFw0yMzA3MTAwMDAwMDBaFw0yNDA3MDkwMDAwMDBa\n"
                                            "MBoxGDAWBgNVBAMMD3d3dy5leGFtcGxlLmNvbTBZMBMGByqGSM49AgEGCCqGSM49\n"
                                            "AwEHA0IABE7ROZI0gNMKQsvtduO+U8L1qr8JhIGDtNA4yMxh8Lg+zm1wugUlcwHG\n"
                                            "Ze1v66IHrdRnLfkdOL75SK2Cqha6FFKjMTAvMC0GA1UdEQQmMCSCD3d3dy5leGFt\n"
                                            "cGxlLmNvbYIRKi53d3cuZXhhbXBsZS5jb20wCgYIKoZIzj0EAwIDRwAwRAIgIk35\n"
                                            "V6jL64rdW8czciR7USxKfRx6dawrbUtxqS8U0AsCICZJC99gM21VIQFCBP1orgbG\n"
                                            "qrBMxG7OZOZ/tl7M7+Ll\n"
                                            "-----END CERTIFICATE-----";

    ag::UniquePtr<BIO, &BIO_free_all> bio{BIO_new(BIO_s_mem())};
    BIO_puts(bio.get(), CERTIFICATE_STR);
    return ag::UniquePtr<X509, &X509_free>{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
}();

static const ag::UniquePtr<EVP_PKEY, &EVP_PKEY_free> PRIVATE_KEY = []() {
    constexpr const char *PRIVATE_KEY_STR = "-----BEGIN PRIVATE KEY-----\n"
                                            "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgQR8tv+rNFlPal3RB\n"
                                            "C/uu3LqupGu/SUIFtKKfQO++6XuhRANCAARO0TmSNIDTCkLL7XbjvlPC9aq/CYSB\n"
                                            "g7TQOMjMYfC4Ps5tcLoFJXMBxmXtb+uiB63UZy35HTi++UitgqoWuhRS\n"
                                            "-----END PRIVATE KEY-----";

    ag::UniquePtr<BIO, &BIO_free_all> bio{BIO_new(BIO_s_mem())};
    BIO_puts(bio.get(), PRIVATE_KEY_STR);
    return ag::UniquePtr<EVP_PKEY, &EVP_PKEY_free>{PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr)};
}();

static ag::Logger logger("!SERV");
static thread_local uint8_t socket_buffer[2 * 1024];

enum ServerSide::State : int {
    STOPPED,
    STARTED,
};

using Session = ServerSide::Session;
Session::Session(ServerSide *parent, const ag::SocketAddress &peer)
        : parent(parent)
        , peer(peer)
        , expiry_timer(event_new(
                  parent->base.get(), -1, EV_PERSIST,
                  [](evutil_socket_t, short, void *arg) {
                      auto *self = (Session *) arg;
                      self->server->handle_expiry();
                      ag::Error<ag::http::Http3Error> error = self->server->flush();
                      if (error != nullptr) {
                          errlog(logger, "Couldn't flush session: {}", error->str());
                          abort();
                      }
                  },
                  this)) {
}

void Session::flush() {
    ag::Error<ag::http::Http3Error> error = server->flush();
    ASSERT_EQ(error, nullptr) << error->str();
}

static void make_ssl(ag::UniquePtr<SSL, &SSL_free> &ssl) {
    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_server_method());
    ASSERT_NE(ssl_ctx, nullptr) << ERR_error_string(ERR_get_error(), nullptr);
    ASSERT_EQ(ngtcp2_crypto_boringssl_configure_server_context(ssl_ctx), 0);
    SSL_CTX_set_alpn_select_cb(
            ssl_ctx,
            [](SSL *, const uint8_t **out, uint8_t *out_len, const uint8_t *, unsigned, void *) {
                static constexpr std::string_view ALPN = &NGHTTP3_ALPN_H3[1];
                *out = (uint8_t *) ALPN.data();
                *out_len = ALPN.size();
                return SSL_TLSEXT_ERR_OK;
            },
            nullptr);
    SSL_CTX_set_info_callback(ssl_ctx, [](const SSL *ssl, int type_, int value) {
        uint32_t type = type_;
        std::string_view where = "undefined";
        if (type & SSL_ST_CONNECT) {
            where = "SSL_connect";
        } else if (type & SSL_ST_ACCEPT) {
            where = "SSL_accept";
        }

        if (type & SSL_CB_LOOP) {
            tracelog(logger, "{}: {}", where, SSL_state_string_long(ssl));
        } else if (type & SSL_CB_ALERT) {
            bool is_fatal = (uint16_t(value) >> 8u) == SSL3_AL_FATAL;
            uint8_t alert = uint16_t(value) & 0xfu;
            warnlog(logger, "{}: {} {} TLS alert: {} ({})", where, (type & SSL_CB_READ) ? "received" : "sent",
                    is_fatal ? "fatal" : "warning", SSL_alert_type_string_long(alert),
                    SSL_alert_desc_string_long(alert));
        } else if (type & SSL_CB_EXIT) {
            if (value == 0) {
                warnlog(logger, "{}: failed in {}", where, SSL_state_string_long(ssl));
            } else if (value < 0) {
                warnlog(logger, "{}: error in {}", where, SSL_state_string_long(ssl));
            }
        }
    });
    ASSERT_EQ(1, SSL_CTX_use_certificate(ssl_ctx, CERTIFICATE.get()));
    ASSERT_EQ(1, SSL_CTX_use_PrivateKey(ssl_ctx, PRIVATE_KEY.get()));
    ASSERT_EQ(1, SSL_CTX_check_private_key(ssl_ctx));
    ssl.reset(SSL_new(ssl_ctx));
    ASSERT_NE(ssl, nullptr);
    SSL_set_tlsext_host_name(ssl.get(), SERVER_NAME);
    SSL_set_accept_state(ssl.get());

    if (const char *file = getenv("SSLKEYLOGFILE"); file != nullptr) {
        static ag::UniquePtr<std::FILE, &std::fclose> handle{std::fopen(file, "a")};
        ASSERT_NE(handle, nullptr);
        SSL_CTX_set_keylog_callback(ssl_ctx, [](const SSL *, const char *line) {
            fprintf(handle.get(), "%s\n", line);
            fflush(handle.get());
        });
    }
}

static void on_request(void *arg, uint64_t stream_id, ag::http::Request request) {
    infolog(logger, "[Stream={}] {}", stream_id, request);
    auto *self = (Session *) arg;
    const ag::http::Request &req = self->streams[stream_id].request.emplace(std::move(request));

    std::optional<ag::http::Response> response;
    std::optional<std::vector<uint8_t>> body;
    std::optional<ag::http::Headers> trailer;
    bool eof = false;

    if (std::string_view path = req.path(), method = req.method(); method == "GET" && path == "/") {
        response.emplace(ag::http::HTTP_3_0, 200);
        eof = true;
    } else if (method == "GET" && path == DOWNLOAD_REQUEST_PATH) {
        response.emplace(ag::http::HTTP_3_0, 200);
        body.emplace(DOWNLOAD_SIZE);
        eof = true;
    } else if (method == "POST" && path == UPLOAD_REQUEST_PATH) {
        // waiting for eof
    } else if (method == "GET" && path == TRAILER_REQUEST_PATH) {
        response.emplace(ag::http::HTTP_3_0, 200);
        body.emplace(1);
        ag::http::Headers headers;
        headers.put("foo", "bar");
        trailer.emplace(std::move(headers));
        eof = true;
    } else if (method == "POST" && path == TRAILER_REQUEST_PATH) {
        // waiting for trailers
    } else {
        warnlog(logger, "Unexpected request");
        response.emplace(ag::http::HTTP_3_0, 400);
        eof = true;
    }

    if (response.has_value()) {
        infolog(logger, "Sending response: {}", response.value());
        ag::Error<ag::http::Http3Error> error = self->server->submit_response(
                stream_id, response.value(), eof && !body.has_value() && !trailer.has_value());
        if (error != nullptr) {
            warnlog(logger, "Couldn't submit response: {}", error->str());
        }
    }

    if (body.has_value()) {
        infolog(logger, "Sending body: {} bytes", body->size());
        ag::Error<ag::http::Http3Error> error =
                self->server->submit_body(stream_id, {body->data(), body->size()}, eof && !trailer.has_value());
        if (error != nullptr) {
            warnlog(logger, "Couldn't submit body: {}", error->str());
        }
    }

    if (trailer.has_value()) {
        infolog(logger, "Sending trailer: {}", trailer.value());
        ag::Error<ag::http::Http3Error> error = self->server->submit_trailer(stream_id, trailer.value());
        if (error != nullptr) {
            warnlog(logger, "Couldn't submit trailer: {}", error->str());
        }
    }
}

static void on_trailer_headers(void *arg, uint64_t stream_id, ag::http::Headers headers) {
    infolog(logger, "[Stream={}] {}", stream_id, headers);
    auto *self = (Session *) arg;
    const ag::http::Request &req = self->streams[stream_id].request.value();
    self->streams[stream_id].trailer.emplace(std::move(headers));

    std::optional<ag::http::Response> response;

    if (std::string_view path = req.path(), method = req.method(); method == "POST" && path == TRAILER_REQUEST_PATH) {
        // waiting for eof
    } else {
        warnlog(logger, "Unexpected request");
        response.emplace(ag::http::HTTP_3_0, 400);
    }

    if (response.has_value()) {
        infolog(logger, "Sending response: {}", response.value());
        ag::Error<ag::http::Http3Error> error = self->server->submit_response(stream_id, response.value(), true);
        if (error != nullptr) {
            warnlog(logger, "Couldn't submit response: {}", error->str());
        }
    }
}

static void on_body(void *arg, uint64_t stream_id, ag::Uint8View chunk) {
    infolog(logger, "[Stream={}] {} bytes", stream_id, chunk.size());
    auto *self = (Session *) arg;
    Session::Stream &stream = self->streams[stream_id];
    stream.body.insert(stream.body.end(), chunk.begin(), chunk.end());
    self->server->consume_stream(stream_id, chunk.size());
}

static void on_stream_read_finished(void *arg, uint64_t stream_id) {
    infolog(logger, "[Stream={}] ...", stream_id);
    auto *self = (Session *) arg;
    Session::Stream &stream = self->streams[stream_id];
    stream.read_finished = true;

    std::optional<ag::http::Response> response;

    const ag::http::Request &request = stream.request.value();
    if (std::string_view path = request.path(), method = request.method();
            method == "POST" && path == UPLOAD_REQUEST_PATH) {
        if (stream.body.size() == UPLOAD_SIZE) {
            response.emplace(ag::http::HTTP_3_0, 200);
        } else {
            warnlog(logger, "Received body size is less than expected: {} vs {}", stream.body.size(), UPLOAD_SIZE);
            response.emplace(ag::http::HTTP_3_0, 400);
        }
    } else if (method == "POST" && path == TRAILER_REQUEST_PATH) {
        if (stream.trailer.has_value()) {
            response.emplace(ag::http::HTTP_3_0, 200);
        } else {
            warnlog(logger, "Trailer headers haven't been received so far");
            response.emplace(ag::http::HTTP_3_0, 400);
        }
    }

    if (response.has_value()) {
        infolog(logger, "Sending response: {}", response.value());
        ag::Error<ag::http::Http3Error> error = self->server->submit_response(stream_id, response.value(), true);
        if (error != nullptr) {
            warnlog(logger, "Couldn't submit response: {}", error->str());
        }
    }
}

static void on_stream_closed(void *arg, uint64_t stream_id, int error) {
    infolog(logger, "[Stream={}] error = {}", stream_id, error);
    auto *self = (Session *) arg;
    self->streams[stream_id].closed = true;
}

static void on_close(void *arg, uint64_t error) {
    infolog(logger, "Error = {}", error);
    auto *self = (Session *) arg;
    self->parent->closing_sessions.insert(self->parent->sessions.extract(self->peer));
}

static void on_output(void *arg, const ag::http::QuicNetworkPath &path, ag::Uint8View chunk) {
    auto *self = (Session *) arg;
    ssize_t r = sendto(self->parent->fd, (char *) chunk.data(), chunk.length(), 0, path.remote, path.remote_len);
    if (r != chunk.length()) {
        int err = evutil_socket_geterror(self->parent->fd);
        warnlog(logger, "Couldn't send chunk: {} ({})", evutil_socket_error_to_string(err), err);
    }
}

static void on_expiry_update(void *arg, ag::Nanos period) {
    auto *self = (Session *) arg;
    timeval tv{};
    tv.tv_sec = std::chrono::duration_cast<ag::Secs>(period).count();
    tv.tv_usec = std::chrono::duration_cast<ag::Micros>(period).count() % 1000000;
    event_add(self->expiry_timer.get(), &tv);
}

static void wait_readable(ServerSide *self) {
    ag::UniquePtr<event, &event_free> read_event(event_new(
            self->base.get(), self->fd, EV_READ | EV_PERSIST,
            [](int, short, void *arg) {
                auto *base = (event_base *) arg;
                event_base_loopexit(base, nullptr);
            },
            self->base.get()));
    if (0 != event_add(read_event.get(), nullptr)) {
        errlog(logger, "event_add");
        abort();
    }
    if (0 != event_base_loop(self->base.get(), EVLOOP_NO_EXIT_ON_EMPTY)) {
        errlog(logger, "event_base_loop");
        abort();
    }
}

static std::optional<std::pair<ag::SocketAddress, ag::Uint8View>> read_socket(ServerSide *self) {
    ag::SocketAddress peer = self->bound_addr;
    ev_socklen_t remote_len = peer.c_socklen();

    ssize_t r = recvfrom(
            self->fd, (char *) socket_buffer, std::size(socket_buffer), 0, (sockaddr *) peer.c_sockaddr(), &remote_len);
    int err = evutil_socket_geterror(self->fd);
    if (r < 0 && !ag::utils::socket_error_is_eagain(err)) {
        return std::nullopt;
    }

    return std::pair{
            peer,
            ag::Uint8View{socket_buffer, size_t((r >= 0) ? r : 0)},
    };
}

static void register_new_session(ServerSide *self, const ag::SocketAddress &peer, ag::Uint8View packet) {
    ngtcp2_pkt_hd hd{};
    ngtcp2_ssize pkt_num_offset = ngtcp2_pkt_decode_hd_long(&hd, packet.data(), packet.size());

    if (pkt_num_offset < 0 || (hd.len + pkt_num_offset) > packet.size()) {
        dbglog(logger, "Failed to parse QUIC long header");
        ngtcp2_ssize res = ngtcp2_pkt_decode_hd_short(&hd, socket_buffer, packet.size(), 12);
        ASSERT_GT(pkt_num_offset, 0) << ngtcp2_strerror(int(res));
        dbglog(logger, "Packet is 1-RTT");
    }

    ag::UniquePtr<SSL, &SSL_free> ssl;
    ASSERT_NO_FATAL_FAILURE(make_ssl(ssl));

    Session *session = self->sessions.emplace(peer, std::make_unique<Session>(self, peer)).first->second.get();

    ag::Result make_result = ag::http::Http3Server::make(ag::http::Http3Settings{},
            ag::http::Http3Server::Callbacks{
                    .arg = session,
                    .on_request = on_request,
                    .on_trailer_headers = on_trailer_headers,
                    .on_body = on_body,
                    .on_stream_read_finished = on_stream_read_finished,
                    .on_stream_closed = on_stream_closed,
                    .on_close = on_close,
                    .on_output = on_output,
                    .on_expiry_update = on_expiry_update,
            },
            ag::http::QuicNetworkPath{
                    .local = self->bound_addr.c_sockaddr(),
                    .local_len = self->bound_addr.c_socklen(),
                    .remote = peer.c_sockaddr(),
                    .remote_len = peer.c_socklen(),
            },
            std::move(ssl), hd.scid, hd.dcid);
    ASSERT_FALSE(make_result.has_error()) << make_result.error()->str();

    session->server = std::move(make_result.value());
    ASSERT_NO_FATAL_FAILURE(session->flush());
}

const ag::SocketAddress &ServerSide::bound_address() {
    return bound_addr;
}

static void serve_connections(ServerSide *self) {
    while (true) {
        ASSERT_NO_FATAL_FAILURE(wait_readable(self));
        if (event_base_got_exit(self->base.get()) && self->state == ServerSide::STOPPED) {
            break;
        }

        while (true) {
            auto read_result = read_socket(self);
            ASSERT_TRUE(read_result.has_value()) << evutil_socket_error_to_string(evutil_socket_geterror(self->fd));

            auto &[peer, packet] = read_result.value();
            if (packet.empty()) {
                break;
            }

            dbglog(logger, "{} bytes from {}", packet.size(), peer.str());
            auto iter = self->sessions.find(peer);
            if (iter == self->sessions.end()) {
                ASSERT_NO_FATAL_FAILURE(register_new_session(self, peer, packet));
                continue;
            }

            Session *session = iter->second.get();
            ag::Result input_result = session->server->input(
                    ag::http::QuicNetworkPath{
                            .local = self->bound_addr.c_sockaddr(),
                            .local_len = self->bound_addr.c_socklen(),
                            .remote = peer.c_sockaddr(),
                            .remote_len = peer.c_socklen(),
                    },
                    packet);
            ASSERT_FALSE(input_result.has_error()) << input_result.error()->str();
            switch (input_result.value()) {
            case ag::http::Http3Server::EATEN:
                // do nothing
                break;
            case ag::http::Http3Server::SEND_RETRY: {
                ngtcp2_pkt_hd hd;
                int status = ngtcp2_accept(&hd, packet.data(), packet.size());
                ASSERT_EQ(NGTCP2_NO_ERROR, status) << ngtcp2_strerror(status);

                ag::Result retry =
                        session->server->prepare_retry(hd, peer.c_sockaddr(), peer.c_socklen(), packet.size() * 3);
                ASSERT_FALSE(retry.has_error()) << retry.error()->str();
                ASSERT_EQ(ssize_t(retry->size()),
                        sendto(self->fd, (char *) retry->data(), retry->size(), 0, peer.c_sockaddr(), peer.c_socklen()))
                        << evutil_socket_error_to_string(evutil_socket_geterror(self->fd));
                break;
            }
            }
            ASSERT_NO_FATAL_FAILURE(session->flush());

            self->closing_sessions.clear();
        }
    }
}

void ServerSide::run() {
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_GE(fd, 0) << strerror(errno);

    ASSERT_EQ(0, bind(fd, bound_addr.c_sockaddr(), bound_addr.c_socklen()))
            << evutil_socket_error_to_string(evutil_socket_geterror(fd));
    ASSERT_TRUE(ag::utils::get_local_address(fd).has_value())
            << evutil_socket_error_to_string(evutil_socket_geterror(fd));
    bound_addr = ag::utils::get_local_address(fd).value();
    infolog(logger, "Bound address: {}", bound_addr.str());

#ifdef WIN32
    u_long flags = 1;
    ASSERT_EQ(NO_ERROR, ioctlsocket(fd, FIONBIO, &flags)) << evutil_socket_error_to_string(evutil_socket_geterror(fd));
#else
    uint32_t flags = fcntl(fd, F_GETFL);
    ASSERT_NE(-1, fcntl(fd, F_SETFL, flags | O_NONBLOCK)) << evutil_socket_error_to_string(evutil_socket_geterror(fd));
#endif

    state = STARTED;

    worker_thread = std::thread([this]() {
        serve_connections(this);
    });
}

void ServerSide::stop() {
    state = STOPPED;
    event_base_loopexit(base.get(), nullptr);
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}
