#include "client/transport/TcpTransport.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include "shared/protocol/StrictJson.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace eat::client {
namespace {

#ifdef _WIN32
int CloseHandle(std::intptr_t handle) { return closesocket(static_cast<SOCKET>(handle)); }
#else
int CloseHandle(std::intptr_t handle) { return close(static_cast<int>(handle)); }
#endif

std::string BuildHandshakeBody(const TransportConfig& config) {
    protocol::JsonValue body = protocol::JsonValue::Object();
    if (!config.access_token.empty()) {
        body.Set("access_token", protocol::JsonValue::String(config.access_token));
    } else if (!config.login_id.empty()) {
        body.Set("login_id", protocol::JsonValue::String(config.login_id));
        body.Set("password", protocol::JsonValue::String(config.password));
        body.Set("display_name", protocol::JsonValue::String(config.display_name));
        if (config.register_account) {
            body.Set("action", protocol::JsonValue::String("register"));
        }
        if (!config.captcha_id.empty()) {
            body.Set("captcha_id", protocol::JsonValue::String(config.captcha_id));
        }
        if (!config.captcha_code.empty()) {
            body.Set("captcha_code", protocol::JsonValue::String(config.captcha_code));
        }
    } else {
        body.Set("access_token", protocol::JsonValue::String(std::string()));
    }
    body.Set("device_id", protocol::JsonValue::String(config.device_id));
    body.Set("client_version", protocol::JsonValue::String(config.client_version));
    protocol::JsonValue version = protocol::JsonValue::Object();
    version.Set("major", protocol::JsonValue::Int(1));
    version.Set("minor", protocol::JsonValue::Int(0));
    body.Set("protocol_version", version);
    body.Set("capabilities", protocol::JsonValue::Array());
    body.Set("max_frame_accept", protocol::JsonValue::Int(65536));
    return body.Dump();
}

}  // namespace

TcpTransport::TcpTransport(TransportConfig config)
    : config_(std::move(config)), codec_(65536) {}

TcpTransport::~TcpTransport() {
    Stop();
}

bool TcpTransport::Start(std::string& error) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "wsa_startup_failed";
        return false;
    }
#endif
    running_.store(true);
    read_thread_ = std::thread([this] { ReadLoop(); });
    if (!ConnectOnce(error)) {
        running_.store(false);
        connect_cv_.notify_all();
        CloseSocket();
        FailAllPending();
        if (read_thread_.joinable()) read_thread_.join();
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    heartbeat_thread_ = std::thread([this] { HeartbeatLoop(); });
    reconnect_thread_ = std::thread([this] { ReconnectLoop(); });
    return true;
}

void TcpTransport::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    CloseSocket();
    connect_cv_.notify_all();
    FailAllPending();
    if (read_thread_.joinable()) read_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (reconnect_thread_.joinable()) reconnect_thread_.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool TcpTransport::Connected() const {
    return connected_.load();
}

bool TcpTransport::ConnectSocket(std::string& error) {
#ifdef _WIN32
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const std::string port = std::to_string(config_.port);
    if (getaddrinfo(config_.host.c_str(), port.c_str(), &hints, &result) != 0) {
        error = "dns_failed";
        return false;
    }
    std::intptr_t handle = -1;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        handle = static_cast<std::intptr_t>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
        if (handle == static_cast<std::intptr_t>(INVALID_SOCKET)) continue;
        if (connect(static_cast<SOCKET>(handle), it->ai_addr,
                    static_cast<int>(it->ai_addrlen)) == 0) {
            break;
        }
        CloseHandle(handle);
        handle = -1;
    }
    freeaddrinfo(result);
    if (handle == -1) {
        error = "connect_failed";
        return false;
    }
#else
    int handle = socket(AF_INET, SOCK_STREAM, 0);
    if (handle < 0) {
        error = "socket_failed";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
        CloseHandle(handle);
        error = "bad_address";
        return false;
    }
    if (connect(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        CloseHandle(handle);
        error = "connect_failed";
        return false;
    }
#endif

    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_handle_ = handle;
        codec_.Reset();
    }
    connected_.store(true);
    connect_cv_.notify_all();
    return true;
}

bool TcpTransport::ConnectOnce(std::string& error) {
    if (!ConnectSocket(error)) return false;
    if (!Handshake(error)) {
        CloseSocket();
        return false;
    }
    return true;
}

bool TcpTransport::StartForCaptcha(std::string& error) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        error = "wsa_startup_failed";
        return false;
    }
#endif
    running_.store(true);
    read_thread_ = std::thread([this] { ReadLoop(); });
    if (!ConnectSocket(error)) {
        running_.store(false);
        connect_cv_.notify_all();
        CloseSocket();
        FailAllPending();
        if (read_thread_.joinable()) read_thread_.join();
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    return true;
}

bool TcpTransport::Handshake(std::string& error) {
    const std::uint64_t request_id = next_request_id_.fetch_add(1);
    auto pending = std::make_shared<Pending>();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[request_id] = pending;
    }
    if (!SendFrame(protocol::FrameType::HandshakeReq, request_id,
                   BuildHandshakeBody(config_))) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(request_id);
        error = "handshake_send_failed";
        return false;
    }
    std::unique_lock<std::mutex> lock(pending->mutex);
    if (!pending->cv.wait_for(lock, std::chrono::milliseconds(config_.connect_timeout_ms),
                              [&pending] { return pending->done; })) {
        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
        pending_.erase(request_id);
        error = "handshake_timeout";
        return false;
    }
    if (pending->frame.header.frame_type != protocol::FrameType::HandshakeResp) {
        const auto rejected = protocol::ParseStrictJson(pending->frame.body);
        if (rejected.ok && rejected.value.IsObject()) {
            const protocol::JsonValue* detail = rejected.value.Find("message");
            if (detail == nullptr) detail = rejected.value.Find("reason");
            if (detail != nullptr && detail->IsString() && !detail->AsString().empty()) {
                error = detail->AsString();
            }
        }
        if (error.empty() || error == "handshake_rejected") error = "handshake_rejected";
        return false;
    }
    const auto parsed = protocol::ParseStrictJson(pending->frame.body);
    if (!parsed.ok || !parsed.value.IsObject()) {
        error = "handshake_bad_body";
        return false;
    }
    const protocol::JsonValue* user_key = parsed.value.Find("user_key");
    if (user_key != nullptr && user_key->IsString()) user_key_ = user_key->AsString();
    const protocol::JsonValue* access_token = parsed.value.Find("access_token");
    if (access_token != nullptr && access_token->IsString()) {
        access_token_ = access_token->AsString();
    }
    const protocol::JsonValue* refresh_token = parsed.value.Find("refresh_token");
    if (refresh_token != nullptr && refresh_token->IsString()) {
        refresh_token_ = refresh_token->AsString();
    }
    error.clear();
    return true;
}

bool TcpTransport::Request(const std::string& method, const protocol::JsonValue& params,
                           protocol::JsonValue& data, protocol::ErrorCode& error,
                           std::string& message, std::uint32_t timeout_ms) {
    if (!connected_.load()) {
        error = protocol::ErrorCode::ServiceUnavailable;
        message = "data_unavailable";
        return false;
    }
    const std::uint64_t request_id = next_request_id_.fetch_add(1);
    protocol::JsonValue body = protocol::JsonValue::Object();
    body.Set("method", protocol::JsonValue::String(method));
    body.Set("params", params);
    auto pending = std::make_shared<Pending>();
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_[request_id] = pending;
    }
    if (!SendFrame(protocol::FrameType::Request, request_id, body.Dump())) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.erase(request_id);
        error = protocol::ErrorCode::ServiceUnavailable;
        message = "send_failed";
        return false;
    }
    const std::uint32_t wait_ms = timeout_ms == 0 ? config_.request_timeout_ms : timeout_ms;
    std::unique_lock<std::mutex> lock(pending->mutex);
    if (!pending->cv.wait_for(lock, std::chrono::milliseconds(wait_ms),
                              [&pending] { return pending->done; })) {
        std::lock_guard<std::mutex> pending_lock(pending_mutex_);
        pending_.erase(request_id);
        error = protocol::ErrorCode::RequestTimeout;
        message = "request_timeout";
        return false;
    }
    const protocol::Frame& frame = pending->frame;
    if (frame.header.frame_type == protocol::FrameType::Error) {
        error = static_cast<protocol::ErrorCode>(frame.header.error_code);
        message = frame.body;
        return false;
    }
    const auto parsed = protocol::ParseStrictJson(frame.body);
    if (!parsed.ok || !parsed.value.IsObject()) {
        error = protocol::ErrorCode::InternalError;
        message = "bad_response";
        return false;
    }
    const protocol::JsonValue* ok = parsed.value.Find("ok");
    if (ok == nullptr || !ok->AsBool()) {
        const protocol::JsonValue* error_body = parsed.value.Find("error");
        error = static_cast<protocol::ErrorCode>(frame.header.error_code);
        message = error_body != nullptr ? error_body->Dump() : "request_failed";
        return false;
    }
    const protocol::JsonValue* data_value = parsed.value.Find("data");
    data = data_value != nullptr ? *data_value : protocol::JsonValue();
    error = protocol::ErrorCode::Ok;
    message.clear();
    return true;
}

bool TcpTransport::SendFrame(protocol::FrameType type, std::uint64_t request_id,
                             const std::string& body) {
    const std::string wire = protocol::FrameCodec::Encode(type, request_id, body);
    return SendAll(wire.data(), wire.size());
}

bool TcpTransport::SendAll(const char* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_handle_ == -1) return false;
    std::size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        const int n = static_cast<int>(send(static_cast<SOCKET>(socket_handle_), data + sent,
                                            static_cast<int>(size - sent), 0));
#else
        const int n = static_cast<int>(send(static_cast<int>(socket_handle_), data + sent,
                                            size - sent, 0));
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void TcpTransport::ReadLoop() {
    std::vector<char> buffer(8192);
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(connect_mutex_);
            connect_cv_.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return !running_.load() || socket_handle_ != -1;
            });
        }
        std::intptr_t handle = -1;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            handle = socket_handle_;
        }
        if (handle == -1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
#ifdef _WIN32
        const int n = static_cast<int>(recv(static_cast<SOCKET>(handle), buffer.data(),
                                            static_cast<int>(buffer.size()), 0));
#else
        const int n = static_cast<int>(recv(static_cast<int>(handle), buffer.data(),
                                            buffer.size(), 0));
#endif
        if (n <= 0) {
            connected_.store(false);
            CloseSocket();
            FailAllPending();
            continue;
        }
        codec_.Append(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                      static_cast<std::size_t>(n));
        for (;;) {
            protocol::Frame frame;
            protocol::ErrorCode error = protocol::ErrorCode::Ok;
            std::string reason;
            const auto result = codec_.Next(frame, error, reason);
            if (result == protocol::FrameCodec::Result::NeedMore) break;
            if (result == protocol::FrameCodec::Result::Error) {
                connected_.store(false);
                CloseSocket();
                FailAllPending();
                break;
            }
            if (frame.header.frame_type == protocol::FrameType::Close) {
                connected_.store(false);
                CloseSocket();
                FailAllPending();
                break;
            }
            CompletePending(frame.header.request_id, frame);
        }
    }
}

void TcpTransport::HeartbeatLoop() {
    while (running_.load()) {
        for (std::uint32_t elapsed = 0; elapsed < config_.heartbeat_s * 10; ++elapsed) {
            if (!running_.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (connected_.load()) {
            SendFrame(protocol::FrameType::Heartbeat, next_request_id_.fetch_add(1), "");
        }
    }
}

void TcpTransport::ReconnectLoop() {
    std::uint32_t backoff_ms = 1000;
    while (running_.load()) {
        if (connected_.load()) {
            backoff_ms = 1000;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        if (!running_.load()) return;
        std::string error;
        if (ConnectOnce(error)) {
            backoff_ms = 1000;
        } else {
            backoff_ms = std::min(backoff_ms * 2, 30000u);
        }
    }
}

void TcpTransport::CloseSocket() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_handle_ != -1) {
#ifdef _WIN32
        shutdown(static_cast<SOCKET>(socket_handle_), SD_BOTH);
#else
        shutdown(static_cast<int>(socket_handle_), SHUT_RDWR);
#endif
        CloseHandle(socket_handle_);
        socket_handle_ = -1;
    }
    codec_.Reset();
}

void TcpTransport::FailAllPending() {
    std::vector<std::shared_ptr<Pending>> pending;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto& item : pending_) pending.push_back(item.second);
        pending_.clear();
    }
    for (auto& item : pending) {
        {
            std::lock_guard<std::mutex> lock(item->mutex);
            item->done = true;
            item->frame.header.frame_type = protocol::FrameType::Error;
            item->frame.header.error_code =
                static_cast<std::uint32_t>(protocol::ErrorCode::ServiceUnavailable);
            item->frame.body = "data_unavailable";
        }
        item->cv.notify_all();
    }
}

void TcpTransport::CompletePending(std::uint64_t request_id,
                                   const protocol::Frame& frame) {
    std::shared_ptr<Pending> pending;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        const auto it = pending_.find(request_id);
        if (it != pending_.end()) {
            pending = it->second;
            pending_.erase(it);
        }
    }
    if (!pending) return;
    {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->frame = frame;
        pending->done = true;
    }
    pending->cv.notify_all();
}

}  // namespace eat::client
