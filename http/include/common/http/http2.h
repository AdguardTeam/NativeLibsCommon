#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

// Unbreak Windows build
#include "common/defs.h"

#include <event2/buffer.h>
#include <nghttp2/nghttp2.h>

#include "common/error.h"
#include "common/http/headers.h"

namespace ag {
namespace http {

struct Http2Settings {
    // Chrome/Firefox constant
    static constexpr uint32_t DEFAULT_HEADER_TABLE_SIZE = 64 * 1024;
    // Chrome constant
    static constexpr uint32_t DEFAULT_MAX_CONCURRENT_STREAMS = 1000;
    // Chrome constant
    static constexpr uint32_t DEFAULT_INITIAL_STREAM_WINDOW_SIZE = 131072;
    // Firefox constant
    static constexpr uint32_t DEFAULT_MAX_FRAME_SIZE = 1u << 14u;
    static constexpr uint32_t DEFAULT_INITIAL_SESSION_WINDOW_SIZE = 8 * 1024 * 1024;

    /**
     * Prevents sending WINDOW_UPDATE frames automatically.
     * If set to false, it is up to application to call `consume_*()` to update
     * the flow control windows.
     */
    bool auto_flow_control = false;
    /**
     * https://datatracker.ietf.org/doc/html/rfc9113#section-6.5.2
     * The maximum size of the compression table used to decode field blocks,
     * in units of octets.
     */
    uint32_t header_table_size = DEFAULT_HEADER_TABLE_SIZE;
    /**
     * https://datatracker.ietf.org/doc/html/rfc9113#section-6.5.2
     * The maximum number of concurrent streams that the sender will allow.
     */
    uint32_t max_concurrent_streams = DEFAULT_MAX_CONCURRENT_STREAMS;
    /**
     * https://datatracker.ietf.org/doc/html/rfc9113#section-6.5.2
     * The sender's initial window size (in units of octets) for
     * stream-level flow control.
     */
    uint32_t initial_stream_window_size = DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
    /**
     * The sender's initial window size (in units of octets) for
     * connection-level flow control.
     */
    uint32_t initial_session_window_size = DEFAULT_INITIAL_SESSION_WINDOW_SIZE;
    /**
     * https://datatracker.ietf.org/doc/html/rfc9113#section-6.5.2
     * The size of the largest frame payload that the sender is willing to receive,
     * in units of octets.
     */
    uint32_t max_frame_size = DEFAULT_MAX_FRAME_SIZE;
};

class Http2Server;
class Http2Client;
enum Http2Error {};

/**
 * Contains common code of client- and server-side implementations.
 * For inner use only.
 * @tparam T Either `Http2Server` or `Http2Client`
 */
template <typename T>
class Http2Session {
public:
    Http2Session() = delete;
    Http2Session(const Http2Session &) = delete;
    Http2Session &operator=(const Http2Session &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http2Session(Http2Session &&) = delete;
    Http2Session &operator=(Http2Session &&) = delete;

    ~Http2Session();

protected:
    struct DataSource {
        UniquePtr<evbuffer, &evbuffer_free> buffer;
    };

    using Message = std::conditional_t<std::is_same_v<T, Http2Server>, Request, Response>;

    struct Stream {
        enum Flags {
            MESSAGE_ALREADY_RECEIVED,
            HAS_EOF,
            SCHEDULED,
            HEAD_REQUEST,
        };

        std::optional<Message> message;
        DataSource data_source;
        EnumSet<Flags> flags;
    };

    UniquePtr<nghttp2_session, &nghttp2_session_del> m_session;
    Http2Settings m_settings;
    uint32_t m_id;
    std::unordered_map<uint32_t, Stream> m_streams;
    nghttp2_error_code m_error = NGHTTP2_NO_ERROR;

    explicit Http2Session(const Http2Settings &settings);

    Error<Http2Error> initialize_session();

    Result<size_t, Http2Error> input_impl(Uint8View chunk);
    Error<Http2Error> submit_settings_impl();
    Error<Http2Error> submit_trailer_impl(uint32_t stream_id, const Headers &headers);
    Error<Http2Error> submit_body_impl(uint32_t stream_id, Uint8View chunk, bool eof);
    Error<Http2Error> reset_stream_impl(uint32_t stream_id, nghttp2_error_code error_code);
    Error<Http2Error> consume_connection_impl(size_t length);
    Error<Http2Error> consume_stream_impl(uint32_t stream_id, size_t length);
    Error<Http2Error> flush_impl();

private:
    static int on_begin_frame(nghttp2_session *session, const nghttp2_frame_hd *hd, void *arg);
    static int on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame, void *arg);
    static int on_frame_send(nghttp2_session *session, const nghttp2_frame *frame, void *arg);
    static int on_invalid_frame_recv(
            nghttp2_session *session, const nghttp2_frame *frame, int lib_error_code, void *arg);
    static int on_data_chunk_recv(
            nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *arg);
    static int on_begin_headers(nghttp2_session *session, const nghttp2_frame *frame, void *arg);
    static int on_header(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t namelen,
            const uint8_t *value, size_t valuelen, uint8_t flags, void *arg);
    static int on_stream_close(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *arg);
    static ssize_t on_send(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *arg);
    static int on_error(nghttp2_session *session, const char *msg, size_t len, void *arg);
    static ssize_t on_data_source_read(nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length,
            uint32_t *data_flags, nghttp2_data_source *source, void *arg);

    void on_end_headers(const nghttp2_frame *frame, uint32_t stream_id, Stream &stream);
    void on_end_stream(uint32_t stream_id);
    void close_stream(uint32_t stream_id, nghttp2_error_code error_code);
    static int push_data(Stream &stream, Uint8View chunk, bool eof);
    int schedule_send(uint32_t stream_id, Stream &stream);
};

/**
 * Server-side HTTP/2 session implementation.
 * Note that GOAWAY frame is sent from the destructor, i.e. during destruction
 * the instance may raise `Http2Server::Callbacks::on_output` event.
 *
 * Example of usage:
 *
 * ```c++
 * using namespace ag::http;
 *
 * static std::optional<Request> request;
 *
 * int fd = socket(IPPROTO_TCP);
 * assert(0 == accept(fd));
 *
 * auto server = ag::http::Http2Server::make(
 *         Http2Settings{...},
 *         Http2Server::Callbacks{
 *                 .on_request = [] (void *, Request r) {
 *                     request = std::move(r);
 *                 },
 *                 .on_output = [] (void *, Uint8View chunk) {
 *                     send(fd, chunk);
 *                 },
 *         }).value();
 *
 * while (true) {
 *     int n = recv(fd, buf);
 *     assert(n == server->input({buf, n}).value());
 *     if (!request.has_value()) {
 *         assert(nullptr == server->flush());
 *         continue;
 *     }
 *     log("Received request: {}", request);
 *     Response response(HTTP_2_0, 200);
 *     assert(nullptr == server->submit_response(response, true));
 *     assert(nullptr == server->flush());
 *     break;
 * }
 * ```
 */
class Http2Server : public Http2Session<Http2Server> {
private:
    struct PrivateAccess {};

public:
    struct Callbacks {
        /** User context, will be raised in the callbacks */
        void *arg;
        /** Received request message */
        void (*on_request)(void *arg, uint32_t stream_id, Request request);
        /** Received trailer headers */
        void (*on_trailer_headers)(void *arg, uint32_t stream_id, Headers headers);
        /** Received a body chunk */
        void (*on_body)(void *arg, uint32_t stream_id, Uint8View chunk);
        /** Received a window update on the stream */
        void (*on_window_update)(void *arg, uint32_t stream_id, size_t n);
        /** The peer closed write part of the stream, do not expect further read events */
        void (*on_stream_read_finished)(void *arg, uint32_t stream_id);
        /** The peer closed the stream */
        void (*on_stream_closed)(void *arg, uint32_t stream_id, nghttp2_error_code error_code);
        /** The session is closed for some reason */
        void (*on_close)(void *arg, nghttp2_error_code error_code);
        /** The session wants to send a raw data chunk to the peer */
        void (*on_output)(void *arg, Uint8View chunk);
        /** A data chunk was transferred from the session inner buffer to the transport level */
        void (*on_data_sent)(void *arg, uint32_t stream_id, size_t n);
    };

    Http2Server(PrivateAccess, const Http2Settings &settings, const Callbacks &callbacks);

    Http2Server() = delete;
    Http2Server(const Http2Server &) = delete;
    Http2Server &operator=(const Http2Server &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http2Server(Http2Server &&) = delete;
    Http2Server &operator=(Http2Server &&) = delete;

    ~Http2Server();

    /**
     * Create an instance.
     */
    static Result<std::unique_ptr<Http2Server>, Http2Error> make(
            const Http2Settings &settings, const Callbacks &callbacks);
    /**
     * Process a raw data chunk raising necessary callbacks.
     * @return Number of bytes processed by the session if successful. Error otherwise.
     */
    Result<size_t, Http2Error> input(Uint8View chunk);
    /**
     * Submit a response to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> submit_response(uint32_t stream_id, const Response &response, bool eof);
    /**
     * Submit trailer headers to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> submit_trailer(uint32_t stream_id, const Headers &headers);
    /**
     * Submit a body chunk to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> submit_body(uint32_t stream_id, Uint8View chunk, bool eof);
    /**
     * Reset a stream.
     * @return Some error if failed, null otherwise
     */
    Error<Http2Error> reset_stream(uint32_t stream_id, nghttp2_error_code error_code);
    /**
     * Set the error code to send with GOAWAY frame when the session will be closed.
     */
    void set_session_close_error(nghttp2_error_code error_code);
    /**
     * Extend connection-level flow control window.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> consume_connection(size_t length);
    /**
     * Extend stream- and connection-level flow control windows.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> consume_stream(uint32_t stream_id, size_t length);
    /**
     * Flush data waiting to be sent into the wire.
     * Must be called after doing any action or a bunch of actions over an instance.
     * Causes raising of `Callbacks::on_output` event if there is any data pending.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> flush();

private:
    friend class Http2Session<Http2Server>;

    Callbacks m_handler;
    bool m_received_handshake = false;
};

/**
 * Client-side HTTP/2 session implementation.
 * Note that GOAWAY frame is sent from the destructor, i.e. during destruction
 * the instance may raise `Http2Client::Callbacks::on_output` event.
 *
 * Example of usage:
 *
 * ```c++
 * using namespace ag::http;
 *
 * static std::optional<Response> response;
 *
 * int fd = socket(IPPROTO_TCP);
 * assert(0 == connect(fd, peer));
 *
 * auto client = ag::http::Http2Client::make(
 *         Http2Settings{...},
 *         Http2Client::Callbacks{
 *                 .on_response = [] (void *, Response r) {
 *                     response = std::move(r);
 *                 },
 *                 .on_output = [] (void *, Uint8View chunk) {
 *                     send(fd, chunk);
 *                 },
 *         }).value();
 *
 * Request request(HTTP_2_0, "GET", "/");
 * auto stream_id = client->submit_request(request, true).value();
 * assert(nullptr == client->flush());
 * while (true) {
 *     int n = recv(fd, buf);
 *     assert(n == client->input({buf, n}).value());
 *     assert(nullptr == client->flush());
 *     if (response.has_value()) {
 *         log("Received response: {}", response);
 *         break;
 *     }
 * }
 * ```
 */
class Http2Client : public Http2Session<Http2Client> {
private:
    struct PrivateAccess {};

public:
    struct Callbacks {
        /** User context, will be raised in the callbacks */
        void *arg;
        /** Received response message */
        void (*on_response)(void *arg, uint32_t stream_id, Response response);
        /** Received trailer headers */
        void (*on_trailer_headers)(void *arg, uint32_t stream_id, Headers headers);
        /** Received a body chunk */
        void (*on_body)(void *arg, uint32_t stream_id, Uint8View chunk);
        /** Received a window update on the stream */
        void (*on_window_update)(void *arg, uint32_t stream_id, size_t n);
        /** The peer closed write part of the stream, do not expect further read events */
        void (*on_stream_read_finished)(void *arg, uint32_t stream_id);
        /** The peer closed the stream */
        void (*on_stream_closed)(void *arg, uint32_t stream_id, nghttp2_error_code error_code);
        /** The session is closed for some reason */
        void (*on_close)(void *arg, nghttp2_error_code error_code);
        /** The session wants to send a raw data chunk to the peer */
        void (*on_output)(void *arg, Uint8View chunk);
        /** A data chunk was transferred from the session inner buffer to the transport level */
        void (*on_data_sent)(void *arg, uint32_t stream_id, size_t n);
    };

    Http2Client(PrivateAccess, const Http2Settings &settings, const Callbacks &callbacks);

    Http2Client() = delete;
    Http2Client(const Http2Client &) = delete;
    Http2Client &operator=(const Http2Client &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http2Client(Http2Client &&) = delete;
    Http2Client &operator=(Http2Client &&) = delete;

    ~Http2Client();

    /**
     * Create an instance.
     */
    static Result<std::unique_ptr<Http2Client>, Http2Error> make(
            const Http2Settings &settings, const Callbacks &callbacks);
    /**
     * Process a raw data chunk raising necessary callbacks.
     * @return Number of bytes processed by the session if successful. Error otherwise.
     */
    Result<size_t, Http2Error> input(Uint8View chunk);
    /**
     * Submit a request to be sent to the peer.
     * @return Assigned stream ID if successful, an error otherwise.
     */
    Result<uint32_t, Http2Error> submit_request(const Request &request, bool eof);
    /**
     * Submit trailer headers to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> submit_trailer(uint32_t stream_id, const Headers &headers);
    /**
     * Submit a body chunk to be sent to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> submit_body(uint32_t stream_id, Uint8View chunk, bool eof);
    /**
     * Reset a stream.
     * @return Some error if failed, null otherwise
     */
    Error<Http2Error> reset_stream(uint32_t stream_id, nghttp2_error_code error_code);
    /**
     * Set the error code to send with GOAWAY frame when the session will be closed.
     */
    void set_session_close_error(nghttp2_error_code error_code);
    /**
     * Extend connection-level flow control window.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> consume_connection(size_t length);
    /**
     * Extend stream- and connection-level flow control windows.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> consume_stream(uint32_t stream_id, size_t length);
    /**
     * Flush data waiting to be sent into the wire.
     * Must be called after doing any action or a bunch of actions over an instance.
     * Causes raising of `Callbacks::on_output` event if there is any data pending.
     * @return Some error if failed, null otherwise.
     */
    Error<Http2Error> flush();

private:
    friend class Http2Session<Http2Client>;

    Callbacks m_handler;
};

} // namespace http

template <>
struct ErrorCodeToString<http::Http2Error> {
    std::string operator()(http::Http2Error) {
        return {};
    }
};

} // namespace ag
