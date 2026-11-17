#include "server/tcp/TcpServerApp.h"

#include <muduo/base/Timestamp.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>

#include <atomic>
#include <chrono>

namespace eat::tcp {

TcpServerApp::TcpServerApp(muduo::net::EventLoop* loop, const app::Config& config,
                           FrameDispatcher* dispatcher)
    : loop_(loop),
      config_(config),
      dispatcher_(dispatcher),
      server_(loop, muduo::net::InetAddress(config.tcp_port), "eat-server"),
      executor_(config.business_threads, config.business_queue_capacity,
                config.task_timeout_ms),
      heartbeat_(std::chrono::seconds(config.idle_timeout_s)),
      backpressure_(config.send_high_watermark, config.send_low_watermark,
                    config.global_send_high_watermark) {
    server_.setThreadNum(static_cast<int>(config.sub_reactors));
    server_.setConnectionCallback(
        [this](const muduo::net::TcpConnectionPtr& connection) { OnConnection(connection); });
    server_.setMessageCallback(
        [this](const muduo::net::TcpConnectionPtr& connection, muduo::net::Buffer* buffer,
               muduo::Timestamp timestamp) { OnMessage(connection, buffer, timestamp); });
}

TcpServerApp::~TcpServerApp() {
    Stop();
}

bool TcpServerApp::Start(std::string& error) {
    if (config_.listen_backlog == 0 || config_.sub_reactors == 0) {
        error = "invalid_listen_configuration";
        return false;
    }
    server_.start();
    loop_->runEvery(1.0, [this] { OnTimer(); });
    return true;
}

void TcpServerApp::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    const std::uint64_t now_id = 0;
    std::vector<Entry> copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy.reserve(states_.size());
        for (const auto& item : states_) {
            copy.push_back(item.second);
        }
    }
    for (auto& entry : copy) {
        CloseWithFrame(entry.connection, now_id, protocol::ErrorCode::ServerShutdown,
                       "server_shutdown");
    }
    executor_.Stop();
}

void TcpServerApp::OnConnection(const muduo::net::TcpConnectionPtr& connection) {
    if (connection->connected()) {
        const std::uint64_t count = active_connections_.fetch_add(1) + 1;
        if (count > config_.max_connections) {
            active_connections_.fetch_sub(1);
            connection->shutdown();
            return;
        }
        auto state = std::make_shared<ConnectionState>(config_.max_frame_size);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state->context.conn_id = ++next_conn_id_;
            states_[connection.get()] = Entry{connection, state};
        }
        heartbeat_.OnFrame(state->context.conn_id, std::chrono::steady_clock::now());
        return;
    }

    std::shared_ptr<ConnectionState> state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = states_.find(connection.get());
        if (it != states_.end()) {
            state = it->second.state;
            states_.erase(it);
        }
    }
    if (state) {
        heartbeat_.Remove(state->context.conn_id);
        rate_limiter_.Remove(std::to_string(state->context.conn_id));
        state->context.Close(protocol::ErrorCode::ProtocolError);
    }
    active_connections_.fetch_sub(1);
}

void TcpServerApp::OnMessage(const muduo::net::TcpConnectionPtr& connection,
                             muduo::net::Buffer* buffer, muduo::Timestamp) {
    auto state = FindState(connection);
    if (!state) return;

    state->codec.Append(buffer->peek(), buffer->readableBytes());
    buffer->retrieveAll();

    state->context.Touch(std::chrono::steady_clock::now());
    heartbeat_.OnFrame(state->context.conn_id, std::chrono::steady_clock::now());

    const std::string rate_key = std::to_string(state->context.conn_id);
    if (!rate_limiter_.Allow(rate_key, config_.rate_per_conn_per_s,
                             config_.rate_per_conn_burst,
                             std::chrono::steady_clock::now())) {
        SendError(connection, 0, protocol::ErrorCode::RateLimited, "rate_limited");
        return;
    }

    for (int processed = 0; processed < 64; ++processed) {
        protocol::Frame frame;
        protocol::ErrorCode error = protocol::ErrorCode::Ok;
        std::string reason;
        const auto result = state->codec.Next(frame, error, reason);
        if (result == FrameCodec::Result::NeedMore) return;
        if (result == FrameCodec::Result::Error) {
            state->context.bad_frame_count += 1;
            CloseWithFrame(connection, frame.header.request_id, error, reason);
            return;
        }
        state->context.recv_frame_count += 1;
        DispatchFrame(connection, state, frame);
    }
    if (state->codec.buffered() >= state->codec.max_frame_size()) {
        CloseWithFrame(connection, 0, protocol::ErrorCode::FrameTooLarge, "buffer_overflow");
    }
}

void TcpServerApp::DispatchFrame(const muduo::net::TcpConnectionPtr& connection,
                                 std::shared_ptr<ConnectionState> state,
                                 const protocol::Frame& frame) {
    if (frame.header.frame_type == protocol::FrameType::Heartbeat) {
        const std::string ack =
            protocol::FrameCodec::Encode(protocol::FrameType::HeartbeatAck,
                                         frame.header.request_id, "{}");
        connection->send(ack);
        return;
    }

    if (!executor_.Submit([this, connection, state, frame] {
            const DispatchOutcome outcome = dispatcher_->Handle(state->context, frame);
            connection->getLoop()->runInLoop([this, connection, frame, outcome] {
                SendOutcome(connection, frame, outcome);
            });
        })) {
        SendError(connection, frame.header.request_id, protocol::ErrorCode::Overloaded,
                  "overloaded");
    }
}

void TcpServerApp::SendOutcome(const muduo::net::TcpConnectionPtr& connection,
                               const protocol::Frame& request,
                               const DispatchOutcome& outcome) {
    const std::string wire =
        protocol::FrameCodec::Encode(outcome.response_type, request.header.request_id,
                                     outcome.body, 0,
                                     static_cast<std::uint32_t>(outcome.error));
    if (!backpressure_.OnQueued(wire.size())) {
        SendError(connection, request.header.request_id, protocol::ErrorCode::Overloaded,
                  "overloaded");
        return;
    }
    connection->send(wire);
    backpressure_.OnDrained(wire.size());
    if (outcome.close_after) {
        connection->shutdown();
    }
}

void TcpServerApp::SendError(const muduo::net::TcpConnectionPtr& connection,
                             std::uint64_t request_id, protocol::ErrorCode code,
                             const std::string& reason) {
    protocol::FrameHeader header;
    header.request_id = request_id;
    const std::string wire =
        protocol::FrameCodec::Encode(protocol::FrameType::Error, request_id, reason, 0,
                                     static_cast<std::uint32_t>(code));
    if (backpressure_.OnQueued(wire.size())) {
        connection->send(wire);
        backpressure_.OnDrained(wire.size());
    }
}

void TcpServerApp::CloseWithFrame(const muduo::net::TcpConnectionPtr& connection,
                                  std::uint64_t request_id, protocol::ErrorCode code,
                                  const std::string& reason) {
    protocol::JsonValue body = protocol::JsonValue::Object();
    body.Set("reason", protocol::JsonValue::String(reason));
    body.Set("retry_after_ms", protocol::JsonValue::Int(1000));
    const std::string wire = protocol::FrameCodec::Encode(
        protocol::FrameType::Close, request_id, body.Dump(), 0,
        static_cast<std::uint32_t>(code));
    connection->send(wire);
    connection->shutdown();
}

std::shared_ptr<TcpServerApp::ConnectionState> TcpServerApp::FindState(
    const muduo::net::TcpConnectionPtr& connection) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(connection.get());
    return it == states_.end() ? nullptr : it->second.state;
}

void TcpServerApp::OnTimer() {
    const auto expired = heartbeat_.Sweep(std::chrono::steady_clock::now());
    std::vector<muduo::net::TcpConnectionPtr> closings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::uint64_t conn_id : expired) {
            for (auto& item : states_) {
                if (item.second.state->context.conn_id == conn_id) {
                    closings.push_back(item.second.connection);
                    break;
                }
            }
        }
    }
    for (const auto& connection : closings) {
        CloseWithFrame(connection, 0, protocol::ErrorCode::IdleTimeout, "idle_timeout");
    }
}

}  // namespace eat::tcp