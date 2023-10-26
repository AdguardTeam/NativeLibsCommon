#include <atomic>
#include <cassert>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "common/http/http3.h"
#include "common/http/util.h"
#include "common/logger.h"
#include "common/utils.h"

#define log_id(lvl_, id_, fmt_, ...) lvl_##log(g_logger, "[{}] " fmt_, id_, ##__VA_ARGS__)
#define log_sid(lvl_, id_, stream_, fmt_, ...) lvl_##log(g_logger, "[{}-{}] " fmt_, id_, stream_, ##__VA_ARGS__)

namespace ag::http {

static const Logger g_logger("H3");    // NOLINT(*-identifier-naming)
static std::atomic_uint32_t g_next_id; // NOLINT(*-avoid-non-const-global-variables)

// Typical DCID length in an initial packet (SCID is zero bytes length in case of Chrome)
static constexpr size_t ORIGINAL_DCID_DATALEN = 18;
// Typical DCID length after handshake
static constexpr size_t ACCEPTED_DCID_DATA_LEN = 12;
static constexpr uint64_t ACTIVE_CONNECTION_ID_LIMIT = 7;

template <typename T>
static constexpr nghttp3_nv transform_header(const Header<T> &header) {
    return nghttp3_nv{
            .name = (uint8_t *) header.name.data(),
            .value = (uint8_t *) header.value.data(),
            .namelen = header.name.size(),
            .valuelen = header.value.size(),
    };
}

template <typename T>
int Http3Session<T>::on_begin_headers(nghttp3_conn *, int64_t stream_id, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "...");

    Stream &stream = self->m_streams.emplace(stream_id, Stream{}).first->second;
    if (stream.message.has_value()) {
        log_sid(warn, self->m_id, stream_id, "Another headers is already in progress: {}", stream.message.value());
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    stream.message.emplace(HTTP_3_0);

    return 0;
}

template <typename T>
int Http3Session<T>::on_recv_header(nghttp3_conn *, int64_t stream_id, int32_t, nghttp3_rcbuf *name_buf,
        nghttp3_rcbuf *value_buf, uint8_t, void *arg, void *) {
    auto *self = (Http3Session *) arg;

    nghttp3_vec name_ = nghttp3_rcbuf_get_buf(name_buf);   // NOLINT(*-identifier-naming)
    nghttp3_vec value_ = nghttp3_rcbuf_get_buf(value_buf); // NOLINT(*-identifier-naming)
    std::string_view name = {(char *) name_.base, name_.len};
    std::string_view value = {(char *) value_.base, value_.len};
    log_sid(trace, self->m_id, stream_id, "{}: {}", name, value);

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    if (!stream.message.has_value()) {
        log_sid(warn, self->m_id, stream_id, "Stream has no pending message");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Message &message = stream.message.value();
    static_assert(std::is_same_v<T, Http3Server> || std::is_same_v<T, Http3Client>);
    if constexpr (std::is_same_v<T, Http3Server>) {
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
                log_sid(dbg, self->m_id, stream_id, "Couldn't parse status code: {}", value);
                return NGHTTP3_ERR_CALLBACK_FAILURE;
            }
            message.status_code(code.value());
            return 0;
        }
    }

    message.headers().put(std::string{name}, std::string{value});

    return 0;
}

template <typename T>
int Http3Session<T>::on_end_headers(nghttp3_conn *, int64_t stream_id, int fin, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "...");

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    Message message = std::move(std::exchange(stream.message, std::nullopt).value());
    message.headers().has_body(!fin);

    static_assert(std::is_same_v<T, Http3Server> || std::is_same_v<T, Http3Client>);
    if constexpr (std::is_same_v<T, Http3Server>) {
        stream.flags.set(Stream::HEAD_REQUEST, message.method() == "HEAD");
        if (const auto &h = static_cast<T *>(self)->m_handler; h.on_request != nullptr) {
            h.on_request(h.arg, stream_id, std::move(message));
        }
    } else {
        if (const auto &h = static_cast<T *>(self)->m_handler; h.on_response != nullptr) {
            h.on_response(h.arg, stream_id, std::move(message));
        }
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_begin_trailers(nghttp3_conn *, int64_t stream_id, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "...");

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    if (stream.message.has_value()) {
        log_sid(warn, self->m_id, stream_id, "Another headers is already in progress: {}", stream.message.value());
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    stream.message.emplace(HTTP_3_0);

    return 0;
}

template <typename T>
int Http3Session<T>::on_recv_trailer(nghttp3_conn *, int64_t stream_id, int32_t, nghttp3_rcbuf *name_buf,
        nghttp3_rcbuf *value_buf, uint8_t, void *arg, void *) {
    auto *self = (Http3Session *) arg;

    nghttp3_vec name_ = nghttp3_rcbuf_get_buf(name_buf);   // NOLINT(*-identifier-naming)
    nghttp3_vec value_ = nghttp3_rcbuf_get_buf(value_buf); // NOLINT(*-identifier-naming)
    std::string_view name = {(char *) name_.base, name_.len};
    std::string_view value = {(char *) value_.base, value_.len};
    log_sid(trace, self->m_id, stream_id, "{}: {}", name, value);

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    if (!stream.message.has_value()) {
        log_sid(warn, self->m_id, stream_id, "Stream has no pending message");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Message &message = stream.message.value();
    message.headers().put(std::string{name}, std::string{value});

    return 0;
}

template <typename T>
int Http3Session<T>::on_end_trailers(nghttp3_conn *, int64_t stream_id, int, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "...");

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    Message message = std::move(std::exchange(stream.message, std::nullopt).value());

    if (const auto &h = static_cast<T *>(self)->m_handler; h.on_trailer_headers != nullptr) {
        h.on_trailer_headers(h.arg, stream_id, Message::into_headers(std::move(message)));
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_data_chunk_recv(
        nghttp3_conn *, int64_t stream_id, const uint8_t *data, size_t len, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "{}", len);

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    if (const auto &h = static_cast<T *>(self)->m_handler; h.on_body != nullptr) {
        h.on_body(h.arg, stream_id, {data, len});
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_h3_stop_sending(nghttp3_conn *, int64_t stream_id, uint64_t app_error_code, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "app_error_code={}", app_error_code);

    if (int status = ngtcp2_conn_shutdown_stream_write(self->m_quic_conn.get(), 0, stream_id, app_error_code);
            status != NGTCP2_NO_ERROR) {
        log_sid(dbg, self->m_id, stream_id, "Couldn't shutdown stream write: {} ({})", ngtcp2_strerror(status), status);
        return -1;
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_quic_stream_stop_sending(
        ngtcp2_conn *, int64_t stream_id, uint64_t app_error_code, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "app_error_code={}", app_error_code);

    nghttp3_conn_shutdown_stream_write(self->m_http_conn.get(), stream_id);

    return 0;
}

template <typename T>
int Http3Session<T>::on_end_stream(nghttp3_conn *, int64_t stream_id, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "...");

    if (const auto &h = static_cast<T *>(self)->m_handler; h.on_stream_read_finished != nullptr) {
        h.on_stream_read_finished(h.arg, stream_id);
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_h3_reset_stream(nghttp3_conn *, int64_t stream_id, uint64_t app_error_code, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "app_error_code={}", app_error_code);

    if (int status = ngtcp2_conn_shutdown_stream_read(self->m_quic_conn.get(), 0, stream_id, app_error_code);
            status != NGTCP2_NO_ERROR) {
        log_sid(dbg, self->m_id, stream_id, "Couldn't shutdown stream read: {} ({})", ngtcp2_strerror(status), status);
        return -1;
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_quic_stream_reset(
        ngtcp2_conn *, int64_t stream_id, uint64_t, uint64_t app_error_code, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "app_error_code={}", app_error_code);

    if (int status = nghttp3_conn_shutdown_stream_read(self->m_http_conn.get(), stream_id); status != 0) {
        log_sid(dbg, self->m_id, stream_id, "Couldn't shutdown stream read: {} ({})", nghttp3_strerror(status), status);
        return -1;
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_quic_stream_close(
        ngtcp2_conn *, uint32_t flags, int64_t stream_id, uint64_t app_error_code, void *arg, void *) {
    auto *self = (Http3Session *) arg;

    if (app_error_code == 0 || !(flags & NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)) {
        app_error_code = NGHTTP3_H3_NO_ERROR;
    }

    int status = nghttp3_conn_close_stream(self->m_http_conn.get(), stream_id, app_error_code);

    if (std::is_same_v<T, Http3Server> && ngtcp2_is_bidi_stream(stream_id)) {
        assert(!ngtcp2_conn_is_local_stream(self->m_quic_conn.get(), stream_id));
        ngtcp2_conn_extend_max_streams_bidi(self->m_quic_conn.get(), 1);
    }

    switch (status) {
    case 0:
    case NGHTTP3_ERR_STREAM_NOT_FOUND:
        break;
    default:
        log_sid(dbg, self->m_id, stream_id, "Couldn't close stream: {} ({})", nghttp3_strerror(status), status);
        ngtcp2_ccerr_set_application_error(
                &self->m_last_error, nghttp3_err_infer_quic_app_error_code(status), nullptr, 0);
        return -1;
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_h3_stream_close(nghttp3_conn *, int64_t stream_id, uint64_t error_code, void *arg, void *) {
    auto *self = (Http3Session *) arg;
    log_sid(trace, self->m_id, stream_id, "{} ({})", nghttp3_strerror(error_code), error_code);

    auto node = self->m_streams.extract(stream_id);
    if (node.empty()) {
        log_sid(warn, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    self->close_stream(stream_id, int(error_code));

    return 0;
}

template <typename T>
void Http3Session<T>::close_stream(uint32_t stream_id, int error_code) {
    if (const auto &h = static_cast<T *>(this)->m_handler; h.on_stream_closed != nullptr) {
        h.on_stream_closed(h.arg, stream_id, error_code);
    }
}

template <typename T>
int Http3Session<T>::on_handshake_completed(ngtcp2_conn *, void *arg) {
    auto *self = (Http3Session *) arg;
    auto *quic_conn = self->m_quic_conn.get();
    auto *h3_conn = self->m_http_conn.get();
    int64_t ctrl_stream_id;      // NOLINT(*-init-variables)
    int64_t qpack_enc_stream_id; // NOLINT(*-init-variables)
    int64_t qpack_dec_stream_id; // NOLINT(*-init-variables)
    int status = ngtcp2_conn_open_uni_stream(quic_conn, &ctrl_stream_id, nullptr);
    if (status != NGTCP2_NO_ERROR) {
        log_id(dbg, self->m_id, "ngtcp2_conn_open_uni_stream: {} ({})", ngtcp2_strerror(status), status);
        return -1;
    }
    status = ngtcp2_conn_open_uni_stream(quic_conn, &qpack_enc_stream_id, nullptr);
    if (status != NGTCP2_NO_ERROR) {
        log_id(dbg, self->m_id, "ngtcp2_conn_open_uni_stream: {} ({})", ngtcp2_strerror(status), status);
        return -1;
    }
    status = ngtcp2_conn_open_uni_stream(quic_conn, &qpack_dec_stream_id, nullptr);
    if (status != NGTCP2_NO_ERROR) {
        log_id(dbg, self->m_id, "ngtcp2_conn_open_uni_stream: {} ({})", ngtcp2_strerror(status), status);
        return -1;
    }
    status = nghttp3_conn_bind_control_stream(h3_conn, ctrl_stream_id);
    if (status != 0) {
        log_id(dbg, self->m_id, "nghttp3_conn_bind_control_stream: {} ({})", nghttp3_strerror(status), status);
        return -1;
    }
    status = nghttp3_conn_bind_qpack_streams(h3_conn, qpack_enc_stream_id, qpack_dec_stream_id);
    if (status != 0) {
        log_id(dbg, self->m_id, "nghttp3_conn_bind_qpack_streams: {} ({})", nghttp3_strerror(status), status);
        return -1;
    }

    self->m_handshake_completed = true;
    if constexpr (std::is_same_v<T, Http3Client>) {
        if (const auto &h = static_cast<T *>(self)->m_handler; h.on_handshake_completed != nullptr) {
            h.on_handshake_completed(h.arg);
        }
    }

    return 0;
}

static ngtcp2_tstamp ts() {
    using namespace std::chrono; // NOLINT(*-build-using-namespace)
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

template <typename T>
void Http3Session<T>::log_quic(void *arg, const char *format, ...) { // NOLINT(*-dcl50-cpp)
    auto *self = (Http3Session *) arg;
    va_list args; // NOLINT(*-init-variables)
    if (g_logger.is_enabled(LOG_LEVEL_TRACE)) {
        va_start(args, format);
        int n = std::vsnprintf(nullptr, 0, format, args);
        va_end(args);
        if (n > 0) {
            std::string buf(n, '\0');
            va_start(args, format);
            std::vsnprintf(buf.data(), buf.size() + 1, format, args); // NOLINT(*-err33-c)
            va_end(args);
            log_id(trace, self->m_id, "{}", buf);
        }
    }
}

static ngtcp2_cid http3_dcid_from_scid(ngtcp2_cid scid) {
    ngtcp2_cid dcid = {
            .datalen = ACCEPTED_DCID_DATA_LEN,
    };
    uint32_t ret_len = sizeof(dcid.data);
    EVP_Digest((const char *) scid.data, scid.datalen, dcid.data, &ret_len, EVP_sha1(), nullptr);
    return dcid;
}

template <typename T>
Http3Session<T>::Http3Session(const Http3Settings &settings)
        : m_id(g_next_id.fetch_add(1, std::memory_order_relaxed))
        , m_ref({
                  .get_conn =
                          [](ngtcp2_crypto_conn_ref *conn_ref) {
                              auto *self = (Http3Session *) conn_ref->user_data;
                              return self->m_quic_conn.get();
                          },
                  .user_data = this,
          })
        , m_settings(settings) {
    ngtcp2_ccerr_default(&m_last_error);
}

template <typename T>
Http3Session<T>::~Http3Session() {
    for (auto &[stream_id, _] : std::exchange(m_streams, {})) {
        close_stream(stream_id, NGHTTP3_H3_REQUEST_CANCELLED);
    }

    if constexpr (std::is_same_v<T, Http3Server>) {
        if (auto *server = static_cast<T *>(this); server->m_drop_silently) {
            return;
        }
    }

    if (m_http_conn != nullptr && m_last_error.error_code == 0 && m_handshake_completed) {
        // Graceful shutdown
        if (int status = nghttp3_conn_shutdown(m_http_conn.get()); status != 0) {
            log_id(dbg, m_id, "Couldn't shutdown connection: {} ({})", nghttp3_strerror(status), status);
        }
    }

    if (m_quic_conn != nullptr) {
        close_connection();
    }
}

static constexpr ngtcp2_cc_algo to_ng_cc_algo(Http3Settings::CongestionControlAlgorithm x) {
    switch (x) {
    case Http3Settings::RENO:
        return NGTCP2_CC_ALGO_RENO;
    case Http3Settings::CUBIC:
        return NGTCP2_CC_ALGO_CUBIC;
    case Http3Settings::BBR:
        return NGTCP2_CC_ALGO_BBR;
    }
}

#ifndef NDEBUG
static void log_http3(const char *format, va_list args) {
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
Error<Http3Error> Http3Session<T>::initialize_session(
        const QuicNetworkPath &path, bssl::UniquePtr<SSL> ssl, ngtcp2_cid client_scid, ngtcp2_cid client_dcid) {
    if (ssl == nullptr) {
        return make_error(Http3Error{}, "SSL handle mustn't be null");
    }

    RAND_bytes(m_static_secret.data(), m_static_secret.size());

    ngtcp2_path_storage path_storage;
    ngtcp2_path_storage_init(&path_storage, path.local, path.local_len, path.remote, path.remote_len, nullptr);

    m_ssl = std::move(ssl);
    SSL_set_app_data(m_ssl.get(), &m_ref);

    nghttp3_callbacks h3_callbacks{
            .acked_stream_data = on_acked_stream_data,
            .stream_close = on_h3_stream_close,
            .recv_data = on_data_chunk_recv,
            .deferred_consume = on_deferred_consume,
            .begin_headers = on_begin_headers,
            .recv_header = on_recv_header,
            .end_headers = on_end_headers,
            .begin_trailers = on_begin_trailers,
            .recv_trailer = on_recv_trailer,
            .end_trailers = on_end_trailers,
            .stop_sending = on_h3_stop_sending,
            .end_stream = on_end_stream,
            .reset_stream = on_h3_reset_stream,
    };

    ngtcp2_callbacks quic_callbacks{
            .client_initial = ngtcp2_crypto_client_initial_cb,
            .recv_client_initial = ngtcp2_crypto_recv_client_initial_cb,
            .recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb,
            .handshake_completed = on_handshake_completed,
            .encrypt = ngtcp2_crypto_encrypt_cb,
            .decrypt = ngtcp2_crypto_decrypt_cb,
            .hp_mask = ngtcp2_crypto_hp_mask_cb,
            .recv_stream_data =
                    [](ngtcp2_conn *, uint32_t flags, int64_t stream_id, uint64_t, const uint8_t *data, size_t datalen,
                            void *arg, void *) {
                        auto *self = (Http3Session *) arg;
                        return self->recv_h3_stream_data(
                                stream_id, {data, datalen}, flags & NGTCP2_STREAM_DATA_FLAG_FIN);
                    },
            .acked_stream_data_offset =
                    [](ngtcp2_conn *, int64_t stream_id, uint64_t, uint64_t datalen, void *arg, void *) {
                        auto *self = (Http3Session *) arg;
                        return nghttp3_conn_add_ack_offset(self->m_http_conn.get(), stream_id, datalen);
                    },
            .stream_close = on_quic_stream_close,
            .recv_retry = ngtcp2_crypto_recv_retry_cb,
            .rand =
                    [](uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *) {
                        RAND_bytes(dest, destlen);
                    },
            .get_new_connection_id =
                    [](ngtcp2_conn *, ngtcp2_cid *cid, uint8_t *token, size_t cidlen, void *arg) {
                        auto *self = (Http3Session *) arg;
                        cid->datalen = cidlen;
                        RAND_bytes(cid->data, cid->datalen);
                        return self->derive_token({cid->data, cid->datalen}, {token, NGTCP2_STATELESS_RESET_TOKENLEN})
                                ? 0
                                : NGTCP2_ERR_CALLBACK_FAILURE;
                    },
            .update_key = ngtcp2_crypto_update_key_cb,
            .stream_reset = on_quic_stream_reset,
            .extend_max_remote_streams_bidi =
                    [](ngtcp2_conn *, uint64_t max_streams, void *arg) {
                        auto *self = (Http3Session *) arg;
                        nghttp3_conn_set_max_client_streams_bidi(self->m_http_conn.get(), max_streams);
                        return 0;
                    },
            .extend_max_stream_data =
                    [](ngtcp2_conn *, int64_t stream_id, uint64_t, void *arg, void *) {
                        auto *self = (Http3Session *) arg;
                        int status = nghttp3_conn_unblock_stream(self->m_http_conn.get(), stream_id);
                        if (status != 0) {
                            log_sid(dbg, self->m_id, stream_id, "Couldn't unblock http3 stream: {} ({})",
                                    nghttp3_strerror(status), status);
                            return NGTCP2_ERR_CALLBACK_FAILURE;
                        }
                        return 0;
                    },
            .delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
            .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
            .get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb,
            .stream_stop_sending = on_quic_stream_stop_sending,
            .version_negotiation = ngtcp2_crypto_version_negotiation_cb,
    };

    ngtcp2_settings quic_settings;
    ngtcp2_settings_default(&quic_settings);
    quic_settings.cc_algo = to_ng_cc_algo(m_settings.congestion_control_algorithm);
    quic_settings.initial_ts = ts();
    quic_settings.initial_rtt = NGTCP2_DEFAULT_INITIAL_RTT / 2;
    if (g_logger.is_enabled(LOG_LEVEL_TRACE)) {
        quic_settings.log_printf = log_quic;
    }
    quic_settings.max_tx_udp_payload_size = m_settings.max_tx_udp_payload_size;

    ngtcp2_transport_params transport_params;
    ngtcp2_transport_params_default(&transport_params);
    transport_params.initial_max_stream_data_bidi_local = m_settings.initial_max_stream_data_bidi_local;
    transport_params.initial_max_stream_data_bidi_remote = m_settings.initial_max_stream_data_bidi_remote;
    transport_params.initial_max_stream_data_uni = m_settings.initial_max_stream_data_uni;
    transport_params.initial_max_data = m_settings.initial_max_data;
    transport_params.initial_max_streams_bidi = m_settings.initial_max_streams_bidi;
    transport_params.max_idle_timeout =
            std::chrono::duration_cast<ag::Nanos>(m_settings.max_idle_timeout).count() * NGTCP2_NANOSECONDS;
    transport_params.initial_max_streams_uni = m_settings.initial_max_streams_uni;
    transport_params.active_connection_id_limit = ACTIVE_CONNECTION_ID_LIMIT;

    nghttp3_settings h3_settings;
    nghttp3_settings_default(&h3_settings);

#ifndef NDEBUG
    if (g_logger.is_enabled(LOG_LEVEL_TRACE)) {
        // works only in case `DEBUGBUILD` is defined for nghttp3
        nghttp3_set_debug_vprintf_callback(log_http3);
    }
#endif

    ngtcp2_conn *quic_conn = nullptr;
    nghttp3_conn *h3_conn = nullptr;
    static_assert(std::is_same_v<T, Http3Server> || std::is_same_v<T, Http3Client>);
    if constexpr (std::is_same_v<T, Http3Server>) {
        transport_params.original_dcid = client_dcid;
        transport_params.original_dcid_present = true;
        ngtcp2_cid server_scid = http3_dcid_from_scid(client_scid);
        if (!derive_token({server_scid.data, server_scid.datalen},
                    {transport_params.stateless_reset_token, std::size(transport_params.stateless_reset_token)})) {
            return make_error(Http3Error{}, "Couldn't forge stateless reset token");
        }
        transport_params.stateless_reset_token_present = true;

        if (int status = ngtcp2_conn_server_new(&quic_conn, &client_scid, &server_scid, &path_storage.path,
                    NGTCP2_PROTO_VER_V1, &quic_callbacks, &quic_settings, &transport_params, nullptr, this);
                status != NGTCP2_NO_ERROR) {
            return make_error(
                    Http3Error{}, AG_FMT("Couldn't create quic connection: {} ({})", ngtcp2_strerror(status), status));
        }

        if (int status = nghttp3_conn_server_new(&h3_conn, &h3_callbacks, &h3_settings, nghttp3_mem_default(), this);
                status != 0) {
            ngtcp2_conn_del(quic_conn);
            return make_error(Http3Error{},
                    AG_FMT("Couldn't create http3 connection: {} ({})", nghttp3_strerror(status), status));
        }
    } else {
        if (client_dcid.datalen == 0) {
            client_dcid.datalen = ORIGINAL_DCID_DATALEN;
            RAND_bytes(client_dcid.data, client_dcid.datalen);
        }

        if (int status = ngtcp2_conn_client_new(&quic_conn, &client_dcid, &client_scid, &path_storage.path,
                    NGTCP2_PROTO_VER_V1, &quic_callbacks, &quic_settings, &transport_params, nullptr, this);
                status != NGTCP2_NO_ERROR) {
            return make_error(
                    Http3Error{}, AG_FMT("Couldn't create quic connection: {} ({})", ngtcp2_strerror(status), status));
        }

        if (int status = nghttp3_conn_client_new(&h3_conn, &h3_callbacks, &h3_settings, nghttp3_mem_default(), this);
                status != 0) {
            ngtcp2_conn_del(quic_conn);
            return make_error(Http3Error{},
                    AG_FMT("Couldn't create http3 connection: {} ({})", nghttp3_strerror(status), status));
        }
    }

    m_quic_conn.reset(quic_conn);
    m_http_conn.reset(h3_conn);

    ngtcp2_conn_set_tls_native_handle(m_quic_conn.get(), m_ssl.get());

    return {};
}

template <typename T>
int Http3Session<T>::input_impl(const QuicNetworkPath &path, Uint8View chunk) {
    log_id(trace, m_id, "Length={}", chunk.length());

    ngtcp2_path_storage path_storage{};
    ngtcp2_path_storage_init(&path_storage, path.local, path.local_len, path.remote, path.remote_len, nullptr);

    return ngtcp2_conn_read_pkt(m_quic_conn.get(), &path_storage.path, nullptr, chunk.data(), chunk.length(), ts());
}

template <typename T>
Error<Http3Error> Http3Session<T>::submit_trailer_impl(uint64_t stream_id, const Headers &headers) {
    auto iter = m_streams.find(stream_id);
    if (iter == m_streams.end()) {
        return make_error(Http3Error{}, "Stream not found");
    }

    Stream &stream = iter->second;

    std::vector<nghttp3_nv> nv_list;
    nv_list.reserve(std::distance(headers.begin(), headers.end()));
    std::transform(headers.begin(), headers.end(), std::back_inserter(nv_list), transform_header<std::string>);

    if (int status =
                    nghttp3_conn_submit_trailers(m_http_conn.get(), int32_t(stream_id), nv_list.data(), nv_list.size());
            status != 0) {
        return make_error(Http3Error{}, AG_FMT("submit_trailers(): {} ({})", nghttp3_strerror(status), status));
    }

    if (int status = nghttp3_conn_resume_stream(m_http_conn.get(), int64_t(stream_id)); status != 0) {
        return make_error(Http3Error{}, AG_FMT("Couldn't resume stream: {} ({})", nghttp3_strerror(status), status));
    }

    stream.flags.set(Stream::TRAILERS_SUBMITTED);

    return {};
}

template <typename T>
int Http3Session<T>::recv_h3_stream_data(int64_t stream_id, Uint8View chunk, bool eof) {
    log_sid(trace, m_id, stream_id, "Length={}, eof={}", chunk.length(), eof);

    nghttp3_ssize r = nghttp3_conn_read_stream(m_http_conn.get(), stream_id, chunk.data(), chunk.length(), eof);
    if (r < 0) {
        log_sid(dbg, m_id, stream_id, "Couldn't read stream: {} ({})", nghttp3_strerror(r), r);
        ngtcp2_ccerr_set_application_error(&m_last_error, nghttp3_err_infer_quic_app_error_code(int(r)), nullptr, 0);
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    if (Error<Http3Error> error = consume_stream_impl(stream_id, r); error != nullptr) {
        log_sid(dbg, m_id, stream_id, "Couldn't consume stream: {}", error->str());
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }

    return 0;
}

template <typename T>
Error<Http3Error> Http3Session<T>::push_data(Stream &stream, Uint8View chunk, bool eof) {
    if (stream.data_source.buffer == nullptr) {
        stream.data_source.buffer.reset(evbuffer_new());
    }
    stream.flags.set(Stream::HAS_EOF, eof);
    if (0 != evbuffer_add(stream.data_source.buffer.get(), chunk.data(), chunk.size())) {
        return make_error(Http3Error{}, "Couldn't write data in buffer");
    }

    return {};
}

template <typename T>
Error<Http3Error> Http3Session<T>::submit_body_impl(uint64_t stream_id, Uint8View chunk, bool eof) {
    log_sid(trace, m_id, stream_id, "Length={} eof={}", chunk.length(), eof);

    auto iter = m_streams.find(stream_id);
    if (iter == m_streams.end()) {
        return make_error(Http3Error{}, "Stream not found");
    }

    Stream &stream = iter->second;
    if (Error<Http3Error> error = push_data(stream, chunk, eof); error != nullptr) {
        return make_error(Http3Error{}, error);
    }
    if (int status = nghttp3_conn_resume_stream(m_http_conn.get(), int64_t(stream_id)); status != 0) {
        return make_error(Http3Error{}, AG_FMT("Couldn't resume stream: {} ({})", nghttp3_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http3Error> Http3Session<T>::reset_stream_impl(uint64_t stream_id, int error_code) {
    log_sid(trace, m_id, stream_id, "Error={}", error_code);

    if (int status = ngtcp2_conn_shutdown_stream(m_quic_conn.get(), 0, int64_t(stream_id), error_code);
            status != NGTCP2_NO_ERROR) {
        return make_error(Http3Error{}, AG_FMT("Couldn't shutdown stream: {} ({})", ngtcp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http3Error> Http3Session<T>::consume_connection_impl(size_t length) {
    ngtcp2_conn_extend_max_offset(m_quic_conn.get(), length);
    return {};
}

template <typename T>
Error<Http3Error> Http3Session<T>::consume_stream_impl(uint64_t stream_id, size_t length) {
    if (Error<Http3Error> error = consume_connection_impl(length); error != nullptr) {
        return make_error(Http3Error{}, "Couldn't consume connection", error);
    }

    if (int status = ngtcp2_conn_extend_max_stream_offset(m_quic_conn.get(), int64_t(stream_id), length);
            status != NGTCP2_NO_ERROR) {
        return make_error(Http3Error{}, AG_FMT("Couldn't consume stream: {} ({})", ngtcp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http3Error> Http3Session<T>::handle_expiry_impl() {
    int status = ngtcp2_conn_handle_expiry(m_quic_conn.get(), ts());
    if (status != NGTCP2_NO_ERROR) {
        return make_error(Http3Error{}, AG_FMT("{} ({})", ngtcp2_strerror(status), status));
    }

    return {};
}

template <typename T>
Error<Http3Error> Http3Session<T>::flush_impl() {
    log_id(trace, m_id, "...");

    ngtcp2_path_storage path;
    ngtcp2_path_storage_zero(&path);

    int64_t write_stream_id; // NOLINT(*-init-variables)
    int fin;                 // NOLINT(*-init-variables)
    uint8_t buf[NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE];
    size_t max_buf_size = std::min(std::size(buf), ngtcp2_conn_get_max_tx_udp_payload_size(m_quic_conn.get()));
    for (;;) {
        std::array<nghttp3_vec, 16> vecs{}; // NOLINT(*-magic-numbers)
        nghttp3_ssize vec_num =
                nghttp3_conn_writev_stream(m_http_conn.get(), &write_stream_id, &fin, vecs.data(), vecs.size());
        if (vec_num < 0) {
            if (write_stream_id < 0) {
                log_id(dbg, m_id, "Couldn't write data: {} ({})", ngtcp2_strerror(vec_num), vec_num);
            } else {
                log_sid(dbg, m_id, write_stream_id, "Couldn't write stream data: {} ({})", ngtcp2_strerror(vec_num),
                        vec_num);
            }
            ngtcp2_ccerr_set_application_error(
                    &m_last_error, nghttp3_err_infer_quic_app_error_code(int(vec_num)), nullptr, 0);
            goto loop_exit; // NOLINT(*-avoid-goto)
        }
        ngtcp2_ssize data_len = 0;
        uint32_t flags = 0;
        if (write_stream_id != -1) {
            flags |= NGTCP2_WRITE_STREAM_FLAG_MORE;
        }
        if (fin) {
            flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;
        }
        static_assert(sizeof(ngtcp2_vec) == sizeof(nghttp3_vec));
        static_assert(offsetof(ngtcp2_vec, base) == offsetof(nghttp3_vec, base)
                && std::is_same_v<decltype(ngtcp2_vec{}.base), decltype(nghttp3_vec{}.base)>);
        static_assert(offsetof(ngtcp2_vec, len) == offsetof(nghttp3_vec, len)
                && std::is_same_v<decltype(ngtcp2_vec{}.len), decltype(nghttp3_vec{}.len)>);
        int r = ngtcp2_conn_writev_stream(m_quic_conn.get(), &path.path, nullptr, buf, max_buf_size, &data_len, flags,
                write_stream_id, (const ngtcp2_vec *) vecs.data(), vec_num, ts());
        if (data_len >= 0) {
            if (int status = nghttp3_conn_add_write_offset(m_http_conn.get(), write_stream_id, data_len); status != 0) {
                log_sid(dbg, m_id, write_stream_id, "Couldn't add write offset: {} ({})", ngtcp2_strerror(status),
                        status);
                ngtcp2_ccerr_set_application_error(
                        &m_last_error, nghttp3_err_infer_quic_app_error_code(status), nullptr, 0);
                goto loop_exit; // NOLINT(*-avoid-goto)
            }
        }

        if (r > 0) {
            if (const auto &h = static_cast<T *>(this)->m_handler; h.on_output != nullptr) {
                h.on_output(h.arg,
                        QuicNetworkPath{
                                .local = path.path.local.addr,
                                .local_len = path.path.local.addrlen,
                                .remote = path.path.remote.addr,
                                .remote_len = path.path.remote.addrlen,
                        },
                        {buf, size_t(r)});
            }
            if (write_stream_id != -1) {
                continue;
            }
            goto loop_exit; // NOLINT(*-avoid-goto)
        }

        if (r == NGTCP2_NO_ERROR) {
            goto loop_exit; // NOLINT(*-avoid-goto)
        }

        if (r != NGTCP2_ERR_WRITE_MORE) {
            log_sid(dbg, m_id, write_stream_id, "Write stream packet result: {} ({})", ngtcp2_strerror(r), r);
        }

        switch (r) {
        case NGTCP2_ERR_STREAM_DATA_BLOCKED:
            assert(data_len == -1);
            nghttp3_conn_block_stream(m_http_conn.get(), write_stream_id);
            continue;
        case NGTCP2_ERR_STREAM_SHUT_WR:
            assert(data_len == -1);
            nghttp3_conn_shutdown_stream_write(m_http_conn.get(), write_stream_id);
            continue;
        case NGTCP2_ERR_WRITE_MORE:
            continue;
        case NGTCP2_ERR_DRAINING:
        case NGTCP2_ERR_DROP_CONN:
            goto loop_exit; // NOLINT(*-avoid-goto)
        default:
            log_sid(dbg, m_id, write_stream_id, "Error writing to stream: {} ({})", ngtcp2_strerror(r), r);
            ngtcp2_ccerr_set_application_error(&m_last_error, r, nullptr, 0);
            goto loop_exit; // NOLINT(*-avoid-goto)
        }
    }

loop_exit:
    if (m_last_error.error_code != NGTCP2_NO_ERROR) {
        handle_error();
        return make_error(
                Http3Error{}, AG_FMT("{} ({})", ngtcp2_strerror(m_last_error.error_code), m_last_error.error_code));
    }

    ngtcp2_tstamp now = ts();
    ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(m_quic_conn.get());
    if (const auto &h = static_cast<T *>(this)->m_handler;
            expiry != UINT64_MAX && expiry > now && h.on_expiry_update != nullptr) {
        h.on_expiry_update(h.arg, Nanos(expiry - now));
    }

    return {};
}

template <typename T>
nghttp3_ssize Http3Session<T>::on_read_data(
        nghttp3_conn *, int64_t stream_id, nghttp3_vec *vec, size_t vec_num, uint32_t *pflags, void *arg, void *) {
    auto *self = (Http3Session *) arg;

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(dbg, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    DataSource &data_source = stream.data_source;

    evbuffer_ptr pos{};
    if (0 != evbuffer_ptr_set(data_source.buffer.get(), &pos, data_source.read_offset, EVBUFFER_PTR_SET)) {
        log_sid(dbg, self->m_id, stream_id, "Couldn't set read pointer");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    vec_num = evbuffer_peek(data_source.buffer.get(), -1, &pos, (evbuffer_iovec *) vec, int(vec_num));
    size_t n = nghttp3_vec_len(vec, vec_num);

    data_source.read_offset += n;
    size_t unsent = evbuffer_get_length(data_source.buffer.get()) - data_source.read_offset;

    if (vec_num == 0 && unsent == 0 && stream.flags.test(Stream::TRAILERS_SUBMITTED)) {
        log_sid(trace, self->m_id, stream_id, "No data left in buffers -- set eof flag");
        *pflags |= NGHTTP3_DATA_FLAG_EOF | NGHTTP3_DATA_FLAG_NO_END_STREAM;
        return 0;
    }

    bool eof = unsent == 0 && stream.flags.test(Stream::HAS_EOF);
    if (vec_num == 0 && !eof) {
        log_sid(trace, self->m_id, stream_id, "No work on current buffer");
        return NGHTTP3_ERR_WOULDBLOCK;
    }

    log_sid(trace, self->m_id, stream_id, "vec_num={}, bytes={}, unsent={}", vec_num, n, unsent);

    if (eof) {
        log_sid(trace, self->m_id, stream_id, "No data left in buffers -- set eof flag");
        *pflags |= NGHTTP3_DATA_FLAG_EOF;
    }

    return nghttp3_ssize(vec_num);
}

template <typename T>
int Http3Session<T>::on_acked_stream_data(nghttp3_conn *, int64_t stream_id, uint64_t orig_len, void *arg, void *) {
    auto *self = (Http3Session *) arg;

    auto iter = self->m_streams.find(stream_id);
    if (iter == self->m_streams.end()) {
        log_sid(dbg, self->m_id, stream_id, "Stream not found");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    Stream &stream = iter->second;
    DataSource &data_source = stream.data_source;

    uint64_t len = orig_len;
    if (size_t buffer_size = evbuffer_get_length(data_source.buffer.get()); buffer_size < len) {
        log_sid(warn, self->m_id, stream_id, "Buffer is smaller that acked data length: {} vs {}", buffer_size, len);
        len = buffer_size;
    }

    if (0 != evbuffer_drain(data_source.buffer.get(), len)) {
        log_sid(dbg, self->m_id, stream_id, "Couldn't drain buffer");
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    if (data_source.read_offset < len) {
        log_sid(warn, self->m_id, stream_id, "Read offset is smaller that acked data length: {} vs {}",
                data_source.read_offset, len);
        len = data_source.read_offset;
    }

    data_source.read_offset -= len;

    if (const auto &h = static_cast<T *>(self)->m_handler; h.on_data_sent != nullptr) {
        h.on_data_sent(h.arg, stream_id, orig_len);
    }

    return 0;
}

template <typename T>
int Http3Session<T>::on_deferred_consume(nghttp3_conn *, int64_t stream_id, size_t consumed, void *arg, void *) {
    auto *self = (Http3Session *) arg;

    Error<Http3Error> error = self->consume_stream_impl(stream_id, consumed);
    if (error != nullptr) {
        log_sid(dbg, self->m_id, stream_id, "{}", error->str());
        return NGHTTP3_ERR_CALLBACK_FAILURE;
    }

    return 0;
}

template <typename T>
void Http3Session<T>::handle_rx_connection_close() {
    uint64_t code = 0;
    const ngtcp2_ccerr *error = ngtcp2_conn_get_ccerr(m_quic_conn.get());
    switch (error->type) {
    case NGTCP2_CCERR_TYPE_TRANSPORT: {
        log_id(dbg, m_id, "Connection closed due to transport error 0x{:x}: '{}'", error->error_code,
                std::string_view{(char *) error->reason, error->reasonlen});
        uint64_t tls_alert = error->error_code & 0xffu; // NOLINT(*-magic-numbers)
        code = SSL_R_SSLV3_ALERT_CLOSE_NOTIFY + tls_alert;
        break;
    }
    case NGTCP2_CCERR_TYPE_APPLICATION: {
        log_id(dbg, m_id, "Connection closed due to application error 0x{:x}: '{}'", error->error_code,
                std::string_view{(char *) error->reason, error->reasonlen});
        code = error->error_code & 0xffu; // NOLINT(*-magic-numbers)
        break;
    }
    case NGTCP2_CCERR_TYPE_VERSION_NEGOTIATION:
    case NGTCP2_CCERR_TYPE_IDLE_CLOSE:
        break;
    }

    if (const auto &h = static_cast<T *>(this)->m_handler; h.on_close != nullptr) {
        h.on_close(h.arg, code);
    }
}

template <typename T>
void Http3Session<T>::handle_error() {
    if (m_last_error.type == NGTCP2_CCERR_TYPE_IDLE_CLOSE) {
        return;
    }

    close_connection();
}

template <typename T>
void Http3Session<T>::close_connection() {
    if (m_connection_close_sent) {
        return;
    }

    uint8_t buf[NGTCP2_DEFAULT_MAX_RECV_UDP_PAYLOAD_SIZE];
    size_t max_buf_size = std::min(std::size(buf), ngtcp2_conn_get_max_tx_udp_payload_size(m_quic_conn.get()));
    ngtcp2_path_storage path{};
    ngtcp2_path_storage_zero(&path);

    int status = ngtcp2_conn_write_connection_close(
            m_quic_conn.get(), &path.path, nullptr, buf, max_buf_size, &m_last_error, ts());
    if (status < 0) {
        log_id(dbg, m_id, "Couldn't write connection close: {} ({})", ngtcp2_strerror(status), status);
        return;
    }

    if (const auto &h = static_cast<T *>(this)->m_handler; h.on_output != nullptr) {
        h.on_output(h.arg,
                QuicNetworkPath{
                        .local = path.path.local.addr,
                        .local_len = path.path.local.addrlen,
                        .remote = path.path.remote.addr,
                        .remote_len = path.path.remote.addrlen,
                },
                {buf, size_t(status)});
    }
    m_connection_close_sent = true;
}

template <typename T>
bool Http3Session<T>::derive_token(Uint8View in, Uint8View out) const {
    UniquePtr<EVP_MD_CTX, &EVP_MD_CTX_free> ctx{EVP_MD_CTX_new()};
    if (!EVP_DigestInit(ctx.get(), EVP_md5())) {
        return false;
    }

    EVP_DigestUpdate(ctx.get(), in.data(), in.size());
    EVP_DigestUpdate(ctx.get(), m_static_secret.data(), m_static_secret.size());

    unsigned int out_len = out.size();
    EVP_DigestFinal(ctx.get(), (uint8_t *) out.data(), &out_len);
    return true;
}

Http3Server::Http3Server(PrivateAccess, const Http3Settings &settings, const Callbacks &handler)
        : Http3Session<Http3Server>(settings)
        , m_handler(handler) {
}

Http3Server::~Http3Server() = default;

Result<std::unique_ptr<Http3Server>, Http3Error> Http3Server::accept(const Http3Settings &settings,
        const Callbacks &handler, const QuicNetworkPath &path, bssl::UniquePtr<SSL> ssl, Uint8View packet) {
    ngtcp2_pkt_hd hd{};

    if (int status = ngtcp2_accept(&hd, packet.data(), packet.size()); status != NGTCP2_NO_ERROR) {
        return make_error(Http3Error{}, AG_FMT("ngtcp2_accept(): {} ({})", ngtcp2_strerror(status), status));
    }

    auto self = std::make_unique<Http3Server>(PrivateAccess{}, settings, handler);
    auto error = self->initialize_session(path, std::move(ssl), hd.scid, hd.dcid);
    if (error != nullptr) {
        return error;
    }

    return self;
}

Result<std::vector<uint8_t>, Http3Error> Http3Server::prepare_retry(
        const ngtcp2_pkt_hd &hd, const sockaddr *remote, size_t remote_len, size_t max_packet_len) const {
    ngtcp2_cid scid = {
            .datalen = ORIGINAL_DCID_DATALEN,
    };
    RAND_bytes(scid.data, scid.datalen);

    uint8_t token[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN];

    ssize_t token_len = ngtcp2_crypto_generate_retry_token(token, m_static_secret.data(), m_static_secret.size(),
            hd.version, remote, remote_len, &scid, &hd.dcid, ts());
    if (token_len < 0) {
        return make_error(Http3Error{}, "Couldn't generate token");
    }

    log_id(trace, m_id, "Generated address validation token: {}", utils::encode_to_hex({token, size_t(token_len)}));

    std::vector<uint8_t> buffer;
    buffer.resize(std::min(size_t(NGTCP2_MAX_UDP_PAYLOAD_SIZE), max_packet_len));

    ssize_t n = ngtcp2_crypto_write_retry(
            buffer.data(), buffer.size(), hd.version, &hd.scid, &scid, &hd.dcid, token, token_len);
    if (n < 0) {
        return make_error(Http3Error{}, "Couldn't write retry packet");
    }

    buffer.resize(n);
    return buffer;
}

Result<Http3Server::InputResult, Http3Error> Http3Server::input(const QuicNetworkPath &path, Uint8View chunk) {
    int status = input_impl(path, chunk);

    Error<Http3Error> error;
    switch (status) {
    case NGTCP2_NO_ERROR:
        return EATEN;
    case NGTCP2_ERR_DROP_CONN:
        m_drop_silently = true;
        error = make_error(Http3Error{}, AG_FMT("{} ({})", ngtcp2_strerror(status), status));
        break;
    case NGTCP2_ERR_DRAINING:
        if (!m_handled_rx_connection_close) {
            m_handled_rx_connection_close = true;
            handle_rx_connection_close();
        }
        return EATEN;
    case NGTCP2_ERR_RETRY:
        return SEND_RETRY;
    case NGTCP2_ERR_CRYPTO: {
        uint8_t alert = ngtcp2_conn_get_tls_alert(m_quic_conn.get());
        if (m_last_error.error_code == 0) {
            ngtcp2_ccerr_set_tls_alert(&m_last_error, alert, nullptr, 0);
        }
        error = make_error(Http3Error{},
                AG_FMT("QUIC error: {} ({}), TLS alert: {} ({})", ngtcp2_strerror(status), status,
                        SSL_alert_desc_string_long(alert), SSL_alert_type_string_long(alert)));
        break;
    }
    default:
        if (m_last_error.error_code == 0) {
            ngtcp2_ccerr_set_liberr(&m_last_error, status, nullptr, 0);
        }
        error = make_error(Http3Error{}, AG_FMT("{} ({})", ngtcp2_strerror(status), status));
        break;
    }

    handle_error();
    return error;
}

Error<Http3Error> Http3Server::submit_response(uint64_t stream_id, const Response &response, bool eof) {
    auto iter = m_streams.find(stream_id);
    if (iter == m_streams.end()) {
        return make_error(Http3Error{}, "Stream not found");
    }

    Stream &stream = iter->second;
    eof = eof || stream.flags.test(Stream::HEAD_REQUEST);

    std::vector<nghttp3_nv> nv_list;
    nv_list.reserve(std::distance(response.begin(), response.end()));
    std::transform(response.begin(), response.end(), std::back_inserter(nv_list), transform_header<std::string_view>);

    nghttp3_data_reader reader{};
    if (!eof && stream.data_source.buffer == nullptr) {
        stream.data_source.buffer.reset(evbuffer_new());
        reader = {
                .read_data = on_read_data,
        };
    }

    if (int status = nghttp3_conn_submit_response(
                m_http_conn.get(), int64_t(stream_id), nv_list.data(), nv_list.size(), eof ? nullptr : &reader);
            status != 0) {
        return make_error(Http3Error{}, AG_FMT("{} ({})", nghttp3_strerror(status), status));
    }

    if (int status = nghttp3_conn_resume_stream(m_http_conn.get(), int64_t(stream_id)); status != 0) {
        return make_error(Http3Error{}, AG_FMT("Couldn't resume stream: {} ({})", nghttp3_strerror(status), status));
    }

    return {};
}

Error<Http3Error> Http3Server::submit_trailer(uint64_t stream_id, const ag::http::Headers &headers) {
    return submit_trailer_impl(stream_id, headers);
}

Error<Http3Error> Http3Server::submit_body(uint64_t stream_id, Uint8View chunk, bool eof) {
    return submit_body_impl(stream_id, chunk, eof);
}

Error<Http3Error> Http3Server::reset_stream(uint64_t stream_id, int error_code) {
    return reset_stream_impl(stream_id, error_code);
}

void Http3Server::set_session_close_error(int error_code, Uint8View reason) {
    if (error_code > SSL_R_SSLV3_ALERT_CLOSE_NOTIFY) {
        int alert_code = error_code - SSL_R_SSLV3_ALERT_CLOSE_NOTIFY;
        std::string_view alert_string = SSL_alert_desc_string_long(alert_code);
        ngtcp2_ccerr_set_tls_alert(
                &m_last_error, alert_code, (const uint8_t *) alert_string.data(), alert_string.size());
    } else if (error_code > NGHTTP3_H3_NO_ERROR) {
        ngtcp2_ccerr_set_application_error(&m_last_error, error_code, reason.data(), reason.length());
    }
}

Error<Http3Error> Http3Server::consume_stream(uint64_t stream_id, size_t length) {
    return consume_stream_impl(stream_id, length);
}

Error<Http3Error> Http3Server::consume_connection(size_t length) {
    return consume_connection_impl(length);
}

Error<Http3Error> Http3Server::handle_expiry() {
    return handle_expiry_impl();
}

Error<Http3Error> Http3Server::flush() {
    return flush_impl();
}

Nanos Http3Server::probe_timeout() const {
    return Nanos{m_drop_silently ? 0 : ngtcp2_conn_get_pto(m_quic_conn.get())};
}

Http3Client::Http3Client(PrivateAccess, const Http3Settings &settings, const Callbacks &handler)
        : Http3Session<Http3Client>(settings)
        , m_handler(handler) {
}

Result<std::unique_ptr<Http3Client>, Http3Error> Http3Client::connect(const Http3Settings &settings,
        const Callbacks &handler, const QuicNetworkPath &path, bssl::UniquePtr<SSL> ssl) {
    auto self = std::make_unique<Http3Client>(PrivateAccess{}, settings, handler);
    auto error = self->initialize_session(path, std::move(ssl), {}, {});
    if (error != nullptr) {
        return error;
    }

    return self;
}

Http3Client::~Http3Client() = default;

Error<Http3Error> Http3Client::input(const QuicNetworkPath &path, Uint8View chunk) {
    int status = input_impl(path, chunk);

    Error<Http3Error> error;
    switch (status) {
    case NGTCP2_NO_ERROR:
        break;
    case NGTCP2_ERR_DRAINING:
        if (!m_handled_rx_connection_close) {
            m_handled_rx_connection_close = true;
            handle_rx_connection_close();
        }
        break;
    case NGTCP2_ERR_CRYPTO: {
        uint8_t alert = ngtcp2_conn_get_tls_alert(m_quic_conn.get());
        if (m_last_error.error_code == 0) {
            ngtcp2_ccerr_set_tls_alert(&m_last_error, alert, nullptr, 0);
        }
        error = make_error(Http3Error{},
                AG_FMT("QUIC error: {} ({}), TLS alert: {} ({})", ngtcp2_strerror(status), status,
                        SSL_alert_desc_string_long(alert), SSL_alert_type_string_long(alert)));
        break;
    }
    default:
        if (m_last_error.error_code == 0) {
            ngtcp2_ccerr_set_liberr(&m_last_error, status, nullptr, 0);
        }
        error = make_error(Http3Error{}, AG_FMT("{} ({})", ngtcp2_strerror(status), status));
        break;
    }

    if (error != nullptr) {
        handle_error();
    }
    return error;
}

Result<uint64_t, Http3Error> Http3Client::submit_request(const Request &request, bool eof) {
    int64_t stream_id = 0;
    if (int status = ngtcp2_conn_open_bidi_stream(m_quic_conn.get(), &stream_id, nullptr); status != NGTCP2_NO_ERROR) {
        return make_error(Http3Error{}, AG_FMT("Couldn't open stream: {} ({})", ngtcp2_strerror(status), status));
    }

    bool head_request = request.method() == "HEAD";
    eof = eof || head_request;

    Stream &stream = m_streams.emplace(stream_id, Stream{}).first->second;
    stream.flags.set(Stream::HEAD_REQUEST, head_request);
    stream.flags.set(Stream::HAS_EOF, eof);

    std::vector<nghttp3_nv> nv_list;
    nv_list.reserve(std::distance(request.begin(), request.end()));
    std::transform(request.begin(), request.end(), std::back_inserter(nv_list), transform_header<std::string_view>);

    nghttp3_data_reader reader{};
    if (!eof) {
        stream.data_source.buffer.reset(evbuffer_new());
        reader = {
                .read_data = on_read_data,
        };
    }

    if (int status = nghttp3_conn_submit_request(
                m_http_conn.get(), stream_id, nv_list.data(), nv_list.size(), eof ? nullptr : &reader, nullptr);
            status != 0) {
        m_streams.erase(stream_id);
        return make_error(Http3Error{}, AG_FMT("Couldn't submit response: {} ({})", nghttp3_strerror(status), status));
    }

    return uint64_t(stream_id);
}

Error<Http3Error> Http3Client::submit_trailer(uint64_t stream_id, const ag::http::Headers &headers) {
    return submit_trailer_impl(stream_id, headers);
}

Error<Http3Error> Http3Client::submit_body(uint64_t stream_id, Uint8View chunk, bool eof) {
    return submit_body_impl(stream_id, chunk, eof);
}

Error<Http3Error> Http3Client::reset_stream(uint64_t stream_id, int error_code) {
    return reset_stream_impl(stream_id, error_code);
}

void Http3Client::set_session_close_error(int error_code, Uint8View reason) {
    if (error_code > SSL_R_SSLV3_ALERT_CLOSE_NOTIFY) {
        int alert_code = error_code - SSL_R_SSLV3_ALERT_CLOSE_NOTIFY;
        std::string_view alert_string = SSL_alert_desc_string_long(alert_code);
        ngtcp2_ccerr_set_tls_alert(
                &m_last_error, alert_code, (const uint8_t *) alert_string.data(), alert_string.size());
    } else if (error_code > NGHTTP3_H3_NO_ERROR) {
        ngtcp2_ccerr_set_application_error(&m_last_error, error_code, reason.data(), reason.length());
    }
}

Error<Http3Error> Http3Client::consume_stream(uint64_t stream_id, size_t length) {
    return consume_stream_impl(stream_id, length);
}

Error<Http3Error> Http3Client::consume_connection(size_t length) {
    return consume_connection_impl(length);
}

Error<Http3Error> Http3Client::handle_expiry() {
    return handle_expiry_impl();
}

Error<Http3Error> Http3Client::flush() {
    return flush_impl();
}

Nanos Http3Client::probe_timeout() const {
    return Nanos{ngtcp2_conn_get_pto(m_quic_conn.get())};
}

} // namespace ag::http
