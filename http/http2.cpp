#include <algorithm>
#include <atomic>

#include <event2/buffer.h>
#include <magic_enum.hpp>

#include "common/http/http2.h"
#include "common/logger.h"
#include "common/utils.h"

#define log_id(lvl_, id_, fmt_, ...) lvl_##log(g_logger, "[{}] " fmt_, id_, ##__VA_ARGS__)
#define log_sid(lvl_, id_, stream_, fmt_, ...) lvl_##log(g_logger, "[{}-{}] " fmt_, id_, stream_, ##__VA_ARGS__)
#define log_frsid(lvl_, id_, frm_, fmt_, ...) log_sid(lvl_, id_, (frm_)->hd.stream_id, fmt_, ##__VA_ARGS__)

namespace ag::http {

static const Logger g_logger("H2");    // NOLINT(*-identifier-naming)
static std::atomic_uint32_t g_next_id; // NOLINT(*-avoid-non-const-global-variables)

template <typename T>
static constexpr nghttp2_nv transform_header(const Header<T> &header) {
    return nghttp2_nv{
            .name = (uint8_t *) header.name.data(),
            .value = (uint8_t *) header.value.data(),
            .namelen = header.name.size(),
            .valuelen = header.value.size(),
    };
}

#ifndef NDEBUG
static void log_http2(const char *format, va_list args) {
    if (g_logger.is_enabled(LOG_LEVEL_TRACE)) {
        int n = std::vsnprintf(nullptr, 0, format, args);
        if (n > 0) {
            std::string buf(n, '\0');
            std::vsnprintf(buf.data(), buf.size() + 1, format, args); // NOLINT(*-err33-c)
            tracelog(g_logger, "{}", buf);
        }
    }
}
#endif

template <typename T>
Http2Session<T>::Http2Session(const Http2Settings &settings)
        : m_settings(settings)
        , m_id(g_next_id.fetch_add(1, std::memory_order_relaxed)) {
}

template <typename T>
Error<Http2Error> Http2Session<T>::initialize_session() {
#ifndef NDEBUG
    if (g_logger.is_enabled(LOG_LEVEL_TRACE)) {
        // works only in case `DEBUGBUILD` is defined for nghttp2
        nghttp2_set_debug_vprintf_callback(log_http2);
    }
#endif

    UniquePtr<nghttp2_session_callbacks, &nghttp2_session_callbacks_del> session_callbacks{[]() {
        nghttp2_session_callbacks *x = nullptr;
        nghttp2_session_callbacks_new(&x);
        return x;
    }()};

    nghttp2_session_callbacks_set_on_begin_frame_callback(session_callbacks.get(), on_begin_frame);
    nghttp2_session_callbacks_set_on_frame_recv_callback(session_callbacks.get(), on_frame_recv);
    nghttp2_session_callbacks_set_on_frame_send_callback(session_callbacks.get(), on_frame_send);
    nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(session_callbacks.get(), on_invalid_frame_recv);
    nghttp2_session_callbacks_set_on_begin_headers_callback(session_callbacks.get(), on_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(session_callbacks.get(), on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(session_callbacks.get(), on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(session_callbacks.get(), on_stream_close);
    nghttp2_session_callbacks_set_send_callback(session_callbacks.get(), on_send);
    nghttp2_session_callbacks_set_error_callback(session_callbacks.get(), on_error);

    UniquePtr<nghttp2_option, &nghttp2_option_del> option{[]() {
        nghttp2_option *x = nullptr;
        nghttp2_option_new(&x);
        return x;
    }()};
    nghttp2_option_set_max_reserved_remote_streams(option.get(), 0);
    if (!m_settings.auto_flow_control) {
        nghttp2_option_set_no_auto_window_update(option.get(), 1);
    }

    nghttp2_session *session = nullptr;
    int status; // NOLINT(*-init-variables)
    static_assert(std::is_same_v<T, Http2Server> || std::is_same_v<T, Http2Client>);
    if constexpr (std::is_same_v<T, Http2Server>) {
        status = nghttp2_session_server_new2(&session, session_callbacks.get(), this, option.get());
    } else {
        status = nghttp2_session_client_new2(&session, session_callbacks.get(), this, option.get());
    }
    if (status != 0) {
        return make_error(Http2Error{}, AG_FMT("Couldn't create session: {} ({})", nghttp2_strerror(status), status));
    }

    m_session.reset(session);

    if constexpr (std::is_same_v<T, Http2Client>) {
        if (Error<Http2Error> error = submit_settings_impl(); error != nullptr) {
            m_session.reset();
            return make_error(Http2Error{}, error);
        }
    }

    return nullptr;
}

template <typename T>
Http2Session<T>::~Http2Session() {
    for (auto &[stream_id, _] : std::exchange(m_streams, {})) {
        close_stream(stream_id, NGHTTP2_CANCEL);
    }

    if (m_session == nullptr) {
        return;
    }

    int status = nghttp2_session_terminate_session(m_session.get(), m_error);
    if (status != NGHTTP2_NO_ERROR) {
        log_id(dbg, m_id, "Couldn't terminate session: {} ({})", nghttp2_strerror(status), status);
    }

    status = nghttp2_session_send(m_session.get());
    if (status != NGHTTP2_NO_ERROR) {
        log_id(dbg, m_id, "Couldn't flush session: {} ({})", nghttp2_strerror(status), status);
    }
}

template <typename T>
int Http2Session<T>::on_begin_frame(nghttp2_session *, const nghttp2_frame_hd *hd, void *arg) {
    auto *self = (T *) arg;
    const auto *frame = (nghttp2_frame *) hd;
    log_frsid(trace, self->m_id, frame, "{}", magic_enum::enum_name(nghttp2_frame_type(hd->type)));
    return 0;
}

template <typename T>
int Http2Session<T>::on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame, void *arg) {
    auto *self = (T *) arg;
    log_frsid(trace, self->m_id, frame, "{}", magic_enum::enum_name(nghttp2_frame_type(frame->hd.type)));

    uint32_t stream_id = frame->hd.stream_id;
    Stream *stream = nullptr;

    if (frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA || frame->hd.type == NGHTTP2_PUSH_PROMISE) {
        auto iter = self->m_streams.find(stream_id);
        if (iter != self->m_streams.end()) {
            stream = &iter->second;
        } else {
            log_frsid(dbg, self->m_id, frame, "Stream not found");
        }
    }

    const auto &handler = self->m_handler;
    /*
     * Documentation says that there are no ..._end_... callbacks, and this callback is fired after
     * all frame-type-specific callbacks.
     * So if we need to do some finishing frame-type-specific work, it must be done here.
     * Also, all CONTINUATION frames are automatically merged into previous by nghttp2,
     * and we don't need to process them separately.
     */
    switch (frame->hd.type) {
    case NGHTTP2_HEADERS:
        if (stream != nullptr && (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS)) {
            self->on_end_headers(frame, stream_id, *stream);
        }
        [[fallthrough]];
    case NGHTTP2_DATA:
        if (stream != nullptr && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
            self->on_end_stream(stream_id);
        }
        break;
    case NGHTTP2_GOAWAY:
        if (handler.on_close != nullptr) {
            handler.on_close(handler.arg, nghttp2_error_code(frame->goaway.error_code));
        }
        break;
    case NGHTTP2_PUSH_PROMISE:
        // Push promises are not supported at this time and silently dropped
        break;
    case NGHTTP2_WINDOW_UPDATE:
        log_frsid(trace, self->m_id, frame, "Received window update: increment={}",
                frame->window_update.window_size_increment);

        if (stream_id != 0 && frame->window_update.window_size_increment > 0) {
            if (handler.on_window_update != nullptr) {
                handler.on_window_update(handler.arg, stream_id, frame->window_update.window_size_increment);
            }
        }

        break;
    default:
        // do nothing
        break;
    }

    if (frame->hd.type == NGHTTP2_DATA || frame->hd.type == NGHTTP2_WINDOW_UPDATE) {
        log_frsid(trace, self->m_id, frame, "Remote window size: session={} stream={}",
                nghttp2_session_get_remote_window_size(session),
                nghttp2_session_get_stream_remote_window_size(session, stream_id));
        log_frsid(trace, self->m_id, frame, "Local window size: session={} stream={}",
                nghttp2_session_get_local_window_size(session),
                nghttp2_session_get_stream_local_window_size(session, stream_id));
    }

    return 0;
}

template <typename T>
int Http2Session<T>::on_frame_send(nghttp2_session *session, const nghttp2_frame *frame, void *arg) {
    auto *self = (T *) arg;
    log_frsid(trace, self->m_id, frame, "{}", magic_enum::enum_name(nghttp2_frame_type(frame->hd.type)));

    switch (frame->hd.type) {
    case NGHTTP2_WINDOW_UPDATE:
        log_frsid(trace, self->m_id, frame, "Sent window update: increment={}",
                frame->window_update.window_size_increment);
        break;
    case NGHTTP2_DATA:
        log_frsid(trace, self->m_id, frame, "Remote window size: session={} stream={}",
                nghttp2_session_get_remote_window_size(session),
                nghttp2_session_get_stream_remote_window_size(session, frame->hd.stream_id));
        log_frsid(trace, self->m_id, frame, "Local window size: session={} stream={}",
                nghttp2_session_get_local_window_size(session),
                nghttp2_session_get_stream_local_window_size(session, frame->hd.stream_id));
        break;
    case NGHTTP2_SETTINGS:
        if constexpr (std::is_same_v<T, Http2Server>) {
            auto *server = static_cast<T *>(self);
            if ((frame->settings.hd.flags & NGHTTP2_FLAG_ACK) && !server->m_received_handshake) {
                server->m_received_handshake = true;
                Error<Http2Error> error = self->submit_settings_impl();
                if (error != nullptr) {
                    log_frsid(dbg, self->m_id, frame, "{}", error->str());
                    return NGHTTP2_ERR_CALLBACK_FAILURE;
                }
            }
        }
        break;
    default:
        // do nothing
        break;
    }

    return 0;
}

template <typename T>
int Http2Session<T>::on_invalid_frame_recv(nghttp2_session *, const nghttp2_frame *frame, int error_code, void *arg) {
    auto *self = (T *) arg;
    log_frsid(dbg, self->m_id, frame, "{} ({})", nghttp2_strerror(error_code), error_code);
    return 0;
}

template <typename T>
int Http2Session<T>::on_begin_headers(nghttp2_session *, const nghttp2_frame *frame, void *arg) {
    auto *self = (T *) arg;
    log_frsid(trace, self->m_id, frame, "...");

    Stream &stream = self->m_streams.emplace(frame->hd.stream_id, Stream{}).first->second;
    if (stream.message.has_value()) {
        log_frsid(warn, self->m_id, frame, "Another headers is already in progress: {}", stream.message.value());
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    Message &message = stream.message.emplace(HTTP_2_0);
    message.headers().reserve(frame->headers.nvlen);

    return 0;
}

template <typename T>
int Http2Session<T>::on_header(nghttp2_session *, const nghttp2_frame *frame, const uint8_t *name_, size_t name_len,
        const uint8_t *value_, size_t value_len, uint8_t, void *arg) {
    auto *self = (T *) arg;
    std::string_view name = {(char *) name_, name_len};
    std::string_view value = {(char *) value_, value_len};
    log_frsid(trace, self->m_id, frame, "{}: {}", name, value);

    auto iter = self->m_streams.find(frame->hd.stream_id);
    if (iter == self->m_streams.end()) {
        log_frsid(warn, self->m_id, frame, "Stream not found");
        return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    if (!stream.message.has_value()) {
        log_frsid(warn, self->m_id, frame, "Stream has no pending message");
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    Message &message = stream.message.value();
    static_assert(std::is_same_v<T, Http2Server> || std::is_same_v<T, Http2Client>);
    if constexpr (std::is_same_v<T, Http2Server>) {
        if (name == PSEUDO_HEADER_NAME_METHOD) {
            message.method(std::string{value});
            return 0;
        }
        if (name == PSEUDO_HEADER_NAME_SCHEME) {
            message.scheme(std::string{value});
            return 0;
        }
        if (name == PSEUDO_HEADER_NAME_AUTHORITY) {
            message.authority(std::string{value});
            return 0;
        }
        if (name == PSEUDO_HEADER_NAME_PATH) {
            message.path(std::string{value});
            return 0;
        }
    } else {
        if (name == PSEUDO_HEADER_NAME_STATUS) {
            std::optional code = utils::to_integer<unsigned int>(value);
            if (!code.has_value()) {
                log_frsid(dbg, self->m_id, frame, "Couldn't parse status code: {}", value);
                return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
            }
            message.status_code(code.value());
            return 0;
        }
    }

    message.headers().put(std::string{name}, std::string{value});

    return 0;
}

class ConsumeGuard {
public:
    ConsumeGuard(const ConsumeGuard &) = delete;
    ConsumeGuard &operator=(const ConsumeGuard &) = delete;
    ConsumeGuard(ConsumeGuard &&) = delete;
    ConsumeGuard &operator=(ConsumeGuard &&) = delete;

    ConsumeGuard(uint32_t id, nghttp2_session *session, size_t n)
            : m_id(id)
            , m_session(session)
            , m_n(n) {
    }

    ~ConsumeGuard() {
        int status = nghttp2_session_consume_connection(m_session, m_n);
        if (status != NGHTTP2_NO_ERROR) {
            log_id(warn, m_id, "Couldn't consume session: {} ({})", nghttp2_strerror(status), status);
        }
    }

private:
    uint32_t m_id;
    nghttp2_session *m_session;
    size_t m_n;
};

template <typename T>
int Http2Session<T>::on_data_chunk_recv(
        nghttp2_session *session, uint8_t, int32_t stream_id, const uint8_t *data, size_t len, void *arg) {
    auto *self = (Http2Session<T> *) arg;
    log_sid(trace, self->m_id, stream_id, "{}", len);

    std::optional<ConsumeGuard> consume_guard;
    if (self->m_settings.auto_flow_control) {
        // Consuming session here is simpler than controlling it on the higher level, which is more
        // error-prone and may lead to unrecoverable window leak out, but it leads to the higher data
        // buffering rate.
        consume_guard.emplace(self->m_id, session, len);
    }

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP2_ERR_INVALID_STATE;
    }

    if (const auto &h = static_cast<T *>(self)->m_handler; h.on_body != nullptr) {
        h.on_body(h.arg, stream_id, {data, len});
    }

    return 0;
}

template <typename T>
int Http2Session<T>::on_stream_close(nghttp2_session *, int32_t stream_id, uint32_t error_code, void *arg) {
    auto *self = (T *) arg;
    log_sid(trace, self->m_id, stream_id, "{} ({})", nghttp2_strerror(error_code), error_code);

    auto node = self->m_streams.extract(stream_id);
    if (node.empty()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP2_ERR_INVALID_STATE;
    }

    self->close_stream(stream_id, nghttp2_error_code(error_code));

    return 0;
}

template <typename T>
ssize_t Http2Session<T>::on_send(nghttp2_session *, const uint8_t *data, size_t length, int, void *arg) {
    auto *self = (T *) arg;
    log_id(trace, self->m_id, "length={}", length);

    if (const auto &h = self->m_handler; h.on_output != nullptr) {
        h.on_output(h.arg, {data, length});
    }

    return ssize_t(length);
}

template <typename T>
int Http2Session<T>::on_error(nghttp2_session *, const char *msg, size_t len, void *arg) {
    auto *self = (T *) arg;
    log_id(dbg, self->m_id, "{}", std::string_view{msg, len});
    return 0;
}

/**
 * Data source read callback. Called by nghttp2 when it is ready to send data.
 * Returns data if it is in output buffer or ERR_DEFERRED if buffer is empty and no EOF flag set.
 * If ERR_DEFERRED is set, nghttp2_session_resume_data() is called on next http2_data_provider_write() call.
 */
template <typename T>
ssize_t Http2Session<T>::on_data_source_read(nghttp2_session *session, int32_t stream_id, uint8_t *buf, size_t length,
        uint32_t *data_flags, nghttp2_data_source *source, void *arg) {
    auto *self = (T *) arg;

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    auto *ds = (DataSource *) source->ptr;

    // Pause and destroy a data source if no work on the current buffer
    if (!stream.flags.test(Stream::HAS_EOF) && 0 == evbuffer_get_length(ds->buffer.get())) {
        log_sid(trace, self->m_id, stream_id, "No work on current buffer");
        return NGHTTP2_ERR_DEFERRED;
    }

    ssize_t n = evbuffer_remove(ds->buffer.get(), buf, length);
    log_sid(trace, self->m_id, stream_id, "{} bytes", n);
    if (n < 0) {
        log_sid(dbg, self->m_id, stream_id, "Couldn't read buffer");
        return NGHTTP2_ERR_BUFFER_ERROR;
    }

    // If eof, set flag and destroy data source
    if (stream.flags.test(Stream::HAS_EOF) && 0 == evbuffer_get_length(ds->buffer.get())) {
        log_sid(trace, self->m_id, stream_id, "No data left in buffers -- set eof flag");
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }

    if (const auto &h = self->m_handler; h.on_data_sent != nullptr) {
        h.on_data_sent(h.arg, stream_id, n);
    }

    log_sid(trace, self->m_id, stream_id, "Remote window size: session={} stream={}",
            nghttp2_session_get_remote_window_size(session),
            nghttp2_session_get_stream_remote_window_size(session, stream_id));
    log_sid(trace, self->m_id, stream_id, "Local window size: session={} stream={}",
            nghttp2_session_get_local_window_size(session),
            nghttp2_session_get_stream_local_window_size(session, stream_id));

    return n;
}

template <typename T>
void Http2Session<T>::on_end_headers(const nghttp2_frame *frame, uint32_t stream_id, Stream &stream) {
    log_frsid(trace, m_id, frame, "...");

    Message message = std::move(std::exchange(stream.message, std::nullopt).value());
    message.headers().has_body(!(frame->headers.hd.flags & NGHTTP2_FLAG_END_STREAM));

    if (stream.flags.test(Stream::MESSAGE_ALREADY_RECEIVED)) {
        if (const auto &h = static_cast<T *>(this)->m_handler; h.on_trailer_headers != nullptr) {
            h.on_trailer_headers(h.arg, stream_id, Message::into_headers(std::move(message)));
        }
        return;
    }

    static_assert(std::is_same_v<T, Http2Server> || std::is_same_v<T, Http2Client>);
    if constexpr (std::is_same_v<T, Http2Server>) {
        stream.flags.set(Stream::MESSAGE_ALREADY_RECEIVED);
        stream.flags.set(Stream::HEAD_REQUEST, message.method() == "HEAD");
        if (const auto &h = static_cast<T *>(this)->m_handler; h.on_request != nullptr) {
            h.on_request(h.arg, stream_id, std::move(message));
        }
    } else {
        // NOLINTNEXTLINE(*-magic-numbers)
        stream.flags.set(Stream::MESSAGE_ALREADY_RECEIVED, 200 <= message.status_code());
        if (const auto &h = static_cast<T *>(this)->m_handler; h.on_response != nullptr) {
            h.on_response(h.arg, stream_id, std::move(message));
        }
    }
}

template <typename T>
void Http2Session<T>::on_end_stream(uint32_t stream_id) {
    log_sid(trace, m_id, stream_id, "...");

    if (const auto &h = static_cast<T *>(this)->m_handler; h.on_stream_read_finished != nullptr) {
        h.on_stream_read_finished(h.arg, stream_id);
    }
}

template <typename T>
void Http2Session<T>::close_stream(uint32_t stream_id, nghttp2_error_code error_code) {
    if (const auto &h = static_cast<T *>(this)->m_handler; h.on_stream_closed != nullptr) {
        h.on_stream_closed(h.arg, stream_id, error_code);
    }
}

template <typename T>
int Http2Session<T>::push_data(Stream &stream, Uint8View chunk, bool eof) {
    if (stream.data_source.buffer == nullptr) {
        stream.data_source.buffer.reset(evbuffer_new());
    }
    stream.flags.set(Stream::HAS_EOF, eof);
    return evbuffer_add(stream.data_source.buffer.get(), chunk.data(), chunk.size());
}

template <typename T>
int Http2Session<T>::schedule_send(uint32_t stream_id, Stream &stream) {
    if (stream.flags.test(Stream::SCHEDULED)) {
        return nghttp2_session_resume_data(m_session.get(), int32_t(stream_id));
    }

    nghttp2_data_provider provider = {
            .source =
                    {
                            .ptr = &stream.data_source,
                    },
            .read_callback = on_data_source_read,
    };
    stream.flags.set(Stream::SCHEDULED);
    return nghttp2_submit_data(m_session.get(), NGHTTP2_FLAG_END_STREAM, int32_t(stream_id), &provider);
}

template <typename T>
Result<size_t, Http2Error> Http2Session<T>::input_impl(Uint8View chunk) {
    log_id(trace, m_id, "Length={}", chunk.length());

    ssize_t status = nghttp2_session_mem_recv(m_session.get(), chunk.data(), chunk.size());
    if (status < 0) {
        return make_error(Http2Error{}, AG_FMT("{} ({})", nghttp2_strerror(status), status));
    }

    return size_t(status);
}

template <typename T>
Error<Http2Error> Http2Session<T>::submit_settings_impl() {
    nghttp2_settings_entry settings[] = {
            {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, m_settings.header_table_size},
            {NGHTTP2_SETTINGS_ENABLE_PUSH, false},
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, m_settings.max_concurrent_streams},
            {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, m_settings.initial_stream_window_size},
            {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, m_settings.max_frame_size},
    };

    if (int status = nghttp2_submit_settings(m_session.get(), 0, settings, std::size(settings));
            status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("Couldn't submit settings: {} ({})", nghttp2_strerror(status), status));
    }

    // Set window size for connection after sending SETTINGS to remote server
    if (int status = nghttp2_session_set_local_window_size(
                m_session.get(), NGHTTP2_FLAG_NONE, 0, int32_t(m_settings.initial_session_window_size));
            status != NGHTTP2_NO_ERROR) {
        return make_error(
                Http2Error{}, AG_FMT("Couldn't set session window size: {} ({})", nghttp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http2Error> Http2Session<T>::submit_trailer_impl(uint32_t stream_id, const Headers &headers) {
    std::vector<nghttp2_nv> nv_list;
    nv_list.reserve(std::distance(headers.begin(), headers.end()));
    std::transform(headers.begin(), headers.end(), std::back_inserter(nv_list), transform_header<std::string>);

    if (int status = nghttp2_submit_trailer(m_session.get(), int32_t(stream_id), nv_list.data(), nv_list.size());
            status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("{} ({})", nghttp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http2Error> Http2Session<T>::submit_body_impl(uint32_t stream_id, ag::Uint8View chunk, bool eof) {
    log_sid(trace, m_id, stream_id, "Length={} eof={}", chunk.length(), eof);

    auto iter = m_streams.find(stream_id);
    if (iter == m_streams.end()) {
        return make_error(Http2Error{}, "Stream not found");
    }

    Stream &stream = iter->second;
    if (0 != push_data(stream, chunk, eof)) {
        return make_error(Http2Error{}, "Couldn't push data in buffer");
    }
    if (int status = schedule_send(stream_id, stream); status != 0) {
        return make_error(
                Http2Error{}, AG_FMT("Couldn't schedule data to send: {} ({})", nghttp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http2Error> Http2Session<T>::reset_stream_impl(uint32_t stream_id, nghttp2_error_code error_code) {
    log_sid(trace, m_id, stream_id, "Error={}", error_code);

    if (int status = nghttp2_submit_rst_stream(m_session.get(), NGHTTP2_FLAG_NONE, int32_t(stream_id), error_code);
            status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("{} ({})", nghttp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http2Error> Http2Session<T>::consume_connection_impl(size_t length) {
    if (int status = nghttp2_session_consume_connection(m_session.get(), length); status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("{} ({})", nghttp2_strerror(status), status));
    }

    log_id(trace, m_id, "Remote window size: {}", nghttp2_session_get_remote_window_size(m_session.get()));
    log_id(trace, m_id, "Local window size: {}", nghttp2_session_get_local_window_size(m_session.get()));

    return {};
}

template <typename T>
Error<Http2Error> Http2Session<T>::consume_stream_impl(uint32_t stream_id, size_t length) {
    if (!m_settings.auto_flow_control) {
        Error<Http2Error> error = consume_connection_impl(length);
        if (error != nullptr) {
            return make_error(Http2Error{}, "Couldn't consume connection", error);
        }
    }

    // session was consumed in data chunk callback
    if (int status = nghttp2_session_consume_stream(m_session.get(), int32_t(stream_id), length);
            status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("Couldn't consume stream: {} ({})", nghttp2_strerror(status), status));
    }

    log_sid(trace, m_id, stream_id, "Remote window size: session={} stream={}",
            nghttp2_session_get_remote_window_size(m_session.get()),
            nghttp2_session_get_stream_remote_window_size(m_session.get(), stream_id));
    log_sid(trace, m_id, stream_id, "Local window size: session={} stream={}",
            nghttp2_session_get_local_window_size(m_session.get()),
            nghttp2_session_get_stream_local_window_size(m_session.get(), stream_id));

    return {};
}

template <typename T>
Error<Http2Error> Http2Session<T>::flush_impl() {
    int status = nghttp2_session_send(m_session.get());
    if (status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("{} ({})", nghttp2_strerror(status), status));
    }
    return {};
}

Http2Server::Http2Server(PrivateAccess, const Http2Settings &settings, const Callbacks &callbacks)
        : Http2Session<Http2Server>(settings)
        , m_handler(callbacks) {
}

Http2Server::~Http2Server() = default;

Result<std::unique_ptr<Http2Server>, Http2Error> Http2Server::make(
        const Http2Settings &settings, const Callbacks &callbacks) {
    auto self = std::make_unique<Http2Server>(PrivateAccess{}, settings, callbacks);
    auto error = self->initialize_session();
    if (error != nullptr) {
        return error;
    }

    return self;
}

Result<size_t, Http2Error> Http2Server::input(Uint8View chunk) {
    return input_impl(chunk);
}

Error<Http2Error> Http2Server::submit_response(uint32_t stream_id, const Response &response, bool eof) {
    auto iter = m_streams.find(stream_id);
    if (iter == m_streams.end()) {
        return make_error(Http2Error{}, "Stream not found");
    }

    const Stream &stream = iter->second;
    eof = eof || stream.flags.test(Stream::HEAD_REQUEST);

    std::vector<nghttp2_nv> nv_list;
    nv_list.reserve(std::distance(response.begin(), response.end()));
    std::transform(response.begin(), response.end(), std::back_inserter(nv_list), transform_header<std::string_view>);

    uint32_t flags = eof ? NGHTTP2_FLAG_END_STREAM : 0;
    if (int status = nghttp2_submit_headers(
                m_session.get(), flags, int32_t(stream_id), nullptr, nv_list.data(), nv_list.size(), nullptr);
            status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("{} ({})", nghttp2_strerror(status), status));
    }

    return {};
}

Error<Http2Error> Http2Server::submit_trailer(uint32_t stream_id, const Headers &headers) {
    return submit_trailer_impl(stream_id, headers);
}

Error<Http2Error> Http2Server::submit_body(uint32_t stream_id, Uint8View chunk, bool eof) {
    return submit_body_impl(stream_id, chunk, eof);
}

Error<Http2Error> Http2Server::reset_stream(uint32_t stream_id, nghttp2_error_code error_code) {
    return reset_stream_impl(stream_id, error_code);
}

void Http2Server::set_session_close_error(nghttp2_error_code error_code) {
    m_error = error_code;
}

Error<Http2Error> Http2Server::consume_connection(size_t length) {
    return consume_connection_impl(length);
}

Error<Http2Error> Http2Server::consume_stream(uint32_t stream_id, size_t length) {
    return consume_stream_impl(stream_id, length);
}

Error<Http2Error> Http2Server::flush() {
    return flush_impl();
}

Http2Client::Http2Client(PrivateAccess, const Http2Settings &settings, const Callbacks &callbacks)
        : Http2Session<Http2Client>(settings)
        , m_handler(callbacks) {
}

Result<std::unique_ptr<Http2Client>, Http2Error> Http2Client::make(
        const Http2Settings &settings, const Callbacks &callbacks) {
    auto self = std::make_unique<Http2Client>(PrivateAccess{}, settings, callbacks);
    auto error = self->initialize_session();
    if (error != nullptr) {
        return error;
    }

    return self;
}

Http2Client::~Http2Client() = default;

Result<size_t, Http2Error> Http2Client::input(Uint8View chunk) {
    return input_impl(chunk);
}

Result<uint32_t, Http2Error> Http2Client::submit_request(const Request &request, bool eof) {
    std::vector<nghttp2_nv> nv_list;
    nv_list.reserve(std::distance(request.begin(), request.end()));
    std::transform(request.begin(), request.end(), std::back_inserter(nv_list), transform_header<std::string_view>);

    uint32_t stream_id = nghttp2_session_get_next_stream_id(m_session.get());
    if (int status = nghttp2_session_set_next_stream_id(m_session.get(), int32_t(stream_id));
            status != NGHTTP2_NO_ERROR) {
        return make_error(Http2Error{}, AG_FMT("Couldn't set stream ID: {} ({})", nghttp2_strerror(status), status));
    }

    bool head_request = request.method() == "HEAD";
    eof = eof || head_request;

    uint32_t flags = eof ? NGHTTP2_FLAG_END_STREAM : 0;
    if (int status = nghttp2_submit_headers(
                m_session.get(), flags, -1, nullptr, nv_list.data(), nv_list.size(), nullptr);
            status < 0) {
        return make_error(Http2Error{}, AG_FMT("Couldn't submit request: {} ({})", nghttp2_strerror(status), status));
    }

    Stream &stream = m_streams.emplace(stream_id, Stream{}).first->second;
    stream.flags.set(Stream::HEAD_REQUEST, head_request);
    stream.flags.set(Stream::HAS_EOF, eof);
    return stream_id;
}

Error<Http2Error> Http2Client::submit_trailer(uint32_t stream_id, const Headers &headers) {
    return submit_trailer_impl(stream_id, headers);
}

Error<Http2Error> Http2Client::submit_body(uint32_t stream_id, Uint8View chunk, bool eof) {
    return submit_body_impl(stream_id, chunk, eof);
}

Error<Http2Error> Http2Client::reset_stream(uint32_t stream_id, nghttp2_error_code error_code) {
    return reset_stream_impl(stream_id, error_code);
}

void Http2Client::set_session_close_error(nghttp2_error_code error_code) {
    m_error = error_code;
}

Error<Http2Error> Http2Client::consume_connection(size_t length) {
    return consume_connection_impl(length);
}

Error<Http2Error> Http2Client::consume_stream(uint32_t stream_id, size_t length) {
    return consume_stream_impl(stream_id, length);
}

Error<Http2Error> Http2Client::flush() {
    return flush_impl();
}

} // namespace ag::http
