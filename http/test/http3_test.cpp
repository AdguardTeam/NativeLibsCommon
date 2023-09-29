#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
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
#include <openssl/ssl.h>

#include "common/http/http3.h"
#include "common/logger.h"
#include "common/net_utils.h"
#include "common/socket_address.h"
#include "http3_test_common.h"

static ag::Logger logger("!CLI");

class Http3Client : public ::testing::Test {
public:
    Http3Client() {
        ag::Logger::set_log_level(ag::LOG_LEVEL_TRACE);
    }

protected:
    struct Stream {
        std::optional<ag::http::Response> response;
        std::optional<ag::http::Headers> trailer;
        std::vector<uint8_t> body;
        bool read_finished = false;
        bool closed = false;
    };

    ServerSide server_side;
    ag::SocketAddress bound_addr{"127.0.0.1:0"};
    ag::UniquePtr<SSL, &SSL_free> ssl;
    ag::UniquePtr<event_base, &event_base_free> base{event_base_new()};
    bool wait_readable_timer_expired = false;
    ag::UniquePtr<event, &event_free> expiry_timer{event_new(
            base.get(), -1, EV_PERSIST,
            [](evutil_socket_t, short, void *arg) {
                auto *self = (Http3Client *) arg;
                self->session->handle_expiry();
                ag::Error<ag::http::Http3Error> error = self->session->flush();
                if (error != nullptr) {
                    errlog(logger, "Couldn't flush session: {}", error->str());
                    abort();
                }
            },
            this)};
    evutil_socket_t fd = -1;
    uint8_t socket_buffer[2 * 1024];
    ag::http::Http3Client::Callbacks handler = {
            .arg = this,
            .on_handshake_completed = on_handshake_completed,
            .on_response = on_response,
            .on_trailer_headers = on_trailer_headers,
            .on_body = on_body,
            .on_stream_read_finished = on_stream_read_finished,
            .on_stream_closed = on_stream_closed,
            .on_close = on_close,
            .on_output = on_output,
            .on_expiry_update = on_expiry_update,
    };
    std::map<uint64_t, Stream> streams;
    std::unique_ptr<ag::http::Http3Client> session;
    bool handshake_completed = false;

    void SetUp() override {
#ifdef _WIN32
        WSADATA wsa_data = {};
        ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data));
#endif

        ASSERT_NO_FATAL_FAILURE(server_side.run());

        SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_server_method());
        ASSERT_NE(ssl_ctx, nullptr) << ERR_error_string(ERR_get_error(), nullptr);
        ASSERT_EQ(ngtcp2_crypto_boringssl_configure_client_context(ssl_ctx), 0);
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
        ssl.reset(SSL_new(ssl_ctx));
        ASSERT_NE(ssl, nullptr);
        static constexpr std::string_view ALPN = NGHTTP3_ALPN_H3;
        ASSERT_EQ(0, SSL_set_alpn_protos(ssl.get(), (uint8_t *) ALPN.data(), ALPN.size()));
        SSL_set_tlsext_host_name(ssl.get(), SERVER_NAME);
        SSL_set_connect_state(ssl.get());

        if (const char *file = getenv("SSLKEYLOGFILE"); file != nullptr) {
            if (static ag::UniquePtr<std::FILE, &std::fclose> handle{std::fopen(file, "a")}; handle != nullptr) {
                SSL_CTX_set_keylog_callback(ssl_ctx, [](const SSL *, const char *line) {
                    fprintf(handle.get(), "%s\n", line);
                    fflush(handle.get());
                });
            } else {
                assert(0);
            }
        }

        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ASSERT_GE(fd, 0) << strerror(errno);

        ASSERT_EQ(0, bind(fd, bound_addr.c_sockaddr(), bound_addr.c_socklen()))
                << evutil_socket_error_to_string(evutil_socket_geterror(fd));
        ASSERT_TRUE(ag::utils::get_local_address(fd).has_value())
                << evutil_socket_error_to_string(evutil_socket_geterror(fd));
        bound_addr = ag::utils::get_local_address(fd).value();
        infolog(logger, "Bound address: {}", bound_addr.str());

        ag::Result make_result = ag::http::Http3Client::make(ag::http::Http3Settings{}, handler,
                ag::http::QuicNetworkPath{
                        .local = bound_addr.c_sockaddr(),
                        .local_len = bound_addr.c_socklen(),
                        .remote = server_side.bound_address().c_sockaddr(),
                        .remote_len = server_side.bound_address().c_socklen(),
                },
                std::move(ssl));
        ASSERT_FALSE(make_result.has_error()) << make_result.error()->str();

        session = std::move(make_result.value());
        ASSERT_NO_FATAL_FAILURE(flush_session());

#ifdef WIN32
        u_long flags = 1;
        ASSERT_EQ(NO_ERROR, ioctlsocket(fd, FIONBIO, &flags))
                << evutil_socket_error_to_string(evutil_socket_geterror(fd));
#else
        uint32_t flags = fcntl(fd, F_GETFL);
        ASSERT_NE(-1, fcntl(fd, F_SETFL, flags | O_NONBLOCK))
                << evutil_socket_error_to_string(evutil_socket_geterror(fd));
#endif

        while (!handshake_completed) {
            ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
            ASSERT_NO_FATAL_FAILURE(read_out_socket());
            ASSERT_NO_FATAL_FAILURE(flush_session());
        }
    }

    void TearDown() override {
        session.reset();
        evutil_closesocket(fd);

        ASSERT_NO_FATAL_FAILURE(server_side.stop());
    }

    static void on_handshake_completed(void *arg) {
        infolog(logger, "...");
        auto *self = (Http3Client *) arg;
        self->handshake_completed = true;
    }

    static void on_response(void *arg, uint64_t stream_id, ag::http::Response response) {
        infolog(logger, "[Stream={}] {}", stream_id, response);
        auto *self = (Http3Client *) arg;
        self->streams[stream_id].response.emplace(std::move(response));
    }

    static void on_trailer_headers(void *arg, uint64_t stream_id, ag::http::Headers headers) {
        infolog(logger, "[Stream={}] {}", stream_id, headers);
        auto *self = (Http3Client *) arg;
        self->streams[stream_id].trailer.emplace(std::move(headers));
    }

    static void on_body(void *arg, uint64_t stream_id, ag::Uint8View chunk) {
        infolog(logger, "[Stream={}] {} bytes", stream_id, chunk.size());
        auto *self = (Http3Client *) arg;
        Stream &stream = self->streams[stream_id];
        stream.body.insert(stream.body.end(), chunk.begin(), chunk.end());
        self->session->consume_stream(stream_id, chunk.size());
    }

    static void on_stream_read_finished(void *arg, uint64_t stream_id) {
        infolog(logger, "[Stream={}] ...", stream_id);
        auto *self = (Http3Client *) arg;
        self->streams[stream_id].read_finished = true;
    }

    static void on_stream_closed(void *arg, uint64_t stream_id, int error) {
        infolog(logger, "[Stream={}] error = {}", stream_id, error);
        auto *self = (Http3Client *) arg;
        self->streams[stream_id].closed = true;
    }

    static void on_close(void *arg, uint64_t error) {
        infolog(logger, "Error = {}", error);
        auto *self = (Http3Client *) arg;
        event_base_loopexit(self->base.get(), nullptr);
    }

    static void on_output(void *arg, const ag::http::QuicNetworkPath &path, ag::Uint8View chunk) {
        auto *self = (Http3Client *) arg;
        ssize_t r = sendto(self->fd, (char *) chunk.data(), chunk.length(), 0, path.remote, path.remote_len);
        if (r != chunk.length()) {
            int err = evutil_socket_geterror(self->fd);
            warnlog(logger, "Couldn't send chunk: {} ({})", evutil_socket_error_to_string(err), err);
        }
    }

    static void on_expiry_update(void *arg, ag::Nanos period) {
        auto *self = (Http3Client *) arg;
        timeval tv{};
        tv.tv_sec = std::chrono::duration_cast<ag::Secs>(period).count();
        tv.tv_usec = std::chrono::duration_cast<ag::Micros>(period).count() % 1000000;
        event_add(self->expiry_timer.get(), &tv);
    }

    void wait_readable(ag::Micros timeout) {
        ag::UniquePtr<event, &event_free> timer{event_new(
                base.get(), -1, EV_PERSIST,
                [](evutil_socket_t, short, void *arg) {
                    auto *self = (Http3Client *) arg;
                    self->wait_readable_timer_expired = true;
                    event_base_loopexit(self->base.get(), nullptr);
                },
                this)};
        timeval tv{};
        tv.tv_sec = std::chrono::duration_cast<ag::Secs>(timeout).count();
        tv.tv_usec = std::chrono::duration_cast<ag::Micros>(timeout).count() % 1000000;
        ASSERT_EQ(0, event_add(timer.get(), &tv));

        ag::UniquePtr<event, &event_free> read_event(event_new(
                base.get(), fd, EV_READ | EV_PERSIST,
                [](int, short, void *arg) {
                    auto *base = (event_base *) arg;
                    event_base_loopexit(base, nullptr);
                },
                base.get()));
        ASSERT_EQ(0, event_add(read_event.get(), nullptr));
        ASSERT_EQ(0, event_base_loop(base.get(), EVLOOP_NO_EXIT_ON_EMPTY));
        ASSERT_FALSE(wait_readable_timer_expired);
    }

    void read_out_socket() {
        sockaddr_storage remote{};
        ev_socklen_t remote_len = sizeof(remote);

        static bool first_error = true;

        while (true) {
            ssize_t r = recvfrom(
                    fd, (char *) socket_buffer, std::size(socket_buffer), 0, (sockaddr *) &remote, &remote_len);
            int err = evutil_socket_geterror(fd);
            if (r < 0 && ag::utils::socket_error_is_eagain(err)) {
                break;
            }
            ASSERT_GT(r, 0) << evutil_socket_error_to_string(err);

            ag::Error<ag::http::Http3Error> error = session->input(
                    ag::http::QuicNetworkPath{
                            .local = bound_addr.c_sockaddr(),
                            .local_len = bound_addr.c_socklen(),
                            .remote = (sockaddr *) &remote,
                            .remote_len = remote_len,
                    },
                    {socket_buffer, size_t(r)});
            ASSERT_EQ(error, nullptr) << error->str();
        }
    }

    void flush_session() {
        ag::Error<ag::http::Http3Error> error = session->flush();
        ASSERT_EQ(error, nullptr) << error->str();
    }
};

TEST_F(Http3Client, Exchange) {
    ag::http::Request request(ag::http::HTTP_3_0, "GET", "/");
    request.authority(SERVER_NAME);
    request.scheme("https");
    ag::Result request_result = session->submit_request(request, true);
    ASSERT_TRUE(request_result.has_value()) << request_result.error()->str();
    streams[request_result.value()] = {};
    ASSERT_NO_FATAL_FAILURE(flush_session());

    while (true) {
        ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
        ASSERT_NO_FATAL_FAILURE(read_out_socket());

        ASSERT_LE(streams.size(), 1);
        if (streams.empty()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        const auto &[stream_id, stream] = *streams.begin();
        ASSERT_EQ(stream.closed, stream.response.has_value());
        if (!stream.response.has_value()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        ASSERT_EQ(stream.response->status_code(), 200) << stream.response->str();
        break;
    }
}

TEST_F(Http3Client, IncomingTrailer) {
    ag::http::Request request(ag::http::HTTP_3_0, "GET", TRAILER_REQUEST_PATH);
    request.authority(SERVER_NAME);
    request.scheme("https");
    ag::Result request_result = session->submit_request(request, true);
    ASSERT_TRUE(request_result.has_value()) << request_result.error()->str();
    ASSERT_NO_FATAL_FAILURE(flush_session());

    while (true) {
        ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
        ASSERT_NO_FATAL_FAILURE(read_out_socket());

        ASSERT_LE(streams.size(), 1);
        if (streams.empty()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        const Stream &stream = streams.begin()->second;
        ASSERT_EQ(stream.read_finished, stream.response.has_value() && stream.trailer.has_value());
        if (!stream.read_finished) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        ASSERT_EQ(stream.response->status_code(), 200) << stream.response->str();
        ASSERT_EQ(stream.trailer->str(), "foo: bar\r\n");
        break;
    }
}

TEST_F(Http3Client, OutgoingTrailer) {
    ag::http::Request request(ag::http::HTTP_3_0, "POST", TRAILER_REQUEST_PATH);
    request.authority(SERVER_NAME);
    request.scheme("https");
    ag::Result request_result = session->submit_request(request, false);
    ASSERT_TRUE(request_result.has_value()) << request_result.error()->str();

    uint64_t stream_id = request_result.value();
    streams[stream_id] = {};
    ag::http::Headers headers;
    headers.put("foo", "bar");
    ag::Error<ag::http::Http3Error> error = session->submit_trailer(stream_id, headers);
    ASSERT_EQ(error, nullptr) << error->str();

    ASSERT_NO_FATAL_FAILURE(flush_session());

    while (true) {
        ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
        ASSERT_NO_FATAL_FAILURE(read_out_socket());

        ASSERT_LE(streams.size(), 1);
        if (streams.empty()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        const Stream &stream = streams.begin()->second;
        ASSERT_EQ(stream.read_finished, stream.response.has_value());
        if (!stream.response.has_value()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        ASSERT_EQ(stream.response->status_code(), 200) << stream.response->str();
        break;
    }
}

TEST_F(Http3Client, Download) {
    ag::http::Request request(ag::http::HTTP_3_0, "GET", DOWNLOAD_REQUEST_PATH);
    request.authority(SERVER_NAME);
    request.scheme("https");
    ag::Result request_result = session->submit_request(request, true);
    ASSERT_TRUE(request_result.has_value()) << request_result.error()->str();
    streams[request_result.value()] = {};
    ASSERT_NO_FATAL_FAILURE(flush_session());

    while (true) {
        ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
        ASSERT_NO_FATAL_FAILURE(read_out_socket());

        ASSERT_LE(streams.size(), 1);
        if (streams.empty()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        const auto &[stream_id, stream] = *streams.begin();
        ASSERT_EQ(stream.closed, stream.body.size() == DOWNLOAD_SIZE) << stream.body.size();
        if (!stream.response.has_value() || stream.body.size() < DOWNLOAD_SIZE) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        ASSERT_EQ(stream.response->status_code(), 200) << stream.response->str();
        break;
    }
}

TEST_F(Http3Client, Upload) {
    ag::http::Request request(ag::http::HTTP_3_0, "POST", UPLOAD_REQUEST_PATH);
    request.authority(SERVER_NAME);
    request.scheme("https");
    ag::Result request_result = session->submit_request(request, false);
    ASSERT_TRUE(request_result.has_value()) << request_result.error()->str();
    uint64_t stream_id = request_result.value();
    streams[stream_id] = {};
    std::vector<uint8_t> chunk(DOWNLOAD_SIZE);
    ag::Error<ag::http::Http3Error> error = session->submit_body(stream_id, {chunk.data(), chunk.size()}, true);
    ASSERT_EQ(error, nullptr) << error->str();
    ASSERT_NO_FATAL_FAILURE(flush_session());

    while (true) {
        ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
        ASSERT_NO_FATAL_FAILURE(read_out_socket());

        ASSERT_LE(streams.size(), 1);
        if (streams.empty()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        const Stream &stream = streams.begin()->second;
        ASSERT_EQ(stream.read_finished, stream.response.has_value());
        if (!stream.response.has_value()) {
            ASSERT_NO_FATAL_FAILURE(flush_session());
            continue;
        }

        ASSERT_EQ(stream.response->status_code(), 200) << stream.response->str();
        break;
    }
}

TEST_F(Http3Client, MaxStreamsNumberDontLeak) {
    ag::http::Request request(ag::http::HTTP_3_0, "GET", "/");
    request.authority(SERVER_NAME);
    request.scheme("https");

    for (size_t i = 0; i < 4 * ag::http::Http3Settings::DEFAULT_INITIAL_MAX_STREAMS_BIDI; ++i) {
        ag::Result request_result = session->submit_request(request, true);
        ASSERT_TRUE(request_result.has_value()) << request_result.error()->str() << " " << streams.size();
        ASSERT_NO_FATAL_FAILURE(flush_session());

        ASSERT_NO_FATAL_FAILURE(wait_readable(ag::Secs{5}));
        ASSERT_NO_FATAL_FAILURE(read_out_socket());
    }
}
