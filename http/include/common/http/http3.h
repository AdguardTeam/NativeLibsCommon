#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include <event2/buffer.h>
#include <event2/util.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>

#include "common/defs.h"
#include "common/error.h"
#include "common/http/headers.h"

namespace ag {

namespace http {

// OpenSSL alerts will be prepended with 1000 to distinguish from QUIC and HTTP/3 error codes
static constexpr auto SSL_ALERT_CODES_START = 1000;

struct Http3Settings {
    enum CongestionControlAlgorithm {
        RENO,
        CUBIC,
        BBR,
    };

    // Chrome constant
    static constexpr size_t DEFAULT_MAX_TX_UDP_PAYLOAD_SIZE = 1350;
    static constexpr uint64_t DEFAULT_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL = 256ul * 1024;
    static constexpr uint64_t DEFAULT_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE = 256ul * 1024;
    static constexpr uint64_t DEFAULT_INITIAL_MAX_STREAM_DATA_UNI = 256ul * 1024;
    static constexpr uint64_t DEFAULT_INITIAL_MAX_DATA = 8ul * 1024 * 1024;
    static constexpr uint64_t DEFAULT_INITIAL_MAX_STREAMS_BIDI = 1ul * 1024;
    static constexpr uint64_t DEFAULT_INITIAL_MAX_STREAMS_UNI = 3;
    static constexpr auto DEFAULT_MAX_IDLE_TIMEOUT = ag::Secs(180);

    /**
     * Specifies congestion control algorithm.
     */
    CongestionControlAlgorithm congestion_control_algorithm = CUBIC;
    /**
     * The maximum size of UDP datagram payload that the local endpoint transmits.
     * It is used by congestion controller to compute congestion window.
     */
    size_t max_tx_udp_payload_size = DEFAULT_MAX_TX_UDP_PAYLOAD_SIZE;
    /**
     * The size of flow control window of locally initiated stream.
     * This is the number of bytes that the remote endpoint can send, and
     * the local endpoint must ensure that it has enough buffer to receive them.
     */
    uint64_t initial_max_stream_data_bidi_local = DEFAULT_INITIAL_MAX_STREAM_DATA_BIDI_LOCAL;
    /**
     * The size of flow control window of remotely initiated stream.
     * This is the number of bytes that the remote endpoint can send, and
     * the local endpoint must ensure that it has enough buffer to receive them.
     */
    uint64_t initial_max_stream_data_bidi_remote = DEFAULT_INITIAL_MAX_STREAM_DATA_BIDI_REMOTE;
    /**
     * The size of flow control window of remotely initiated unidirectional stream.
     * This is the number of bytes that the remote endpoint can send, and the local
     * endpoint must ensure that it has enough buffer to receive them.
     */
    uint64_t initial_max_stream_data_uni = DEFAULT_INITIAL_MAX_STREAM_DATA_UNI;
    /**
     * The connection level flow control window.
     */
    uint64_t initial_max_data = DEFAULT_INITIAL_MAX_DATA;
    /**
     * The number of concurrent streams that the remote endpoint can create.
     */
    uint64_t initial_max_streams_bidi = DEFAULT_INITIAL_MAX_STREAMS_BIDI;
    /**
     * The number of concurrent unidirectional streams that the remote endpoint can create.
     *
     * https://datatracker.ietf.org/doc/html/draft-ietf-quic-http-34#section-6.2
     * Note that to avoid blocking, the transport parameters sent
     * by both clients and servers MUST allow the peer to create at least
     * one unidirectional stream for the HTTP control stream plus the number
     * of unidirectional streams required by mandatory extensions (three
     * being the minimum number required for the base HTTP/3 protocol and
     * QPACK).
     */
    uint64_t initial_max_streams_uni = DEFAULT_INITIAL_MAX_STREAMS_UNI;
    /**
     * A duration during which sender allows quiescent.
     */
    ag::Micros max_idle_timeout = DEFAULT_MAX_IDLE_TIMEOUT;
};

struct QuicNetworkPath {
    const sockaddr *local;
    ev_socklen_t local_len;
    const sockaddr *remote;
    ev_socklen_t remote_len;
};

class Http3Server;
class Http3Client;
enum class Http3Error {};

/**
 * Contains common code of client- and server-side implementations.
 * For inner use only.
 * @tparam T Either `Http3Server` or `Http3Client`
 */
template <typename T>
class Http3Session {
public:
    Http3Session(const Http3Session &) = delete;
    Http3Session &operator=(const Http3Session &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http3Session(Http3Session &&) = delete;
    Http3Session &operator=(Http3Session &&) = delete;

    ~Http3Session();

protected:
    using Message = std::conditional_t<std::is_same_v<T, Http3Server>, Request, Response>;

    explicit Http3Session(const Http3Settings &settings);

    Error<Http3Error> initialize_session(
            const QuicNetworkPath &path, ag::UniquePtr<SSL, &SSL_free> ssl, ngtcp2_cid client_scid, ngtcp2_cid client_dcid);

    int input_impl(const QuicNetworkPath &path, Uint8View chunk);
    Error<Http3Error> submit_trailer_impl(uint64_t stream_id, const Headers &headers);
    Error<Http3Error> submit_body_impl(uint64_t stream_id, Uint8View chunk, bool eof);
    Error<Http3Error> reset_stream_impl(uint64_t stream_id, int error_code);
    Error<Http3Error> consume_connection_impl(size_t length);
    Error<Http3Error> consume_stream_impl(uint64_t stream_id, size_t length);
    Error<Http3Error> handle_expiry_impl();
    Error<Http3Error> flush_impl();

    struct DataSource {
        UniquePtr<evbuffer, &evbuffer_free> buffer;
        size_t read_offset = 0;
    };

    struct Stream {
        enum Flags {
            HAS_EOF,
            TRAILERS_SUBMITTED,
            HEAD_REQUEST,
        };

        std::optional<Message> message;
        EnumSet<Flags> flags;
        DataSource data_source;
    };

    uint32_t m_id;
    UniquePtr<ngtcp2_conn, &ngtcp2_conn_del> m_quic_conn;
    UniquePtr<nghttp3_conn, &nghttp3_conn_del> m_http_conn;
    ngtcp2_crypto_conn_ref m_ref;
    ag::UniquePtr<SSL, &SSL_free> m_ssl;
    std::unordered_map<uint64_t, Stream> m_streams;
    Http3Settings m_settings;
    ngtcp2_ccerr m_last_error{};
    bool m_handshake_completed = false;
    bool m_handled_rx_connection_close = false;
    bool m_connection_close_sent = false;
    // Used to derive keying materials for Stateless Reset and, on server side, Retry tokens
    std::array<uint8_t, 32> m_static_secret{};

    static int on_begin_headers(nghttp3_conn *conn, int64_t stream_id, void *user_data, void *stream_data);
    static int on_recv_header(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name,
            nghttp3_rcbuf *value, uint8_t flags, void *arg, void *stream_data);
    static int on_end_headers(nghttp3_conn *conn, int64_t stream_id, int fin, void *arg, void *stream_data);
    static int on_begin_trailers(nghttp3_conn *conn, int64_t stream_id, void *arg, void *stream_data);
    static int on_recv_trailer(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name,
            nghttp3_rcbuf *value, uint8_t flags, void *arg, void *stream_data);
    static int on_end_trailers(nghttp3_conn *conn, int64_t stream_id, int fin, void *arg, void *stream_data);
    static int on_data_chunk_recv(
            nghttp3_conn *conn, int64_t stream_id, const uint8_t *data, size_t len, void *arg, void *stream_data);
    static int on_h3_stop_sending(
            nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *arg, void *stream_data);
    static int on_quic_stream_stop_sending(
            ngtcp2_conn *conn, int64_t stream_id, uint64_t app_error_code, void *arg, void *stream_data);
    static int on_end_stream(nghttp3_conn *conn, int64_t stream_id, void *arg, void *stream_data);
    static int on_h3_reset_stream(
            nghttp3_conn *conn, int64_t stream_id, uint64_t app_error_code, void *arg, void *stream_data);
    static int on_quic_stream_reset(ngtcp2_conn *conn, int64_t stream_id, uint64_t final_size, uint64_t app_error_code,
            void *arg, void *stream_data);
    static int on_quic_stream_close(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id, uint64_t app_error_code,
            void *arg, void *stream_data);
    static int on_h3_stream_close(
            nghttp3_conn *conn, int64_t stream_id, uint64_t error_code, void *arg, void *stream_data);
    void close_stream(uint32_t stream_id, int error_code);
    static int on_handshake_completed(ngtcp2_conn *conn, void *arg);
    static void log_quic(void *arg, const char *format, ...);
    int recv_h3_stream_data(int64_t stream_id, Uint8View chunk, bool eof);
    Error<Http3Error> push_data(Stream &stream, Uint8View chunk, bool eof);
    static nghttp3_ssize on_read_data(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t veccnt,
            uint32_t *pflags, void *arg, void *stream_data);
    static int on_acked_stream_data(
            nghttp3_conn *conn, int64_t stream_id, uint64_t datalen, void *arg, void *stream_data);
    static int on_deferred_consume(
            nghttp3_conn *conn, int64_t stream_id, size_t consumed, void *arg, void *stream_data);
    void handle_rx_connection_close();
    void handle_error();
    void close_connection();
    [[nodiscard]] bool derive_token(Uint8View in, Uint8View out) const;
};

/**
 * Server-side HTTP3 session implementation.
 * Note that CONNECTION_CLOSE frame is sent from the destructor, i.e. during destruction
 * the instance may raise `Http3Server::Callbacks::on_output` event.
 *
 * Example of usage:
 *
 * ```c++
 * using namespace ag::http;
 *
 * static std::optional<uint64_t> stream_id;
 * static std::optional<Request> request;
 * static int fd = socket(IPPROTO_UDP);
 * static sockaddr_storage peer = {};
 *
 * int n = recvfrom(fd, buf, &peer);
 * auto server = ag::http::Http3Server::accept(
 *         Http3Settings{...},
 *         Http3Server::Callbacks{
 *                 .on_request = [] (void *, uint64_t sid, Request r) {
 *                     stream_id = sid;
 *                     request = std::move(r);
 *                 },
 *                 .on_output = [] (void *, Uint8View chunk) {
 *                     sendto(fd, chunk, peer);
 *                 },
 *         },
 *         QuicNetworkPath{
 *                 .local = getsockname(fd),
 *                 .remote = peer,
 *         },
 *         make_ssl(),
 *         {buf, n}).value();
 * assert(server->input({buf, n}).has_value());
 * assert(nullptr == server->flush());
 *
 * while (true) {
 *     int n = recvfrom(fd, buf, &peer);
 *     assert(server->input({buf, n}).has_value());
 *     if (!request.has_value()) {
 *         assert(nullptr == server->flush());
 *         continue;
 *     }
 *
 *     log("Received request: {}", request.value());
 *     Response response(HTTP_3_0, 200);
 *     assert(nullptr == server->submit_response(stream_id.value(), response, true));
 *     assert(nullptr == server->flush());
 *     break;
 * }
 * ```
 */
class Http3Server : public Http3Session<Http3Server> {
private:
    struct PrivateAccess {};

public:
    struct Callbacks {
        /** User context, will be raised in the callbacks */
        void *arg;
        /** Received request message */
        void (*on_request)(void *arg, uint64_t stream_id, Request request);
        /** Received trailer headers */
        void (*on_trailer_headers)(void *arg, uint64_t stream_id, Headers headers);
        /** Received a body chunk */
        void (*on_body)(void *arg, uint64_t stream_id, Uint8View chunk);
        /** Received a window update on the stream */
        void (*on_window_update)(void *arg, uint64_t stream_id, size_t n);
        /** The peer closed write part of the stream, do not expect further read events */
        void (*on_stream_read_finished)(void *arg, uint64_t stream_id);
        /** The peer closed the stream */
        void (*on_stream_closed)(void *arg, uint64_t stream_id, int error_code);
        /** The session is closed for some reason */
        void (*on_close)(void *arg, uint64_t error_code);
        /** The session wants to send a raw data chunk to the peer */
        void (*on_output)(void *arg, const QuicNetworkPath &path, Uint8View chunk);
        /** A data chunk was transferred from the session inner buffer to the transport level */
        void (*on_data_sent)(void *arg, uint64_t stream_id, size_t n);
        /** The next expiry deadline has been updated */
        void (*on_expiry_update)(void *arg, ag::Nanos period);
        /** More streams available from client */
        void (*on_available_streams)(void *arg);
    };

    enum InputResult {
        EATEN,
        SEND_RETRY,
    };

    Http3Server(PrivateAccess, const Http3Settings &settings, const Callbacks &handler);

    Http3Server() = delete;
    Http3Server(const Http3Server &) = delete;
    Http3Server &operator=(const Http3Server &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http3Server(Http3Server &&) = delete;
    Http3Server &operator=(Http3Server &&) = delete;

    ~Http3Server();

    /**
     * Create an instance.
     * In case the packet is accepted, it is expected that the application
     * calls `input()` with the same packet and `flush()` after that.
     */
    static Result<std::unique_ptr<Http3Server>, Http3Error> accept(const Http3Settings &settings,
            const Callbacks &handler, const QuicNetworkPath &path, ag::UniquePtr<SSL, &SSL_free> ssl, Uint8View packet);
    /**
     * Forge retry packet to send to peer.
     * @return Forged packet if successful, error otherwise.
     */
    [[nodiscard]] Result<std::vector<uint8_t>, Http3Error> prepare_retry(
            const ngtcp2_pkt_hd &hd, const sockaddr *remote, size_t remote_len, size_t max_packet_len) const;
    /**
     * Process a raw data chunk raising necessary callbacks.
     * @return Some error if failed, null otherwise.
     */
    Result<InputResult, Http3Error> input(const QuicNetworkPath &path, Uint8View chunk);
    /**
     * Submit a response to be sent to the peer.
     * @return `EATEN` if packet has been processed successfully,
     *         `SEND_RETRY` if the next higher level should send retry packet
     *         back to the peer (see `prepare_retry()`),
     *         error otherwise, the next higher layer should destroy the instance
     *         after the probe timer expiration (see `probe_timeout()`).
     */
    Error<Http3Error> submit_response(uint64_t stream_id, const Response &response, bool eof);
    /**
     * Submit trailer headers to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> submit_trailer(uint64_t stream_id, const Headers &headers);
    /**
     * Submit a body chunk to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> submit_body(uint64_t stream_id, Uint8View chunk, bool eof);
    /**
     * Reset a stream.
     * @return Some error if failed, null otherwise
     */
    Error<Http3Error> reset_stream(uint64_t stream_id, int error_code);
    /**
     * Set the error code and reason to send with CONNECTION_CLOSE frame when
     * the session will be closed.
     */
    void set_session_close_error(int error_code, Uint8View reason);
    /**
     * Extend connection-level flow control window.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> consume_connection(size_t length);
    /**
     * Extend stream- and connection-level flow control windows.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> consume_stream(uint64_t stream_id, size_t length);
    /**
     * Notify the session of expiry deadline has come.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> handle_expiry();
    /**
     * Flush data waiting to be sent into the wire.
     * Must be called after doing any action or a bunch of actions over an instance.
     * Must not be called from any of the `Callbacks` handlers.
     * Causes raising of `Callbacks::on_output` event if there is any data pending.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> flush();
    /**
     * Get the period for which the session should be kept alive before deletion.
     * It is supposed to be called after receiving the `Callbacks::on_close` event
     * or an error from the `input()` method.
     */
    [[nodiscard]] Nanos probe_timeout() const;

private:
    friend class Http3Session<Http3Server>;

    Callbacks m_handler;
    bool m_drop_silently = false;
};

/**
 * Client-side HTTP/3 session implementation.
 * Note that CONNECTION_CLOSE frame is sent from the destructor, i.e. during destruction
 * the instance may raise `Http3Client::Callbacks::on_output` event.
 *
 * Example of usage:
 *
 * ```c++
 * using namespace ag::http;
 *
 * static std::optional<Response> response;
 * static int fd = socket(IPPROTO_UDP);
 *
 * assert(0 == connect(fd, peer));
 *
 * auto client = ag::http::Http3Client::connect(
 *         Http3Settings{...},
 *         Http3Client::Callbacks{
 *                 .on_handshake_completed = [] (void *) {
 *                     handshake_completed = true;
 *                 },
 *                 .on_response = [] (void *, Response r) {
 *                     response = std::move(r);
 *                 },
 *                 .on_output = [] (void *, Uint8View chunk) {
 *                     send(fd, chunk);
 *                 },
 *         },
 *         QuicNetworkPath{
 *                 .local = getsockname(fd),
 *                 .remote = peer,
 *         },
 *         make_ssl()).value();
 * assert(nullptr == client->flush());
 *
 * enum State {
 *     HANDSHAKE,
 *     WAITING_RESPONSE,
 * };
 *
 * State state = HANDSHAKE;
 *
 * while (true) {
 *     int n = recv(fd, buf);
 *     assert(nullptr == client->input({buf, n}));
 *     switch (state) {
 *     case HANDSHAKE:
 *         if (!handshake_completed) {
 *             continue;
 *         }
 *         Request request(HTTP_3_0, "GET", "/");
 *         auto stream_id = client->submit_request(request, true).value();
 *         state = WAITING_RESPONSE;
 *         break;
 *     case WAITING_RESPONSE:
 *         if (response.has_value()) {
 *             log("Received response: {}", response);
 *             goto loop_exit;
 *         }
 *         break;
 *     }
 *     assert(nullptr == client->flush());
 * }
 * loop_exit:
 * ```
 */
class Http3Client : public Http3Session<Http3Client> {
private:
    struct PrivateAccess {};

public:
    struct Callbacks {
        /** User context, will be raised in the callbacks */
        void *arg;
        /** Handshake stage is completed, the session is ready for exchange with the peer */
        void (*on_handshake_completed)(void *arg);
        /** Received response message */
        void (*on_response)(void *arg, uint64_t stream_id, Response response);
        /** Received trailer headers */
        void (*on_trailer_headers)(void *arg, uint64_t stream_id, Headers headers);
        /** Received a body chunk */
        void (*on_body)(void *arg, uint64_t stream_id, Uint8View chunk);
        /** Received a window update on the stream */
        void (*on_window_update)(void *arg, uint64_t stream_id, size_t n);
        /** The peer closed write part of the stream, do not expect further read events */
        void (*on_stream_read_finished)(void *arg, uint64_t stream_id);
        /** The peer closed the stream */
        void (*on_stream_closed)(void *arg, uint64_t stream_id, int error_code);
        /** The session is closed for some reason */
        void (*on_close)(void *arg, uint64_t error_code);
        /** The session wants to send a raw data chunk to the peer */
        void (*on_output)(void *arg, const QuicNetworkPath &path, Uint8View chunk);
        /** A data chunk was transferred from the session inner buffer to the transport level */
        void (*on_data_sent)(void *arg, uint64_t stream_id, size_t n);
        /** The next expiry deadline has been updated */
        void (*on_expiry_update)(void *arg, ag::Nanos period);
        /** More streams available from server */
        void (*on_available_streams)(void *arg);
    };

    Http3Client(PrivateAccess, const Http3Settings &settings, const Callbacks &handler);

    Http3Client() = delete;
    Http3Client(const Http3Client &) = delete;
    Http3Client &operator=(const Http3Client &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http3Client(Http3Client &&) = delete;
    Http3Client &operator=(Http3Client &&) = delete;

    ~Http3Client();

    /**
     * Create an instance.
     * In case the call succeded, it is expected that the application
     * calls `flush()` after that.
     */
    static Result<std::unique_ptr<Http3Client>, Http3Error> connect(const Http3Settings &settings,
            const Callbacks &handler, const QuicNetworkPath &path, ag::UniquePtr<SSL, &SSL_free> ssl);
    /**
     * Process a raw data chunk raising necessary callbacks.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> input(const QuicNetworkPath &path, Uint8View chunk);
    /**
     * Submit a request to be sent to the peer.
     * @return Assigned stream ID if successful, an error otherwise.
     */
    Result<uint64_t, Http3Error> submit_request(const Request &request, bool eof);
    /**
     * Submit trailer headers to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> submit_trailer(uint64_t stream_id, const Headers &headers);
    /**
     * Submit a body chunk to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> submit_body(uint64_t stream_id, Uint8View chunk, bool eof);
    /**
     * Reset a stream.
     * @return Some error if failed, null otherwise
     */
    Error<Http3Error> reset_stream(uint64_t stream_id, int error_code);
    /**
     * Set the error code and reason to send with CONNECTION_CLOSE frame when
     * the session will be closed.
     */
    void set_session_close_error(int error_code, Uint8View reason);
    /**
     * Extend connection-level flow control window.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> consume_connection(size_t length);
    /**
     * Extend stream- and connection-level flow control windows.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> consume_stream(uint64_t stream_id, size_t length);
    /**
     * Notify the session of expiry deadline has come.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> handle_expiry();
    /**
     * Flush data waiting to be sent into the wire.
     * Must be called after doing any action or a bunch of actions over an instance.
     * Must not be called from any of the `Callbacks` handlers.
     * Causes raising of `Callbacks::on_output` event if there is any data pending.
     * @return Some error if failed, null otherwise.
     */
    Error<Http3Error> flush();
    /**
     * Get the period for which the session should be kept alive before deletion.
     * It is supposed to be called after receiving the `Callbacks::on_close` event
     * or an error from the `input()` method.
     */
    [[nodiscard]] Nanos probe_timeout() const;

private:
    friend class Http3Session<Http3Client>;

    Callbacks m_handler;
};

} // namespace http

template <>
struct ErrorCodeToString<http::Http3Error> {
    std::string operator()(http::Http3Error);
};

} // namespace ag
