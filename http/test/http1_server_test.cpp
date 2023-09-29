#include <map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/defs.h"
#include "common/http/http1.h"
#include "common/logger.h"
#include "common/utils.h"

struct Http1Server : public ::testing::Test {
public:
    Http1Server() {
        ag::Logger::set_log_level(ag::LOG_LEVEL_TRACE);
    }

protected:
    ag::http::Http1Server m_server{
            ag::http::Http1Server::Callbacks{
                    .arg = this,
                    .on_request = on_request,
                    .on_trailer_headers = on_trailer_headers,
                    .on_body = on_body,
                    .on_body_finished = on_body_finished,
                    .on_stream_finished = on_stream_finished,
                    .on_output = on_output,
            },
    };

    struct Stream {
        std::optional<ag::http::Request> request;
        std::optional<ag::http::Headers> trailer_headers;
        bool body_finished = false;
        bool stream_finished = false;
        std::string body;
        int error = 0;
    };

    std::map<uint64_t, Stream> m_streams;
    std::string m_output;

    static void on_request(void *arg, uint64_t stream_id, ag::http::Request request) {
        auto *self = (Http1Server *) arg;
        self->m_streams[stream_id].request.emplace(std::move(request));
    }

    static void on_trailer_headers(void *arg, uint64_t stream_id, ag::http::Headers headers) {
        auto *self = (Http1Server *) arg;
        self->m_streams[stream_id].trailer_headers.emplace(std::move(headers));
    }

    static void on_body(void *arg, uint64_t stream_id, ag::Uint8View chunk) {
        auto *self = (Http1Server *) arg;
        auto &body = self->m_streams[stream_id].body;
        body.insert(body.end(), chunk.begin(), chunk.end());
    }

    static void on_body_finished(void *arg, uint64_t stream_id) {
        auto *self = (Http1Server *) arg;
        self->m_streams[stream_id].body_finished = true;
    }

    static void on_stream_finished(void *arg, uint64_t stream_id, int error_code) {
        auto *self = (Http1Server *) arg;
        self->m_streams[stream_id].stream_finished = true;
        self->m_streams[stream_id].error = error_code;
    }

    static void on_output(void *arg, ag::Uint8View chunk) {
        auto *self = (Http1Server *) arg;
        self->m_output.insert(self->m_output.end(), chunk.begin(), chunk.end());
    }

    template <typename E>
    void check_no_error(const ag::Error<E> &error) {
        ASSERT_EQ(error, nullptr) << error->str();
    }

    template <typename T, typename E, typename U>
    void check_result(const ag::Result<T, E> &result, const U &x) {
        ASSERT_TRUE(result.has_value()) << result.error()->str();
        ASSERT_EQ(result.value(), x);
    }

    void check_output_equals(std::string_view str) {
        ASSERT_EQ(m_output, str);
        m_output.clear();
    }
};

TEST_F(Http1Server, CannotRespondWithoutRequest) {
    ag::http::Response response(ag::http::HTTP_1_1, 200); // NOLINT(*-magic-numbers)
    ASSERT_NE(m_server.send_response(42, response), nullptr);

    constexpr uint8_t BODY = 42;
    ASSERT_NE(m_server.send_body(0, {&BODY, 1}, true), nullptr);
}

TEST_F(Http1Server, Upgrade) {
    constexpr std::string_view REQUEST = "GET / HTTP/1.1\r\n"
                                         "Connection: upgrade\r\n"
                                         "Upgrade: websocket\r\n"
                                         "\r\n";
    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) REQUEST.data(), REQUEST.size()}), ag::http::Http1Server::InputUpgrade{}));

    ASSERT_EQ(m_streams.size(), 1);
    const auto &[stream_id, stream] = *m_streams.begin();

    ag::http::Response response(ag::http::HTTP_1_1, 200); // NOLINT(*-magic-numbers)
    response.headers().put("Upgrade", "websocket");
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
    ASSERT_FALSE(stream.stream_finished);
}

TEST_F(Http1Server, PipelineSeparatePackets) {
    constexpr std::string_view BODIES[] = {"", "aaa", "bbb", "ccc"};
    for (auto body : BODIES) {
        std::string request = AG_FMT("GET / HTTP/1.1\r\n"
                                     "Content-Length: {}\r\n\r\n"
                                     "{}\r\n",
                body.length(), body);
        ASSERT_NO_FATAL_FAILURE(check_result(
                m_server.input({(uint8_t *) request.data(), request.size()}), ag::http::Http1Server::InputOk{}));
    }

    ASSERT_EQ(m_streams.size(), std::size(BODIES));
    ag::http::Response response(ag::http::HTTP_1_1, 200); // NOLINT(*-magic-numbers)
    for (size_t i = 0; i < std::size(BODIES); ++i) {
        std::string_view body = BODIES[i]; // NOLINT(*-pro-bounds-constant-array-index)
        const auto &[stream_id, stream] = *std::next(m_streams.begin(), ssize_t(i));
        std::string_view stream_body = {(char *) stream.body.data(), stream.body.size()};
        ASSERT_EQ(body, stream_body);
        ASSERT_EQ(!body.empty(), stream.body_finished);
        ASSERT_EQ(stream.error, 0);
        ASSERT_TRUE(stream.request.has_value());
        ASSERT_FALSE(stream.stream_finished);

        ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
        ASSERT_TRUE(stream.stream_finished);
    }

    // Cannot send in the closed stream
    for (const auto &[stream_id, _] : m_streams) {
        ASSERT_NE(m_server.send_response(stream_id, response), nullptr);
    }
}

TEST_F(Http1Server, PipelineSinglePacket) {
    constexpr std::string_view BODIES[] = {"", "aaa", "bbb", "ccc"};
    std::string requests;
    for (auto body : BODIES) {
        requests.append(AG_FMT("GET / HTTP/1.1\r\n"
                               "Content-Length: {}\r\n\r\n"
                               "{}\r\n",
                body.length(), body));
    }
    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) requests.data(), requests.size()}), ag::http::Http1Server::InputOk{}));

    ASSERT_EQ(m_streams.size(), std::size(BODIES));
    ag::http::Response response(ag::http::HTTP_1_1, 200); // NOLINT(*-magic-numbers)
    for (size_t i = 0; i < std::size(BODIES); ++i) {
        std::string_view body = BODIES[i]; // NOLINT(*-pro-bounds-constant-array-index)
        const auto &[stream_id, stream] = *std::next(m_streams.begin(), ssize_t(i));
        std::string_view stream_body = {(char *) stream.body.data(), stream.body.size()};
        ASSERT_EQ(body, stream_body);
        ASSERT_EQ(!body.empty(), stream.body_finished);
        ASSERT_EQ(stream.error, 0);
        ASSERT_TRUE(stream.request.has_value());
        ASSERT_FALSE(stream.stream_finished);

        ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
        ASSERT_TRUE(stream.stream_finished);
    }

    // Cannot send in the closed stream
    for (const auto &[stream_id, _] : m_streams) {
        ASSERT_NE(m_server.send_response(stream_id, response), nullptr);
    }
}

TEST_F(Http1Server, IncomingTrailer) {
    constexpr std::string_view REQUEST = "GET / HTTP/1.1\r\n"
                                         "Content-Type: text/plain\r\n"
                                         "Transfer-Encoding: chunked\r\n"
                                         "\r\n"
                                         "25\r\n"
                                         "This is the data in the first chunk\r\n"
                                         "\r\n"
                                         "1C\r\n"
                                         "and this is the second one\r\n"
                                         "\r\n"
                                         "0\r\n"
                                         "Trailer: trailer\r\n"
                                         "\r\n";

    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) REQUEST.data(), REQUEST.size()}), ag::http::Http1Server::InputOk{}));

    ASSERT_EQ(m_streams.size(), 1);
    const auto &[stream_id, stream] = *m_streams.begin();

    ASSERT_TRUE(stream.request.has_value());
    ASSERT_EQ(stream.request->str(), // NOLINT(*-unchecked-optional-access)
            "GET / HTTP/1.1\r\n"
            "Content-Type: text/plain\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n");
    ASSERT_EQ(stream.body, "This is the data in the first chunk\r\nand this is the second one\r\n");
    ASSERT_TRUE(stream.trailer_headers.has_value());
    ASSERT_EQ(stream.trailer_headers->str(), "Trailer: trailer\r\n"); // NOLINT(*-unchecked-optional-access)
}

TEST_F(Http1Server, OutgoingTrailer) {
    constexpr std::string_view REQUEST = "GET / HTTP/1.1\r\n\r\n";
    constexpr std::string_view RESPONSE_BODY = "Hello, world!";

    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) REQUEST.data(), REQUEST.size()}), ag::http::Http1Server::InputOk{}));

    ASSERT_EQ(m_streams.size(), 1);
    const auto &[stream_id, stream] = *m_streams.begin();

    ag::http::Response response(ag::http::HTTP_1_1, 200); // NOLINT(*-magic-numbers)
    response.headers().put("Transfer-Encoding", "Chunked");
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
    ASSERT_NO_FATAL_FAILURE(check_output_equals(response.str()));
    ASSERT_NO_FATAL_FAILURE(check_no_error(
            m_server.send_body(stream_id, {(uint8_t *) RESPONSE_BODY.data(), RESPONSE_BODY.size()}, false)));
    ASSERT_NO_FATAL_FAILURE(check_output_equals(AG_FMT("{:X}\r\n{}\r\n", RESPONSE_BODY.size(), RESPONSE_BODY)));
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_body(stream_id, {}, false)));
    ASSERT_NO_FATAL_FAILURE(check_output_equals("0\r\n"));

    ag::http::Headers headers;
    headers.put("foo", "bar");
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_trailer(stream_id, headers, false)));
    headers.remove("foo");
    headers.put("bar", "foo");
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_trailer(stream_id, headers, true)));
    ASSERT_NO_FATAL_FAILURE(check_output_equals("foo: bar\r\nbar: foo\r\n\r\n"));
}

TEST_F(Http1Server, IntermediateResponse) {
    constexpr std::string_view REQUEST = "GET / HTTP/1.1\r\n\r\n";

    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) REQUEST.data(), REQUEST.size()}), ag::http::Http1Server::InputOk{}));

    ASSERT_EQ(m_streams.size(), 1);
    const auto &[stream_id, stream] = *m_streams.begin();

    ag::http::Response response(ag::http::HTTP_1_1, 100); // NOLINT(*-magic-numbers)
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
    ASSERT_NO_FATAL_FAILURE(check_output_equals(response.str()));
    ASSERT_FALSE(stream.stream_finished);

    response.status_code(200); // NOLINT(*-magic-numbers)
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
    ASSERT_NO_FATAL_FAILURE(check_output_equals(response.str()));
    ASSERT_EQ(0, stream.body.size());
    ASSERT_TRUE(stream.stream_finished);
}

struct DecodingSample {
    std::string_view data;
    size_t content_length;
    bool body_finished;
};

struct Http1ServerDecoding : public Http1Server, public ::testing::WithParamInterface<DecodingSample> {};

TEST_P(Http1ServerDecoding, SingleChunk) {
    const DecodingSample &sample = GetParam();
    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) sample.data.data(), sample.data.size()}), ag::http::Http1Server::InputOk{}));
    if (sample.data.empty()) {
        ASSERT_EQ(m_streams.size(), 0);
        return;
    }
    ASSERT_EQ(m_streams.size(), 1);
    const Stream &stream = m_streams.begin()->second;
    ASSERT_EQ(sample.content_length, stream.body.size());
    ASSERT_EQ(sample.body_finished, stream.body_finished);
    ASSERT_EQ(stream.error, 0);
    ASSERT_TRUE(stream.request.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_TRUE(ag::utils::starts_with(sample.data, stream.request->str())) << stream.request->str();
}

TEST_P(Http1ServerDecoding, MultipleChunks) {
    const DecodingSample &sample = GetParam();
    for (char b : sample.data) {
        ASSERT_NO_FATAL_FAILURE(check_result(m_server.input({(uint8_t *) &b, 1}), ag::http::Http1Server::InputOk{}));
    }
    if (sample.data.empty()) {
        ASSERT_EQ(m_streams.size(), 0);
        return;
    }
    ASSERT_EQ(m_streams.size(), 1);
    const Stream &stream = m_streams.begin()->second;
    ASSERT_EQ(sample.content_length, stream.body.size());
    ASSERT_EQ(sample.body_finished, stream.body_finished);
    ASSERT_EQ(stream.error, 0);
    ASSERT_TRUE(stream.request.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_TRUE(ag::utils::starts_with(sample.data, stream.request->str())) << stream.request->str();
}

static constexpr DecodingSample DECODING_SAMPLES[] = {
        // Zero bytes is a valid input for HTTP/1 session
        {},
        {
                .data = "GET /test HTTP/1.1\r\n"
                        "User-Agent: curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g "
                        "zlib/1.2.3.3libidn/1.1\r\n"
                        "Host: 0.0.0.0=5000\r\n"
                        "Accept: */*\r\n"
                        "\r\n",
                .content_length = 0,
                .body_finished = false,
        },
        {
                .data = "POST / HTTP/1.1\r\n"
                        "Host: www.example.com\r\n"
                        "Content-Type: application/x-www-form-urlencoded\r\n"
                        "Content-Length: 4\r\n"
                        "\r\n"
                        "q=42\r\n",
                .content_length = 4,
                .body_finished = true,
        },
};

INSTANTIATE_TEST_SUITE_P(Test, Http1ServerDecoding, ::testing::ValuesIn(DECODING_SAMPLES));

struct EncodingSample {
    int status_code;
    std::string_view status_string;
    std::vector<std::pair<std::string_view, std::string_view>> headers;
    std::string_view body;
    std::string_view expected;
};

struct Http1ServerEncoding : public Http1Server, public ::testing::WithParamInterface<EncodingSample> {};

TEST_P(Http1ServerEncoding, Test) {
    const EncodingSample &sample = GetParam();

    constexpr std::string_view REQUEST = "GET / HTTP/1.1\r\n\r\n";
    ASSERT_NO_FATAL_FAILURE(check_result(
            m_server.input({(uint8_t *) REQUEST.data(), REQUEST.size()}), ag::http::Http1Server::InputOk{}));

    ASSERT_EQ(m_streams.size(), 1);
    uint32_t stream_id = m_streams.begin()->first;

    ag::http::Response response(ag::http::HTTP_1_1, sample.status_code);
    response.status_string(std::string{sample.status_string});
    for (auto [name, value] : sample.headers) {
        response.headers().put(std::string(name), std::string(value));
    }

    ASSERT_NO_FATAL_FAILURE(check_no_error(m_server.send_response(stream_id, response)));
    ASSERT_NO_FATAL_FAILURE(
            check_no_error(m_server.send_body(stream_id, {(uint8_t *) sample.body.data(), sample.body.size()}, true)));

    ASSERT_EQ(sample.expected, m_output);
}

static const EncodingSample ENCODING_SAMPLES[] = {
        {
                .status_code = 200,
                .status_string = "OK",
                .headers = {{"Content-Length", "5"}},
                .body = "hello",
                .expected = "HTTP/1.1 200 OK\r\n"
                            "Content-Length: 5\r\n\r\n"
                            "hello",
        },
        {
                .status_code = 200,
                .status_string = "OK",
                .headers = {{"Transfer-Encoding", "chunked"}, {"Content-Length", "5"}},
                .body = "hello",
                .expected = "HTTP/1.1 200 OK\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "Content-Length: 5\r\n\r\n"
                            "5\r\n"
                            "hello\r\n"
                            "0\r\n\r\n",
        },
};

INSTANTIATE_TEST_SUITE_P(Test, Http1ServerEncoding, ::testing::ValuesIn(ENCODING_SAMPLES));
