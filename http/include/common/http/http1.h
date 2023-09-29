#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <llhttp.h>

#include "common/error.h"
#include "common/http/headers.h"

namespace ag {
namespace http {

enum Http1Error {};

/**
 * Contains common code of client- and server-side implementations.
 * For inner use only.
 * @tparam T Either `Http1Server` or `Http1Client`
 */
template <typename T>
class Http1Session {
public:
    struct InputOk {};
    struct InputUpgrade {};
    using InputResult = std::variant<InputOk, InputUpgrade>;

    Http1Session();
    Http1Session(const Http1Session &) = delete;
    Http1Session &operator=(const Http1Session &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http1Session(Http1Session &&) = delete;
    Http1Session &operator=(Http1Session &&) = delete;

    ~Http1Session();

protected:
    struct ContentLengthUnset {};
    struct ContentLengthChunked {};
    using ContentLength = std::variant<ContentLengthUnset, ContentLengthChunked, size_t>;

    struct Stream {
        enum Flags {
            REQ_SENT,
            // We are currently in body and body decoding started (if message body needs any kind of decoding)
            BODY_DATA_STARTED,
            UPGRADE_REQUEST,
            HEAD_REQUEST,
            HAS_BODY,
            INTERMEDIATE_RESPONSE,
            ZERO_CHUNK_SENT,
        };

        explicit Stream(uint32_t id)
                : id(id) {
        }
        ~Stream() = default;

        Stream(const Stream &) = default;
        Stream &operator=(const Stream &) = default;
        Stream(Stream &&) = default;
        Stream &operator=(Stream &&) = default;

        // Stream id
        uint32_t id;
        // Flags for pending actions for the stream
        EnumSet<Flags> flags;
        // Content-length of an HTTP message that currently being sent
        // or `ContentLengthChunked` (set automatically if there is "Transfer-encoding" in output headers)
        ContentLength content_length = ContentLengthUnset{};
    };

    struct ParserContext {
        Version version = HTTP_1_1;
        std::string path;
        std::string status_string;
        std::vector<std::pair<std::string, std::string>> headers;
    };

    // HTTP parser
    llhttp_t m_parser = {};
    // Connection id
    uint32_t m_id;
    // Current message context
    std::optional<ParserContext> m_parser_context;
    uint32_t m_next_stream_id = 1;
    // Active HTTP streams
    std::list<Stream> m_streams;
    // The HTTP parser settings
    llhttp_settings_t m_settings;

    Result<InputResult, Http1Error> input_impl(Uint8View chunk);
    Error<Http1Error> send_response_impl(uint64_t stream_id, const Response &response);
    Error<Http1Error> send_trailer_impl(uint32_t stream_id, const Headers &headers, bool eof);
    Error<Http1Error> send_body_impl(uint64_t stream_id, Uint8View chunk, bool eof);

private:
    void reset_parser();
    Stream &active_stream();

    static int on_message_begin(llhttp_t *parser);
    static int on_url(llhttp_t *parser, const char *at, size_t length);
    static int on_status(llhttp_t *parser, const char *at, size_t length);
    static int on_header_field(llhttp_t *parser, const char *at, size_t length);
    static int on_header_value(llhttp_t *parser, const char *at, size_t length);
    static int on_headers_complete(llhttp_t *parser);
    static int on_body(llhttp_t *parser, const char *at, size_t length);
    static int on_message_complete(llhttp_t *parser);
};

class Http1Server : public Http1Session<Http1Server> {
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
        /** Do not expect more body chunks on the stream */
        void (*on_body_finished)(void *arg, uint64_t stream_id);
        /**
         * The stream is finished, do not expect further events,
         * do not try to send more data through it
         */
        void (*on_stream_finished)(void *arg, uint64_t stream_id, int error_code);
        /** The session wants to send a raw data chunk to the peer */
        void (*on_output)(void *arg, Uint8View chunk);
    };

    using InputOk = Http1Session<Http1Server>::InputOk;
    using InputUpgrade = Http1Session<Http1Server>::InputUpgrade;
    using InputResult = Http1Session<Http1Server>::InputResult;

    explicit Http1Server(const Callbacks &handler);
    ~Http1Server();

    Http1Server(const Http1Server &) = delete;
    Http1Server &operator=(const Http1Server &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http1Server(Http1Server &&) = delete;
    Http1Server &operator=(Http1Server &&) = delete;

    /**
     * Process a raw data chunk raising necessary callbacks.
     * @return `InputOk` if successfully parsed.
     *         `InputUpgrade` if the connection protocol needs to be upgraded.
     *         Error otherwise.
     */
    Result<InputResult, Http1Error> input(Uint8View chunk);
    /**
     * Send a request to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http1Error> send_response(uint64_t stream_id, const Response &response);
    /**
     * Send trailer headers to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http1Error> send_trailer(uint32_t stream_id, const Headers &headers, bool eof);
    /**
     * Send a body chunk to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http1Error> send_body(uint64_t stream_id, Uint8View chunk, bool eof);

private:
    friend class Http1Session<Http1Server>;

    Callbacks m_handler;
};

class Http1Client : public Http1Session<Http1Client> {
public:
    struct Callbacks {
        /** User context, will be raised in the callbacks */
        void *arg;
        /** Received response message */
        void (*on_response)(void *arg, uint64_t stream_id, Response response);
        /** Received trailer headers */
        void (*on_trailer_headers)(void *arg, uint64_t stream_id, Headers headers);
        /** Received a body chunk */
        void (*on_body)(void *arg, uint64_t stream_id, Uint8View chunk);
        /** Do not expect more body chunks on the stream */
        void (*on_body_finished)(void *arg, uint64_t stream_id);
        /**
         * The stream is finished, do not expect further events,
         * do not try to send more data through it
         */
        void (*on_stream_finished)(void *arg, uint64_t stream_id, int error_code);
        /** The session wants to send a raw data chunk to the peer */
        void (*on_output)(void *arg, Uint8View chunk);
    };

    using InputOk = Http1Session<Http1Client>::InputOk;
    using InputUpgrade = Http1Session<Http1Client>::InputUpgrade;
    using InputResult = Http1Session<Http1Client>::InputResult;

    explicit Http1Client(const Callbacks &handler);
    ~Http1Client();

    Http1Client(const Http1Client &) = delete;
    Http1Client &operator=(const Http1Client &) = delete;
    // Non-movable because the implementation relies on the consistency of `this`
    Http1Client(Http1Client &&) = delete;
    Http1Client &operator=(Http1Client &&) = delete;

    /**
     * Process a raw data chunk raising necessary callbacks.
     * @return `InputOk` if successfully parsed.
     *         `InputUpgrade` if the connection protocol needs to be upgraded.
     *         Error otherwise.
     */
    Result<InputResult, Http1Error> input(Uint8View chunk);
    /**
     * Send a request to the peer.
     * @return Stream ID associated to the request if successful. Error otherwise.
     */
    Result<uint32_t, Http1Error> send_request(const Request &request);
    /**
     * Send trailer headers to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http1Error> send_trailer(uint32_t stream_id, const Headers &headers, bool eof);
    /**
     * Send a body chunk to the peer.
     * @return Some error if failed, null otherwise.
     */
    Error<Http1Error> send_body(uint64_t stream_id, Uint8View chunk, bool eof);

private:
    friend class Http1Session<Http1Client>;

    Callbacks m_handler;
};

} // namespace http

template <>
struct ErrorCodeToString<http::Http1Error> {
    std::string operator()(http::Http1Error) {
        return {};
    }
};

} // namespace ag

namespace std {
constexpr bool operator==(const ag::http::Http1Server::InputResult &lh, const ag::http::Http1Server::InputResult &rh) {
    return lh.index() == rh.index();
}
constexpr bool operator==(const ag::http::Http1Server::InputResult &lh, const ag::http::Http1Server::InputOk &) {
    return std::holds_alternative<ag::http::Http1Server::InputOk>(lh);
}
constexpr bool operator==(const ag::http::Http1Server::InputResult &lh, const ag::http::Http1Server::InputUpgrade &) {
    return std::holds_alternative<ag::http::Http1Server::InputUpgrade>(lh);
}

constexpr bool operator==(const ag::http::Http1Client::InputResult &lh, const ag::http::Http1Client::InputResult &rh) {
    return lh.index() == rh.index();
}
constexpr bool operator==(const ag::http::Http1Client::InputResult &lh, const ag::http::Http1Client::InputOk &) {
    return std::holds_alternative<ag::http::Http1Client::InputOk>(lh);
}
constexpr bool operator==(const ag::http::Http1Client::InputResult &lh, const ag::http::Http1Client::InputUpgrade &) {
    return std::holds_alternative<ag::http::Http1Client::InputUpgrade>(lh);
}
} // namespace std
