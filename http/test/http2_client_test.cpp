#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/http/http2.h"
#include "common/logger.h"
#include "common/utils.h"

constexpr std::string_view PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

constexpr uint8_t OUTGOING_CLIENT_SETTINGS_FRAME[] = {0x00, 0x00, 0x1e, // length
        0x04, 0x00,                                                     // frame type = settings, no flags
        0x00, 0x00, 0x00, 0x00,                                         // stream id
        // values
        0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0xe8,
        0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x40, 0x00};

constexpr uint8_t INCOMING_SERVER_SETTINGS_FRAME[] = {0x00, 0x00, 0x1e, // length
        0x04, 0x00,                                                     // frame type = settings, no flags
        0x00, 0x00, 0x00, 0x00,                                         // stream id
        // values
        0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0xe8,
        0x00, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x40, 0x00};

constexpr uint8_t SESSION_WINDOW_UPDATE_FRAME[] = {
        0x00, 0x00, 0x04,       // length
        0x08, 0x00,             // frame type = window update, no flags
        0x00, 0x00, 0x00, 0x00, // stream id
        0x00, 0x7f, 0x00, 0x01  // some value
};

constexpr uint8_t SETTINGS_ACK_FRAME[] = {
        0x00, 0x00, 0x00,      // length
        0x04, 0x01,            // frame type = settings, flags = ack
        0x00, 0x00, 0x00, 0x00 // stream id
};

// GET http://httpbingo.org/bytes/42
constexpr uint8_t REQUEST_FRAME[] = {0x00, 0x00, 0x29, // length
        0x01, 0x04,                                    // frame type = headers, flags = end headers
        0x00, 0x00, 0x00, 0x01,                        // stream id
        0x3f, 0xe1, 0x1f,                              // table size
        // header block
        0x82, 0x86, 0x04, 0x87, 0x62, 0x3f, 0x49, 0x2a, 0x18, 0x68, 0x5f, 0x41, 0x8a, 0x9d, 0x29, 0xae, 0x33, 0x55,
        0x31, 0xd7, 0x3d, 0x93, 0x7f, 0x7a, 0x88, 0x25, 0xb6, 0x50, 0xc3, 0xab, 0xbc, 0xf2, 0xe1, 0x53, 0x03, 0x2a,
        0x2f, 0x2a};

constexpr uint8_t RESPONSE_FRAME[] = {0x00, 0x00, 0x95, // length
        0x01, 0x04,                                     // frame type = headers, flags = end headers
        0x00, 0x00, 0x00, 0x01,                         // stream id
        0x3f, 0xe1, 0x1f,                               // table size
        // header block
        0x88, 0x40, 0x96, 0x19, 0x08, 0x54, 0x21, 0x62, 0x1e, 0xa4, 0xd8, 0x7a, 0x16, 0x1d, 0x14, 0x1f, 0xc2, 0xc4,
        0xb0, 0xb2, 0x16, 0xa4, 0x98, 0x74, 0x23, 0x83, 0x4d, 0x96, 0x97, 0x54, 0x01, 0x2a, 0x0f, 0x0d, 0x02, 0x34,
        0x32, 0x5f, 0x90, 0x1d, 0x75, 0xd0, 0x62, 0x0d, 0x26, 0x3d, 0x4c, 0x1c, 0x89, 0x2a, 0x56, 0x42, 0x6c, 0x28,
        0xe9, 0x61, 0x96, 0xc3, 0x61, 0xbe, 0x94, 0x0b, 0xca, 0x43, 0x6c, 0xca, 0x08, 0x02, 0x65, 0x40, 0x3b, 0x70,
        0x01, 0xb8, 0xcb, 0xca, 0x62, 0xd1, 0xbf, 0x76, 0x93, 0xc3, 0x47, 0xa6, 0x01, 0x19, 0x1d, 0x00, 0x00, 0x05,
        0x3f, 0xa1, 0x00, 0x4c, 0xac, 0x07, 0x96, 0x0b, 0x9f, 0xdf, 0x7c, 0x86, 0x12, 0x92, 0xd1, 0xe9, 0x73, 0x1f,
        0x40, 0x8b, 0x96, 0x8f, 0x4b, 0x58, 0x5e, 0xd6, 0x95, 0x09, 0x58, 0xd2, 0x7f, 0x98, 0x00, 0x71, 0xbc, 0xce,
        0xc7, 0x1c, 0x77, 0x03, 0xc0, 0xcd, 0x8d, 0xfd, 0x60, 0xbc, 0xd6, 0xc7, 0xef, 0x5d, 0x73, 0x05, 0xe5, 0xa5,
        0xb0, 0x7f};

constexpr std::string_view EXPECTED_RESPONSE_STR = "HTTP/2.0 200\r\n"
                                                   "access-control-allow-credentials: true\r\n"
                                                   "access-control-allow-origin: *\r\n"
                                                   "content-length: 42\r\n"
                                                   "content-type: application/octet-stream\r\n"
                                                   "date: Fri, 18 Aug 2023 07:01:38 GMT\r\n"
                                                   "server: Fly/0bc70000 (2023-08-16)\r\n"
                                                   "via: 2 fly.io\r\n"
                                                   "fly-request-id: 01H83Q667E80KH9P0C4Q9CB6EC-fra\r\n\r\n";

constexpr uint8_t DATA_FRAME[] = {0x00, 0x00, 0x2a, // length
        0x00, 0x01,                                 // frame type = data, flags = end stream
        0x00, 0x00, 0x00, 0x01,                     // stream id
        0x35, 0x30, 0xf7, 0x34, 0xa9, 0x2c, 0xf4, 0x4a, 0x8a, 0xa4, 0xfa, 0x6c, 0xa2, 0x31, 0xb7, 0x2f, 0xfb, 0x09,
        0xe4, 0x81, 0x83, 0x78, 0xda, 0xec, 0x60, 0xf6, 0x36, 0xec, 0x6c, 0xcb, 0x7d, 0xc8, 0x0c, 0x9a, 0xb5, 0x42,
        0xda, 0x3a, 0xe1, 0x6d, 0x1a, 0x35};

constexpr uint8_t EXPECTED_RESPONSE_DATA[] = {0x35, 0x30, 0xf7, 0x34, 0xa9, 0x2c, 0xf4, 0x4a, 0x8a, 0xa4, 0xfa, 0x6c,
        0xa2, 0x31, 0xb7, 0x2f, 0xfb, 0x09, 0xe4, 0x81, 0x83, 0x78, 0xda, 0xec, 0x60, 0xf6, 0x36, 0xec, 0x6c, 0xcb,
        0x7d, 0xc8, 0x0c, 0x9a, 0xb5, 0x42, 0xda, 0x3a, 0xe1, 0x6d, 0x1a, 0x35};

constexpr uint8_t TRAILER_FRAME[] = {
        0x00, 0x00, 0x08,                               // length
        0x01, 0x05,                                     // frame type = headers, flags = end headers + end stream
        0x00, 0x00, 0x00, 0x01,                         // stream id
        0x40, 0x82, 0x94, 0xe7, 0x03, 0x62, 0x61, 0x72, // header block
};

struct Http2Client : public ::testing::Test {
public:
    Http2Client() {
        ag::Logger::set_log_level(ag::LOG_LEVEL_TRACE);
    }

protected:
    std::unique_ptr<ag::http::Http2Client> m_client;

    struct Stream {
        std::optional<ag::http::Response> response;
        std::optional<ag::http::Headers> trailer;
        bool read_finished = false;
        bool closed = false;
        std::vector<uint8_t> body;
        nghttp2_error_code error = NGHTTP2_NO_ERROR;
    };

    std::map<uint64_t, Stream> m_streams;
    std::vector<uint8_t> m_output;

    void SetUp() override {
        ag::Result make_result = ag::http::Http2Client::make(ag::http::Http2Settings{},
                ag::http::Http2Client::Callbacks{
                        .arg = this,
                        .on_response = on_response,
                        .on_trailer_headers = on_trailer_headers,
                        .on_body = on_body,
                        .on_stream_read_finished = on_stream_read_finished,
                        .on_stream_closed = on_stream_closed,
                        .on_output = on_output,
                });
        ASSERT_FALSE(make_result.has_error()) << make_result.error()->str();
        m_client = std::move(make_result.value());

        // handshake
        static const std::vector<uint8_t> HANDSHAKE_STARTING_MESSAGE = []() {
            std::vector<uint8_t> ret;
            ret.insert(ret.end(), std::begin(PREFACE), std::end(PREFACE));
            ret.insert(ret.end(), std::begin(OUTGOING_CLIENT_SETTINGS_FRAME), std::end(OUTGOING_CLIENT_SETTINGS_FRAME));
            ret.insert(ret.end(), std::begin(SESSION_WINDOW_UPDATE_FRAME), std::end(SESSION_WINDOW_UPDATE_FRAME));
            return ret;
        }();

        ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
        ASSERT_NO_FATAL_FAILURE(
                check_output_equals({HANDSHAKE_STARTING_MESSAGE.data(), HANDSHAKE_STARTING_MESSAGE.size()}));

        ASSERT_NO_FATAL_FAILURE(check_result(
                m_client->input({INCOMING_SERVER_SETTINGS_FRAME, std::size(INCOMING_SERVER_SETTINGS_FRAME)}),
                std::size(INCOMING_SERVER_SETTINGS_FRAME)));
        ASSERT_NO_FATAL_FAILURE(
                check_result(m_client->input({SESSION_WINDOW_UPDATE_FRAME, std::size(SESSION_WINDOW_UPDATE_FRAME)}),
                        std::size(SESSION_WINDOW_UPDATE_FRAME)));
        ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
        ASSERT_NO_FATAL_FAILURE(check_output_equals({SETTINGS_ACK_FRAME, std::size(SETTINGS_ACK_FRAME)}));
        ASSERT_NO_FATAL_FAILURE(check_result(
                m_client->input({SETTINGS_ACK_FRAME, std::size(SETTINGS_ACK_FRAME)}), std::size(SETTINGS_ACK_FRAME)));
    }

    void TearDown() override {
        m_client.reset();
    }

    static void on_response(void *arg, uint32_t stream_id, ag::http::Response response) {
        auto *self = (Http2Client *) arg;
        self->m_streams[stream_id].response.emplace(std::move(response));
    }

    static void on_trailer_headers(void *arg, uint32_t stream_id, ag::http::Headers trailer) {
        auto *self = (Http2Client *) arg;
        self->m_streams[stream_id].trailer.emplace(std::move(trailer));
    }

    static void on_body(void *arg, uint32_t stream_id, ag::Uint8View chunk) {
        auto *self = (Http2Client *) arg;
        auto &body = self->m_streams[stream_id].body;
        body.insert(body.end(), chunk.begin(), chunk.end());
    }

    static void on_stream_read_finished(void *arg, uint32_t stream_id) {
        auto *self = (Http2Client *) arg;
        self->m_streams[stream_id].read_finished = true;
    }

    static void on_stream_closed(void *arg, uint32_t stream_id, nghttp2_error_code error_code) {
        auto *self = (Http2Client *) arg;
        self->m_streams[stream_id].closed = true;
        self->m_streams[stream_id].error = error_code;
    }

    static void on_output(void *arg, ag::Uint8View chunk) {
        auto *self = (Http2Client *) arg;
        self->m_output.insert(self->m_output.end(), chunk.begin(), chunk.end());
    }

    template <typename E>
    void check_no_error(const ag::Error<E> &error) {
        ASSERT_EQ(error, nullptr) << error->str();
    }

    template <typename T, typename E>
    void check_result(const ag::Result<T, E> &result, const T &x) {
        ASSERT_TRUE(result.has_value()) << result.error()->str();
        ASSERT_EQ(result.value(), x);
    }

    void check_output_equals(ag::Uint8View data) {
        ASSERT_EQ(ag::utils::encode_to_hex({m_output.data(), m_output.size()}), ag::utils::encode_to_hex(data));
        m_output.clear();
    }
};

TEST_F(Http2Client, Exchange) {
    ag::http::Request req(ag::http::HTTP_2_0, "GET", "/bytes/42");
    req.scheme("http");
    req.authority("httpbingo.org");
    req.headers().put("user-agent", "curl/7.88.1");
    req.headers().put("accept", "*/*");

    ag::Result result = m_client->submit_request(req, false);
    ASSERT_TRUE(result.has_value()) << result.error()->str();
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
    ASSERT_NO_FATAL_FAILURE(check_output_equals({REQUEST_FRAME, std::size(REQUEST_FRAME)}));

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client->input({RESPONSE_FRAME, std::size(RESPONSE_FRAME)}), std::size(RESPONSE_FRAME)));
    uint32_t stream_id = result.value();
    ASSERT_TRUE(m_streams.contains(stream_id));
    ASSERT_TRUE(m_streams[stream_id].response.has_value());
    ASSERT_EQ(m_streams[stream_id].response->str(), EXPECTED_RESPONSE_STR); // NOLINT(*-unchecked-optional-access)

    ASSERT_NO_FATAL_FAILURE(check_result(m_client->input({DATA_FRAME, std::size(DATA_FRAME)}), std::size(DATA_FRAME)));
    ASSERT_EQ(ag::utils::encode_to_hex({m_streams[stream_id].body.data(), m_streams[stream_id].body.size()}),
            ag::utils::encode_to_hex({EXPECTED_RESPONSE_DATA, std::size(EXPECTED_RESPONSE_DATA)}));

    ASSERT_TRUE(m_streams[stream_id].read_finished);
}

TEST_F(Http2Client, IntermediateResponse) {
    constexpr uint8_t INTERMEDIATE_RESPONSE_FRAME[] = {
            0x00, 0x00, 0x07,       // length
            0x01, 0x04,             // frame type = headers, flags = end headers
            0x00, 0x00, 0x00, 0x01, // stream id
            0x3f, 0xe1, 0x1f,       // table size
            0x48, 0x82, 0x08, 0x01  // header block
    };

    constexpr std::string_view EXPECTED_INTERMEDIATE_RESPONSE_STR = "HTTP/2.0 100\r\n\r\n";

    ag::http::Request req(ag::http::HTTP_2_0, "GET", "/bytes/42");
    req.scheme("http");
    req.authority("httpbingo.org");
    req.headers().put("user-agent", "curl/7.88.1");
    req.headers().put("accept", "*/*");
    ag::Result result = m_client->submit_request(req, false);
    ASSERT_TRUE(result.has_value()) << result.error()->str();
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
    ASSERT_NO_FATAL_FAILURE(check_output_equals({REQUEST_FRAME, std::size(REQUEST_FRAME)}));

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client->input({INTERMEDIATE_RESPONSE_FRAME, std::size(INTERMEDIATE_RESPONSE_FRAME)}),
                    std::size(INTERMEDIATE_RESPONSE_FRAME)));
    uint32_t stream_id = result.value();
    ASSERT_TRUE(m_streams.contains(stream_id));
    ASSERT_TRUE(m_streams[stream_id].response.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_EQ(m_streams[stream_id].response->str(), EXPECTED_INTERMEDIATE_RESPONSE_STR);
    ASSERT_FALSE(m_streams[stream_id].read_finished);

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client->input({RESPONSE_FRAME, std::size(RESPONSE_FRAME)}), std::size(RESPONSE_FRAME)));
    ASSERT_TRUE(m_streams.contains(stream_id));
    ASSERT_TRUE(m_streams[stream_id].response.has_value());
    ASSERT_EQ(m_streams[stream_id].response->str(), EXPECTED_RESPONSE_STR); // NOLINT(*-unchecked-optional-access)
    ASSERT_FALSE(m_streams[stream_id].read_finished);

    ASSERT_NO_FATAL_FAILURE(check_result(m_client->input({DATA_FRAME, std::size(DATA_FRAME)}), std::size(DATA_FRAME)));
    ASSERT_EQ(ag::utils::encode_to_hex({m_streams[stream_id].body.data(), m_streams[stream_id].body.size()}),
            ag::utils::encode_to_hex({EXPECTED_RESPONSE_DATA, std::size(EXPECTED_RESPONSE_DATA)}));

    ASSERT_TRUE(m_streams[stream_id].read_finished);
}

TEST_F(Http2Client, IncomingTrailer) {
    constexpr uint8_t DATA_FRAME_NO_FLAGS[] = {0x00, 0x00, 0x2a, // length
            0x00, 0x00,                                          // frame type = data, no flags
            0x00, 0x00, 0x00, 0x01,                              // stream id
            0x35, 0x30, 0xf7, 0x34, 0xa9, 0x2c, 0xf4, 0x4a, 0x8a, 0xa4, 0xfa, 0x6c, 0xa2, 0x31, 0xb7, 0x2f, 0xfb, 0x09,
            0xe4, 0x81, 0x83, 0x78, 0xda, 0xec, 0x60, 0xf6, 0x36, 0xec, 0x6c, 0xcb, 0x7d, 0xc8, 0x0c, 0x9a, 0xb5, 0x42,
            0xda, 0x3a, 0xe1, 0x6d, 0x1a, 0x35};

    ag::http::Request req(ag::http::HTTP_2_0, "GET", "/bytes/42");
    req.scheme("http");
    req.authority("httpbingo.org");
    req.headers().put("user-agent", "curl/7.88.1");
    req.headers().put("accept", "*/*");
    ag::Result result = m_client->submit_request(req, false);
    ASSERT_TRUE(result.has_value()) << result.error()->str();
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
    ASSERT_NO_FATAL_FAILURE(check_output_equals({REQUEST_FRAME, std::size(REQUEST_FRAME)}));

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client->input({RESPONSE_FRAME, std::size(RESPONSE_FRAME)}), std::size(RESPONSE_FRAME)));
    uint32_t stream_id = result.value();
    ASSERT_TRUE(m_streams.contains(stream_id));
    ASSERT_TRUE(m_streams[stream_id].response.has_value());
    ASSERT_EQ(m_streams[stream_id].response->str(), EXPECTED_RESPONSE_STR); // NOLINT(*-unchecked-optional-access)

    ASSERT_NO_FATAL_FAILURE(check_result(
            m_client->input({DATA_FRAME_NO_FLAGS, std::size(DATA_FRAME_NO_FLAGS)}), std::size(DATA_FRAME_NO_FLAGS)));
    ASSERT_EQ(ag::utils::encode_to_hex({m_streams[stream_id].body.data(), m_streams[stream_id].body.size()}),
            ag::utils::encode_to_hex({EXPECTED_RESPONSE_DATA, std::size(EXPECTED_RESPONSE_DATA)}));
    ASSERT_FALSE(m_streams[stream_id].read_finished);

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client->input({TRAILER_FRAME, std::size(TRAILER_FRAME)}), std::size(TRAILER_FRAME)));
    ASSERT_TRUE(m_streams[stream_id].trailer.has_value());
    ASSERT_EQ(m_streams[stream_id].trailer->str(), "foo: bar\r\n"); // NOLINT(*-unchecked-optional-access)
    ASSERT_TRUE(m_streams[stream_id].read_finished);
}

TEST_F(Http2Client, OutgoingTrailer) {
    ag::http::Request req(ag::http::HTTP_2_0, "GET", "/bytes/42");
    req.scheme("http");
    req.authority("httpbingo.org");
    req.headers().put("user-agent", "curl/7.88.1");
    req.headers().put("accept", "*/*");

    ag::Result result = m_client->submit_request(req, false);
    ASSERT_TRUE(result.has_value()) << result.error()->str();
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
    ASSERT_NO_FATAL_FAILURE(check_output_equals({REQUEST_FRAME, std::size(REQUEST_FRAME)}));

    uint32_t stream_id = result.value();
    ag::http::Headers trailer;
    trailer.put("foo", "bar");
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->submit_trailer(stream_id, trailer)));
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client->flush()));
    ASSERT_NO_FATAL_FAILURE(check_output_equals({TRAILER_FRAME, std::size(TRAILER_FRAME)}));

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client->input({RESPONSE_FRAME, std::size(RESPONSE_FRAME)}), std::size(RESPONSE_FRAME)));
    ASSERT_TRUE(m_streams.contains(stream_id));
    ASSERT_TRUE(m_streams[stream_id].response.has_value());
    ASSERT_EQ(m_streams[stream_id].response->str(), EXPECTED_RESPONSE_STR); // NOLINT(*-unchecked-optional-access)

    ASSERT_NO_FATAL_FAILURE(check_result(m_client->input({DATA_FRAME, std::size(DATA_FRAME)}), std::size(DATA_FRAME)));
    ASSERT_EQ(ag::utils::encode_to_hex({m_streams[stream_id].body.data(), m_streams[stream_id].body.size()}),
            ag::utils::encode_to_hex({EXPECTED_RESPONSE_DATA, std::size(EXPECTED_RESPONSE_DATA)}));

    ASSERT_TRUE(m_streams[stream_id].read_finished);
}
