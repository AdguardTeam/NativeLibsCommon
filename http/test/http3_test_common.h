#pragma once

#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/util.h>

#include "common/defs.h"
#include "common/http/headers.h"
#include "common/http/http3.h"
#include "common/socket_address.h"

static constexpr const char *SERVER_NAME = "www.example.com";
static constexpr const char *TRAILER_REQUEST_PATH = "/trailer";
static constexpr const char *DOWNLOAD_REQUEST_PATH = "/download";
static constexpr uint64_t DOWNLOAD_SIZE = 4 * ag::http::Http3Settings::DEFAULT_INITIAL_MAX_DATA;
static constexpr const char *UPLOAD_REQUEST_PATH = "/upload";
static constexpr uint64_t UPLOAD_SIZE = 4 * ag::http::Http3Settings::DEFAULT_INITIAL_MAX_DATA;

struct ServerSide {
    enum State : int;

    struct Session {
        Session(ServerSide *parent, const ag::SocketAddress &peer);
        ~Session() = default;

        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;
        Session(Session &&) = delete;
        Session &operator=(Session &&) = delete;

        struct Stream {
            std::optional<ag::http::Request> request;
            std::optional<ag::http::Headers> trailer;
            std::vector<uint8_t> body;
            bool read_finished = false;
            bool closed = false;
        };

        ServerSide *parent;
        std::unordered_map<uint64_t, Stream> streams;
        std::unique_ptr<ag::http::Http3Server> server;
        ag::SocketAddress peer;
        ag::UniquePtr<event, &event_free> expiry_timer;

        void flush();
    };

    const ag::SocketAddress &bound_address();
    void run();
    void stop();

    ag::UniquePtr<event_base, &event_base_free> base{event_base_new()};
    std::thread worker_thread;
    State state = State{};
    ag::SocketAddress bound_addr{"127.0.0.1:0"};
    evutil_socket_t fd = -1;
    std::unordered_map<ag::SocketAddress, std::unique_ptr<Session>> sessions;
    std::unordered_map<ag::SocketAddress, std::unique_ptr<Session>> closing_sessions;
};
