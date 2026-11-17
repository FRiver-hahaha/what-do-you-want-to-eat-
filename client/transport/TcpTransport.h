#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "shared/protocol/FrameCodec.h"
#include "shared/protocol/JsonValue.h"

namespace eat::client {

struct TransportConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9443;
    std::string access_token;
    std::string login_id;
    std::string password;
    std::string display_name;
    bool register_account = false;
    std::string captcha_id;
    std::string captcha_code;
    std::string device_id;
    std::string client_version = "windows-app/1";
    std::uint32_t heartbeat_s = 60;
    std::uint32_t connect_timeout_ms = 5000;
    std::uint32_t request_timeout_ms = 3000;
};

class TcpTransport {
 public:
    explicit TcpTransport(TransportConfig config);
    ~TcpTransport();

    bool Start(std::string& error);
    bool StartForCaptcha(std::string& error);
    void Stop();
    bool Connected() const;

    const std::string& user_key() const { return user_key_; }
    const std::string& access_token() const { return access_token_; }
    const std::string& refresh_token() const { return refresh_token_; }

    bool Request(const std::string& method, const protocol::JsonValue& params,
                 protocol::JsonValue& data, protocol::ErrorCode& error,
                 std::string& message, std::uint32_t timeout_ms = 0);

 private:
    struct Pending {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        protocol::Frame frame;
    };

    bool ConnectOnce(std::string& error);
    bool ConnectSocket(std::string& error);
    bool Handshake(std::string& error);
    void ReadLoop();
    void HeartbeatLoop();
    void ReconnectLoop();
    bool SendFrame(protocol::FrameType type, std::uint64_t request_id,
                   const std::string& body);
    bool SendAll(const char* data, std::size_t size);
    void CloseSocket();
    void FailAllPending();
    void CompletePending(std::uint64_t request_id, const protocol::Frame& frame);

    TransportConfig config_;
    std::string user_key_;
    std::string access_token_;
    std::string refresh_token_;
    std::intptr_t socket_handle_ = -1;
    mutable std::mutex socket_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::uint64_t> next_request_id_{1};
    protocol::FrameCodec codec_;

    std::mutex pending_mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Pending>> pending_;
    std::mutex connect_mutex_;
    std::condition_variable connect_cv_;

    std::thread read_thread_;
    std::thread heartbeat_thread_;
    std::thread reconnect_thread_;
};

}  // namespace eat::client
