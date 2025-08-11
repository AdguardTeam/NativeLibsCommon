#include <atomic>

#include <magic_enum/magic_enum.hpp>

#include "common/http/http1.h"
#include "common/logger.h"
#include "common/utils.h"

#define log_id(lvl_, id_, fmt_, ...) lvl_##log(g_logger, "[id={}] " fmt_, id_, ##__VA_ARGS__)
#define log_sid(lvl_, id_, stream_, fmt_, ...) lvl_##log(g_logger, "[id={}={}] " fmt_, id_, stream_, ##__VA_ARGS__)

namespace ag::http {

static const Logger g_logger("H1");    // NOLINT(*-identifier-naming)
static std::atomic_uint32_t g_next_id; // NOLINT(*-avoid-non-const-global-variables)
static constexpr std::string_view CHUNK_FOOTER = "\r\n";

template <typename T>
Http1Session<T>::Http1Session()
        : m_id(g_next_id.fetch_add(1, std::memory_order_relaxed))
        , m_settings({
                  .on_message_begin = on_message_begin,
                  .on_url = on_url,
                  .on_status = on_status,
                  .on_header_field = on_header_field,
                  .on_header_value = on_header_value,
                  .on_headers_complete = on_headers_complete,
                  .on_body = on_body,
                  .on_message_complete = on_message_complete,
          }) {
    if constexpr (std::is_same_v<T, Http1Server>) {
        llhttp_init(&m_parser, HTTP_REQUEST, &m_settings);
    } else {
        llhttp_init(&m_parser, HTTP_RESPONSE, &m_settings);
    }
    m_parser.data = this;
}

template <typename T>
Http1Session<T>::~Http1Session() = default;

template <typename T>
int Http1Session<T>::on_message_begin(llhttp_t *parser) {
    auto *self = (Http1Session<T> *) parser->data;

    if constexpr (std::is_same_v<T, Http1Server>) {
        const Stream &stream = self->m_streams.emplace_back(self->m_next_stream_id++);
        log_sid(trace, self->m_id, stream.id, "...");
    } else {
        if (self->m_streams.empty()) {
            log_id(dbg, self->m_id, "There're no active streams");
            return -1;
        }

        Stream &stream = self->active_stream();
        log_sid(trace, self->m_id, stream.id, "...");

        stream.flags.reset(Stream::HAS_BODY);
        stream.flags.reset(Stream::INTERMEDIATE_RESPONSE);
    }

    self->m_parser_context.emplace();

    return 0;
}

template <typename T>
int Http1Session<T>::on_url(llhttp_t *parser, const char *at, size_t length) {
    auto *self = (Http1Session<T> *) parser->data;
    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    if constexpr (std::is_same_v<T, Http1Client>) {
        log_id(warn, self->m_id, "Unexpected URL in response");
        return -1;
    }

    const Stream &stream = self->active_stream();
    std::string_view path = {at, length};
    log_sid(trace, self->m_id, stream.id, "{}", path);

    if (!self->m_parser_context.has_value()) {
        log_sid(dbg, self->m_id, stream.id, "Parser context isn't initialized");
        return -1;
    }

    ParserContext &context = self->m_parser_context.value();
    context.path.append(path);
    return 0;
}

template <typename T>
int Http1Session<T>::on_status(llhttp_t *parser, const char *at, size_t length) {
    auto *self = (Http1Session<T> *) parser->data;
    if constexpr (std::is_same_v<T, Http1Server>) {
        log_id(warn, self->m_id, "Unexpected status in request");
        return -1;
    }

    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    const Stream &stream = self->active_stream();
    std::string_view status = {at, length};
    log_sid(trace, self->m_id, stream.id, "{}", status);

    if (!self->m_parser_context.has_value()) {
        log_sid(dbg, self->m_id, stream.id, "Parser context isn't initialized");
        return -1;
    }

    ParserContext &context = self->m_parser_context.value();
    context.status_string.append(status);
    return 0;
}

template <typename T>
int Http1Session<T>::on_header_field(llhttp_t *parser, const char *at, size_t length) {
    auto *self = (Http1Session<T> *) parser->data;
    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    const Stream &stream = self->active_stream();

    std::string_view field = {at, length};
    log_sid(trace, self->m_id, stream.id, "{}", field);

    if (!self->m_parser_context.has_value()) {
        log_sid(dbg, self->m_id, stream.id, "Parser context isn't initialized");
        return -1;
    }

    ParserContext &context = self->m_parser_context.value();
    if (bool is_new_field = context.headers.empty() || !context.headers.back().second.empty(); is_new_field) {
        context.headers.emplace_back(field, std::string{});
    } else {
        context.headers.back().first.append(field);
    }

    return 0;
}

template <typename T>
int Http1Session<T>::on_header_value(llhttp_t *parser, const char *at, size_t length) {
    auto *self = (Http1Session<T> *) parser->data;
    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    const Stream &stream = self->active_stream();

    std::string_view value = {at, length};
    log_sid(trace, self->m_id, stream.id, "{}", value);

    if (!self->m_parser_context.has_value()) {
        log_sid(dbg, self->m_id, stream.id, "Parser context isn't initialized");
        return -1;
    }

    ParserContext &context = self->m_parser_context.value();
    if (context.headers.empty()) {
        log_sid(dbg, self->m_id, stream.id, "Got value before name: {}", value);
        return -1;
    }

    context.headers.back().second.append(value);
    return 0;
}

template <typename T>
int Http1Session<T>::on_headers_complete(llhttp_t *parser) {
    // According to `llhttp_settings_t.on_headers_complete` spec
    enum Result {
        PROCEED_NORMALLY = 0,
        HAS_NO_BODY_PROCEED_PARSING = 1,
        HAS_NO_BODY_RETURN_PAUSED_UPGRADE = 2,
        ERROR = -1,
    };

    auto *self = (Http1Session<T> *) parser->data;
    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    Stream &stream = self->active_stream();

    log_sid(trace, self->m_id, stream.id, "...");

    if (!self->m_parser_context.has_value()) {
        log_sid(dbg, self->m_id, stream.id, "Parser context isn't initialized");
        return -1;
    }

    ParserContext &context = self->m_parser_context.value();
    Version version = HTTP_1_1;
    if (std::optional v = make_version(llhttp_get_http_major(parser), llhttp_get_http_minor(parser)); v.has_value()) {
        version = v.value();
    } else {
        log_sid(dbg, self->m_id, stream.id, "Unexpected version: {}.{}", llhttp_get_http_major(parser),
                llhttp_get_http_minor(parser));
        return -1;
    }

    Headers headers;
    for (auto &[name, value] : std::exchange(context.headers, {})) {
        headers.put(std::move(name), std::move(value));
    }

    Result ret = PROCEED_NORMALLY;
    if constexpr (std::is_same_v<T, Http1Server>) {
        auto method = (llhttp_method_t) llhttp_get_method(parser);
        headers.has_body(
                (parser->flags & F_CHUNKED) || ((parser->flags & F_CONTENT_LENGTH) && parser->content_length > 0));

        Request request(version, llhttp_method_name(method), std::move(context.path));
        request.headers() = std::move(headers);

        stream.flags.set(Stream::UPGRADE_REQUEST, parser->upgrade != 0);
        stream.flags.set(Stream::HEAD_REQUEST, method == HTTP_HEAD);
        stream.flags.set(Stream::HAS_BODY, request.headers().has_body());

        if (auto &h = static_cast<T *>(self)->m_handler; h.on_request != nullptr) {
            h.on_request(h.arg, stream.id, std::move(request));
        }

        if (llhttp_get_upgrade(parser)) {
            ret = HAS_NO_BODY_RETURN_PAUSED_UPGRADE;
        } else if (!stream.flags.test(Stream::HAS_BODY)) {
            ret = HAS_NO_BODY_PROCEED_PARSING;
        }
    } else {
        int status_code = llhttp_get_status_code(parser);
        headers.has_body((status_code != HTTP_STATUS_CONTINUE && status_code != HTTP_STATUS_EARLY_HINTS)
                && ((parser->flags & F_CHUNKED) || !(parser->flags & F_CONTENT_LENGTH) || parser->content_length != 0));

        Response response(version, status_code);
        response.status_code(status_code);
        response.status_string(std::move(context.status_string));
        response.headers() = std::move(headers);

        stream.flags.set(Stream::HAS_BODY, response.headers().has_body());
        stream.flags.set(Stream::INTERMEDIATE_RESPONSE, status_code < HTTP_STATUS_OK);

        if (auto &h = static_cast<T *>(self)->m_handler; h.on_response != nullptr) {
            h.on_response(h.arg, stream.id, std::move(response));
        }

        // If server responds before request is sent, it is not HTTP/1.1
        // https://github.com/AdguardTeam/CoreLibs/issues/441
        bool pseudo_http = !stream.flags.test(Stream::REQ_SENT) && status_code != HTTP_STATUS_CONTINUE
                && status_code != HTTP_STATUS_EARLY_HINTS;

        if (llhttp_get_upgrade(parser) || pseudo_http) {
            ret = HAS_NO_BODY_RETURN_PAUSED_UPGRADE;
        } else if (!stream.flags.test(Stream::HAS_BODY)) {
            ret = HAS_NO_BODY_PROCEED_PARSING;
        }
    }

    log_sid(trace, self->m_id, stream.id, "Returning {}", magic_enum::enum_name(ret));
    return ret;
}

template <typename T>
int Http1Session<T>::on_body(llhttp_t *parser, const char *at, size_t length) {
    auto *self = (Http1Session<T> *) parser->data;
    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    Stream &stream = self->active_stream();
    log_sid(trace, self->m_id, stream.id, "length={}", length);

    stream.flags.set(Stream::BODY_DATA_STARTED);
    if (auto &h = static_cast<T *>(self)->m_handler; h.on_body != nullptr) {
        h.on_body(h.arg, stream.id, {(uint8_t *) at, length});
    }

    return 0;
}

template <typename T>
int Http1Session<T>::on_message_complete(llhttp_t *parser) {
    auto *self = (Http1Session<T> *) parser->data;
    if (self->m_streams.empty()) {
        log_id(dbg, self->m_id, "There're no active streams");
        return -1;
    }

    Stream &stream = self->active_stream();
    log_sid(trace, self->m_id, stream.id, "...");

    auto &handler = static_cast<T *>(self)->m_handler;
    if (stream.flags.test(Stream::HAS_BODY)) {
        if (handler.on_trailer_headers != nullptr && self->m_parser_context.has_value()
                && !self->m_parser_context->headers.empty()) {
            Headers headers;
            for (auto &[name, value] : std::exchange(self->m_parser_context->headers, {})) {
                headers.put(std::move(name), std::move(value));
            }
            handler.on_trailer_headers(handler.arg, stream.id, std::move(headers));
        }
        if (handler.on_body_finished != nullptr) {
            handler.on_body_finished(handler.arg, stream.id);
        }
    }

    if (std::is_same_v<T, Http1Client> && !stream.flags.test(Stream::INTERMEDIATE_RESPONSE)) {
        if (handler.on_stream_finished != nullptr) {
            handler.on_stream_finished(handler.arg, stream.id, 0);
        }
        self->m_streams.pop_front();
        self->m_parser_context.reset();
    }

    return 0;
}

template <typename T>
void Http1Session<T>::reset_parser() {
    llhttp_reset(&m_parser);
    m_parser_context.reset();
}

template <typename T>
typename Http1Session<T>::Stream &Http1Session<T>::active_stream() {
    if constexpr (std::is_same_v<T, Http1Server>) {
        return m_streams.back();
    }
    return m_streams.front();
}

template <typename T>
Result<typename Http1Session<T>::InputResult, Http1Error> Http1Session<T>::input_impl(ag::Uint8View chunk) {
    log_id(trace, m_id, "length={}", chunk.length());

    if (llhttp_get_errno(&m_parser) != HPE_OK) {
        reset_parser();
    }

    llhttp_errno_t err = llhttp_execute(&m_parser, (char *) chunk.data(), chunk.length());
    switch (err) {
    case HPE_OK:
        return InputOk{};
    case HPE_PAUSED_UPGRADE:
        return InputUpgrade{};
    default:
        return make_error(Http1Error{}, AG_FMT("{} ({})", llhttp_errno_name(err), magic_enum::enum_name(err)));
    }
}

template <typename T>
Error<Http1Error> Http1Session<T>::send_trailer_impl(uint32_t stream_id, const ag::http::Headers &headers, bool eof) {
    if (m_streams.empty()) {
        return make_error(Http1Error{}, "There're no active streams");
    }

    Stream &stream = m_streams.front();
    if (stream_id != stream.id) {
        return make_error(Http1Error{}, AG_FMT("Invalid stream ID ({}), expected {}", stream_id, stream.id));
    }

    bool zero_chunk_sent = stream.flags.test(Stream::ZERO_CHUNK_SENT);
    stream.flags.set(Stream::ZERO_CHUNK_SENT);

    auto &handler = static_cast<T *>(this)->m_handler;
    if (handler.on_output == nullptr) {
        return {};
    }

    if (!zero_chunk_sent) {
        if (Error<Http1Error> error = send_body_impl(stream_id, {}, false); error != nullptr) {
            return make_error(Http1Error{}, error);
        }
        handler.on_output(handler.arg, {(uint8_t *) CHUNK_FOOTER.data(), CHUNK_FOOTER.size()});
    }

    std::string data = headers.str();
    handler.on_output(handler.arg, {(uint8_t *) data.data(), data.length()});

    if (eof) {
        handler.on_output(handler.arg, {(uint8_t *) CHUNK_FOOTER.data(), CHUNK_FOOTER.size()});
        m_streams.pop_front();
        if (handler.on_stream_finished != nullptr) {
            handler.on_stream_finished(handler.arg, stream_id, 0);
        }
    }

    return {};
}

template <typename T>
Error<Http1Error> Http1Session<T>::send_body_impl(uint64_t stream_id, ag::Uint8View chunk, bool eof) {
    log_sid(trace, m_id, stream_id, "length={} eof={}", chunk.length(), eof);

    if (m_streams.empty()) {
        return make_error(Http1Error{}, "There're no active streams");
    }

    Stream &stream = active_stream();
    if (stream_id != stream.id) {
        return make_error(Http1Error{}, AG_FMT("Invalid stream ID ({}), expected {}", stream_id, stream.id));
    }

    auto &handler = static_cast<T *>(this)->m_handler;
    bool chunked = std::holds_alternative<ContentLengthChunked>(stream.content_length);
    if (chunked) {
        char chunk_header[64]; // NOLINT(*-magic-numbers)
        size_t chunk_header_size =
                fmt::format_to_n(chunk_header, sizeof(chunk_header), "{:X}\r\n", chunk.length()).size;

        stream.flags.set(Stream::ZERO_CHUNK_SENT, chunk.empty());

        if (handler.on_output != nullptr) {
            handler.on_output(handler.arg, {(uint8_t *) chunk_header, chunk_header_size});
            if (!chunk.empty()) {
                handler.on_output(handler.arg, chunk);
                handler.on_output(handler.arg, {(uint8_t *) CHUNK_FOOTER.data(), CHUNK_FOOTER.size()});
                if (eof) {
                    if (Error<Http1Error> error = send_body_impl(stream_id, {}, false); error != nullptr) {
                        return make_error(Http1Error{}, error);
                    }
                }
            }
            if (eof) {
                handler.on_output(handler.arg, {(uint8_t *) CHUNK_FOOTER.data(), CHUNK_FOOTER.size()});
            }
        }
    } else {
        if (!chunk.empty() && handler.on_output != nullptr) {
            handler.on_output(handler.arg, chunk);
        }
        if (auto *content_length = std::get_if<size_t>(&stream.content_length); content_length != nullptr) {
            *content_length = (*content_length >= chunk.length()) ? *content_length - chunk.length() : 0;
            eof = *content_length == 0;
        }
    }

    if (eof) {
        if constexpr (std::is_same_v<Http1Server, T>) {
            m_streams.pop_front();
            if (handler.on_stream_finished != nullptr) {
                handler.on_stream_finished(handler.arg, stream_id, 0);
            }
        } else {
            stream.flags.set(Stream::REQ_SENT);
        }
    }

    return {};
}

Http1Server::Http1Server(const Callbacks &handler)
        : m_handler(handler) {
}

Http1Server::~Http1Server() = default;

Result<Http1Server::InputResult, Http1Error> Http1Server::input(Uint8View chunk) {
    return input_impl(chunk);
}

Error<Http1Error> Http1Server::send_response(uint64_t stream_id, const ag::http::Response &response) {
    log_sid(trace, m_id, stream_id, "...");

    if (m_streams.empty()) {
        return make_error(Http1Error{}, "There're no active streams");
    }

    Stream &stream = m_streams.front();
    if (stream_id != stream.id) {
        return make_error(Http1Error{}, AG_FMT("Invalid stream ID ({}), expected {}", stream_id, stream.id));
    }

    const Headers &headers = response.headers();
    if (utils::iequals(headers.gets("Transfer-Encoding"), "chunked")) {
        stream.content_length = ContentLengthChunked{};
    } else if (std::optional x = utils::to_integer<size_t>(headers.gets("Content-Length")).has_value(); x.has_value()) {
        stream.content_length = x.value();
    } else {
        stream.content_length = ContentLengthUnset{};
    }

    if (m_handler.on_output != nullptr) {
        std::string data = response.str();
        m_handler.on_output(m_handler.arg, {(uint8_t *) data.data(), data.length()});
    }

    int status_code = response.status_code();
    // NOLINTNEXTLINE(*-magic-numbers)
    bool cant_have_body = status_code / 100 == 1 /* 1xx e.g. Continue */
            || status_code == HTTP_STATUS_NO_CONTENT || status_code == HTTP_STATUS_RESET_CONTENT
            || status_code == HTTP_STATUS_NOT_MODIFIED || stream.flags.test(Stream::HEAD_REQUEST);
    bool empty_msg = cant_have_body
            || (std::holds_alternative<size_t>(stream.content_length) && std::get<size_t>(stream.content_length) == 0);
    if (empty_msg) {
        bool is_upgrade = stream.flags.test(Stream::UPGRADE_REQUEST)
                && (!headers.gets("Upgrade").empty()
                        || std::string_view::npos != utils::ifind(headers.gets("Connection"), "upgrade"));
        if (!is_upgrade && status_code != HTTP_STATUS_CONTINUE && status_code != HTTP_STATUS_EARLY_HINTS) {
            m_streams.pop_front();
            if (m_handler.on_stream_finished != nullptr) {
                m_handler.on_stream_finished(m_handler.arg, stream_id, 0);
            }
        }
    }

    return {};
}

Error<Http1Error> Http1Server::send_trailer(uint32_t stream_id, const Headers &headers, bool eof) {
    return send_trailer_impl(stream_id, headers, eof);
}

Error<Http1Error> Http1Server::send_body(uint64_t stream_id, Uint8View chunk, bool eof) {
    return send_body_impl(stream_id, chunk, eof);
}

Http1Client::Http1Client(const Callbacks &handler)
        : m_handler(handler) {
}

Http1Client::~Http1Client() = default;

Result<Http1Client::InputResult, Http1Error> Http1Client::input(Uint8View chunk) {
    return input_impl(chunk);
}

Result<uint32_t, Http1Error> Http1Client::send_request(const Request &request) {
    uint32_t stream_id = m_next_stream_id++;
    log_sid(trace, m_id, stream_id, "...");

    Stream &stream = m_streams.emplace_back(stream_id);

    const Headers &headers = request.headers();
    if (utils::iequals(headers.gets("Transfer-Encoding"), "chunked")) {
        stream.content_length = ContentLengthChunked{};
    } else if (std::optional h = headers.get("Content-Length"); h.has_value()) {
        if (std::optional x = utils::to_integer<size_t>(h.value()).has_value(); x.has_value()) {
            stream.content_length = x.value();
        } else {
            log_sid(dbg, m_id, stream_id, "Couldn't parse Content-Length header value: {}", h.value());
        }
    }

    if (m_handler.on_output != nullptr) {
        std::string data = request.str();
        m_handler.on_output(m_handler.arg, {(uint8_t *) data.data(), data.length()});
    }

    bool empty_msg = request.method() == "HEAD" || std::holds_alternative<ContentLengthUnset>(stream.content_length)
            || (std::holds_alternative<size_t>(stream.content_length) && std::get<size_t>(stream.content_length) == 0);
    stream.flags.set(Stream::REQ_SENT, empty_msg);

    return stream_id;
}

Error<Http1Error> Http1Client::send_trailer(uint32_t stream_id, const Headers &headers, bool eof) {
    return send_trailer_impl(stream_id, headers, eof);
}

Error<Http1Error> Http1Client::send_body(uint64_t stream_id, Uint8View chunk, bool eof) {
    return send_body_impl(stream_id, chunk, eof);
}

} // namespace ag::http
