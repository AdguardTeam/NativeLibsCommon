#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "common/defs.h"
#include "common/http/http1.h"
#include "common/logger.h"
#include "common/utils.h"

struct Http1Client : ::testing::Test {
public:
    Http1Client() {
        ag::Logger::set_log_level(ag::LOG_LEVEL_TRACE);
    }

protected:
    ag::http::Http1Client m_client{
            ag::http::Http1Client::Callbacks{
                    .arg = this,
                    .on_response = on_response,
                    .on_trailer_headers = on_trailer_headers,
                    .on_body = on_body,
                    .on_body_finished = on_body_finished,
                    .on_stream_finished = on_stream_finished,
                    .on_output = on_output,
            },
    };

    struct Stream {
        std::optional<ag::http::Response> response;
        std::optional<ag::http::Headers> trailer;
        bool body_finished = false;
        bool stream_finished = false;
        std::string body;
        int error = 0;
    };

    std::map<uint64_t, Stream> m_streams;
    std::string m_output;

    static void on_response(void *arg, uint64_t stream_id, ag::http::Response response) {
        auto *self = (Http1Client *) arg;
        self->m_streams[stream_id].response.emplace(std::move(response));
    }

    static void on_trailer_headers(void *arg, uint64_t stream_id, ag::http::Headers headers) {
        auto *self = (Http1Client *) arg;
        self->m_streams[stream_id].trailer.emplace(std::move(headers));
    }

    static void on_body(void *arg, uint64_t stream_id, ag::Uint8View chunk) {
        auto *self = (Http1Client *) arg;
        auto &body = self->m_streams[stream_id].body;
        body.insert(body.end(), chunk.begin(), chunk.end());
    }

    static void on_body_finished(void *arg, uint64_t stream_id) {
        auto *self = (Http1Client *) arg;
        self->m_streams[stream_id].body_finished = true;
    }

    static void on_stream_finished(void *arg, uint64_t stream_id, int error_code) {
        auto *self = (Http1Client *) arg;
        self->m_streams[stream_id].stream_finished = true;
        self->m_streams[stream_id].error = error_code;
    }

    static void on_output(void *arg, ag::Uint8View chunk) {
        auto *self = (Http1Client *) arg;
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

TEST_F(Http1Client, CannotSendBodyWithoutHeaders) {
    constexpr uint8_t BODY = 42;
    ASSERT_NE(m_client.send_body(0, {&BODY, 1}, true), nullptr);
}

TEST_F(Http1Client, IncomingTrailer) {
    static constexpr std::string_view DATA = "HTTP/1.1 200 OK\r\n"
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

    ag::http::Request request(ag::http::HTTP_1_1, "GET", "/");
    ag::Result result = m_client.send_request(request);
    ASSERT_TRUE(result.has_value()) << result.error()->str();

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client.input({(uint8_t *) DATA.data(), DATA.size()}), ag::http::Http1Client::InputOk{}));
    ASSERT_EQ(m_streams.size(), 1);
    const Stream &stream = m_streams.begin()->second;
    ASSERT_EQ(65, stream.body.size());
    ASSERT_TRUE(stream.body_finished);
    ASSERT_EQ(stream.error, 0);
    ASSERT_TRUE(stream.response.has_value());
    ASSERT_TRUE(ag::utils::starts_with(DATA, stream.response->str())); // NOLINT(*-unchecked-optional-access)

    ASSERT_TRUE(stream.trailer.has_value());
    ASSERT_EQ(stream.trailer->get("Trailer"), "trailer");                       // NOLINT(*-unchecked-optional-access)
    ASSERT_TRUE(ag::utils::ends_with(DATA, AG_FMT("{}\r\n", *stream.trailer))); // NOLINT(*-unchecked-optional-access)
}

TEST_F(Http1Client, OutgoingTrailer) {
    ag::http::Request request(ag::http::HTTP_1_1, "GET", "/");
    request.headers().put("Transfer-Encoding", "chunked");
    ag::Result result = m_client.send_request(request);
    ASSERT_TRUE(result.has_value()) << result.error()->str();
    ASSERT_NO_FATAL_FAILURE(check_output_equals(request.str()));

    uint32_t stream_id = result.value();
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client.send_body(stream_id, {}, false)));

    ag::http::Headers trailer;
    trailer.put("foo", "bar");
    ASSERT_NO_FATAL_FAILURE(check_no_error(m_client.send_trailer(stream_id, trailer, true)));
    ASSERT_EQ(m_output, AG_FMT("0\r\n{}\r\n", trailer));
}

TEST_F(Http1Client, PipelineSeparatePackets) {
    constexpr std::string_view BODIES[] = {"", "aaa", "bbb", "ccc"};

    ag::http::Request request(ag::http::HTTP_1_1, "GET", "/");
    for (size_t i = 0; i < std::size(BODIES); ++i) {
        ag::Result result = m_client.send_request(request);
        ASSERT_TRUE(result.has_value()) << result.error()->str();
    }

    for (std::string_view body : BODIES) {
        std::string response = AG_FMT("HTTP/1.1 200 OK\r\n"
                                      "Content-Length: {}\r\n\r\n"
                                      "{}\r\n",
                body.length(), body);
        ASSERT_NO_FATAL_FAILURE(check_result(
                m_client.input({(uint8_t *) response.data(), response.size()}), ag::http::Http1Client::InputOk{}));
    }

    ASSERT_EQ(m_streams.size(), std::size(BODIES));
    for (size_t i = 0; i < std::size(BODIES); ++i) {
        const auto &[stream_id, stream] = *std::next(m_streams.begin(), ssize_t(i));
        std::string_view stream_body = {(char *) stream.body.data(), stream.body.size()};
        std::string_view body = BODIES[i]; // NOLINT(*-pro-bounds-constant-array-index)
        ASSERT_EQ(body, stream_body) << i;
        ASSERT_EQ(!body.empty(), stream.body_finished);
        ASSERT_EQ(stream.error, 0);
        ASSERT_TRUE(stream.response.has_value());
        ASSERT_TRUE(stream.stream_finished);
    }
}

TEST_F(Http1Client, PipelineSinglePacket) {
    constexpr std::string_view BODIES[] = {"", "aaa", "bbb", "ccc"};

    ag::http::Request request(ag::http::HTTP_1_1, "GET", "/");
    for (size_t i = 0; i < std::size(BODIES); ++i) {
        ag::Result result = m_client.send_request(request);
        ASSERT_TRUE(result.has_value()) << result.error()->str();
    }

    std::string responses;
    for (std::string_view body : BODIES) {
        responses.append(AG_FMT("HTTP/1.1 200 OK\r\n"
                                "Content-Length: {}\r\n\r\n"
                                "{}\r\n",
                body.length(), body));
    }
    ASSERT_NO_FATAL_FAILURE(check_result(
            m_client.input({(uint8_t *) responses.data(), responses.size()}), ag::http::Http1Client::InputOk{}));

    ASSERT_EQ(m_streams.size(), std::size(BODIES));
    for (size_t i = 0; i < std::size(BODIES); ++i) {
        const auto &[stream_id, stream] = *std::next(m_streams.begin(), ssize_t(i));
        std::string_view stream_body = {(char *) stream.body.data(), stream.body.size()};
        std::string_view body = BODIES[i]; // NOLINT(*-pro-bounds-constant-array-index)
        ASSERT_EQ(body, stream_body);
        ASSERT_EQ(!body.empty(), stream.body_finished);
        ASSERT_EQ(stream.error, 0);
        ASSERT_TRUE(stream.response.has_value());
        ASSERT_TRUE(stream.stream_finished);
    }
}

TEST_F(Http1Client, IntermediateResponse) {
    static constexpr std::string_view CONTINUE_RESPONSE = "HTTP/1.1 100 Continue\r\n\r\n";
    static constexpr std::string_view MAIN_RESPONSE = "HTTP/1.1 200 OK\r\n\r\n";

    ag::http::Request request(ag::http::HTTP_1_1, "GET", "/");
    ag::Result result = m_client.send_request(request);
    ASSERT_TRUE(result.has_value()) << result.error()->str();

    ASSERT_NO_FATAL_FAILURE(
            check_result(m_client.input({(uint8_t *) CONTINUE_RESPONSE.data(), CONTINUE_RESPONSE.size()}),
                    ag::http::Http1Client::InputOk{}));

    ASSERT_EQ(m_streams.size(), 1);
    const Stream &stream = m_streams.begin()->second;
    ASSERT_EQ(0, stream.body.size());
    ASSERT_FALSE(stream.body_finished);
    ASSERT_EQ(stream.error, 0);
    ASSERT_TRUE(stream.response.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_EQ(CONTINUE_RESPONSE, stream.response->str());

    ASSERT_NO_FATAL_FAILURE(check_result(m_client.input({(uint8_t *) MAIN_RESPONSE.data(), MAIN_RESPONSE.size()}),
            ag::http::Http1Client::InputOk{}));

    ASSERT_EQ(m_streams.size(), 1);
    ASSERT_EQ(0, stream.body.size());
    ASSERT_FALSE(stream.body_finished);
    ASSERT_EQ(stream.error, 0);
    ASSERT_TRUE(stream.response.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_EQ(MAIN_RESPONSE, stream.response->str());
}

struct DecodingSample {
    std::string_view data;
    size_t content_length;
    bool body_finished;
    bool stream_finished;
};

struct Http1ClientDecoding : public Http1Client, public ::testing::WithParamInterface<DecodingSample> {
protected:
    void SetUp() override {
        Http1Client::SetUp();

        ag::http::Request request(ag::http::HTTP_1_1, "GET", "/");
        ag::Result result = m_client.send_request(request);
        ASSERT_TRUE(result.has_value()) << result.error()->str();
    }
};

TEST_P(Http1ClientDecoding, SingleChunk) {
    const DecodingSample &sample = GetParam();
    ASSERT_NO_FATAL_FAILURE(check_result(
            m_client.input({(uint8_t *) sample.data.data(), sample.data.size()}), ag::http::Http1Client::InputOk{}));

    if (sample.data.empty()) {
        ASSERT_EQ(m_streams.size(), 0);
        return;
    }
    ASSERT_EQ(m_streams.size(), 1);
    const Stream &stream = m_streams.begin()->second;
    ASSERT_EQ(sample.content_length, stream.body.size());
    ASSERT_EQ(sample.body_finished, stream.body_finished);
    ASSERT_EQ(stream.error, 0);
    if (sample.data.empty()) {
        ASSERT_FALSE(stream.response.has_value());
        return;
    }
    ASSERT_TRUE(stream.response.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_TRUE(ag::utils::starts_with(sample.data, stream.response->str())) << stream.response->str();
}

TEST_P(Http1ClientDecoding, MultipleChunks) {
    const DecodingSample &sample = GetParam();
    for (char b : sample.data) {
        ASSERT_NO_FATAL_FAILURE(check_result(m_client.input({(uint8_t *) &b, 1}), ag::http::Http1Client::InputOk{}));
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
    if (sample.data.empty()) {
        ASSERT_FALSE(stream.response.has_value());
        return;
    }
    ASSERT_TRUE(stream.response.has_value());
    // NOLINTNEXTLINE(*-unchecked-optional-access)
    ASSERT_TRUE(ag::utils::starts_with(sample.data, stream.response->str())) << stream.response->str();
}

static constexpr DecodingSample DECODING_SAMPLES[] = {
        // Zero bytes is a valid input for HTTP/1 session
        {},
        {
                .data = "HTTP/1.1 301 Moved Permanently\r\n"
                        "Location: http://www.google.com/\r\n"
                        "Content-Type: text/html; charset=UTF-8\r\n"
                        "Date: Sun, 26 Apr 2009 11:11:49 GMT\r\n"
                        "Expires: Tue, 26 May 2009 11:11:49 GMT\r\n"
                        "X-$PrototypeBI-Version: 1.6.0.3\r\n" /* $ char in header field */
                        "Cache-Control: public, max-age=2592000\r\n"
                        "Server: gws\r\n"
                        "Content-Length: 219  \r\n"
                        "\r\n"
                        "<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\n"
                        "<TITLE>301 Moved</TITLE></HEAD><BODY>\n"
                        "<H1>301 Moved</H1>\n"
                        "The document has moved\n"
                        "<A HREF=\"http://www.google.com/\">here</A>.\r\n"
                        "</BODY></HTML>\r\n",
                .content_length = 219,
                .body_finished = true,
                .stream_finished = true,
        },
        {
                .data = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Transfer-Encoding: chunked\r\n"
                        "\r\n"
                        "25\r\n"
                        "This is the data in the first chunk\r\n"
                        "\r\n"
                        "1C\r\n"
                        "and this is the second one\r\n"
                        "\r\n"
                        "0\r\n\r\n",
                .content_length = 65,
                .body_finished = true,
                .stream_finished = true,
        },
        {
                .data = "HTTP/1.1 500 ВАСИЛИЙ\r\n"
                        "Date: Fri, 5 Nov 2010 23:07:12 GMT+2\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                .content_length = 0,
                .body_finished = false,
                .stream_finished = false,
        },
        {
                .data = "HTTP/1.1 301 MovedPermanently\r\n"
                        "Date: Wed, 15 May 2013 17:06:33 GMT\r\n"
                        "Server: Server\r\n"
                        "x-amz-id-1: 0GPHKXSJQ826RK7GZEB2\r\n"
                        "p3p: policyref=\"http://www.amazon.com/w3c/p3p.xml\",CP=\"CAO DSP LAW CUR ADM IVAo IVDo CONo "
                        "OTPo OUR "
                        "DELi PUBi OTRi BUS PHY ONL UNI PUR FIN COM NAV INT DEM CNT STA HEA PRE LOC GOV OTC \"\r\n"
                        "x-amz-id-2: STN69VZxIFSz9YJLbz1GDbxpbjG6Qjmmq5E3DxRhOUw+Et0p4hr7c/Q8qNcx4oAD\r\n"
                        "Location: "
                        "http://www.amazon.com/Dan-Brown/e/B000AP9DSU/"
                        "ref=s9_pop_gw_al1?_encoding=UTF8&refinementId=618073011&pf_rd_m=ATVPDKIKX0DER&pf_rd_s=center-"
                        "2&pf_rd_"
                        "r=0SHYY5BZXN3KR20BNFAY&pf_rd_t=101&pf_rd_p=1263340922&pf_rd_i=507846\r\n"
                        "Vary: Accept-Encoding,User-Agent\r\n"
                        "Content-Type: text/html; charset=ISO-8859-1\r\n"
                        "Transfer-Encoding: chunked\r\n"
                        "\r\n"
                        "1\r\n"
                        "\n\r\n"
                        "0\r\n"
                        "\r\n",
                .content_length = 1,
                .body_finished = true,
                .stream_finished = true,
        },
};

INSTANTIATE_TEST_SUITE_P(Test, Http1ClientDecoding, ::testing::ValuesIn(DECODING_SAMPLES));

struct EncodingSample {
    std::string_view method;
    std::string_view path;
    std::vector<std::pair<std::string_view, std::string_view>> headers;
    std::string_view body;
    std::string_view expected;
};

struct Http1ClientEncoding : public Http1Client, public ::testing::WithParamInterface<EncodingSample> {};

TEST_P(Http1ClientEncoding, Test) {
    const EncodingSample &sample = GetParam();

    ag::http::Request request(ag::http::HTTP_1_1, std::string{sample.method}, std::string{sample.path});
    for (auto [name, value] : sample.headers) {
        request.headers().put(std::string(name), std::string(value));
    }

    ag::Result result = m_client.send_request(request);
    ASSERT_TRUE(result.has_value()) << result.error()->str();
    uint32_t stream_id = result.value();
    ASSERT_NO_FATAL_FAILURE(
            check_no_error(m_client.send_body(stream_id, {(uint8_t *) sample.body.data(), sample.body.size()}, true)));

    ASSERT_EQ(sample.expected, m_output);
}

static const EncodingSample ENCODING_SAMPLES[] = {
        {
                .method = "GET",
                .path = "/",
                .headers = {{"Content-Length", "5"}},
                .body = "hello",
                .expected = "GET / HTTP/1.1\r\n"
                            "Content-Length: 5\r\n\r\n"
                            "hello",
        },
        {
                .method = "GET",
                .path = "/",
                .headers = {{"Transfer-Encoding", "chunked"}, {"Content-Length", "5"}},
                .body = "hello",
                .expected = "GET / HTTP/1.1\r\n"
                            "Transfer-Encoding: chunked\r\n"
                            "Content-Length: 5\r\n\r\n"
                            "5\r\n"
                            "hello\r\n"
                            "0\r\n\r\n",
        },
};

INSTANTIATE_TEST_SUITE_P(Test, Http1ClientEncoding, ::testing::ValuesIn(ENCODING_SAMPLES));
