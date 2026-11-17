#pragma once

#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "server/app/BusinessExecutor.h"
#include "server/app/Config.h"
#include "server/tcp/Backpressure.h"
#include "server/tcp/ConnContext.h"
#include "server/tcp/FrameCodec.h"
#include "server/tcp/FrameDispatcher.h"
#include "server/tcp/HeartbeatMonitor.h"
#include "server/tcp/RateLimiter.h"

namespace eat::tcp {

class TcpServerApp {
 public:
    TcpServerApp(muduo::net::EventLoop* loop, const app::Config& config,
                 FrameDispatcher* dispatcher);
    ~TcpServerApp();

    bool Start(std::string& error);
    void Stop();

 private:
    struct ConnectionState {
        ConnContext context;
        FrameCodec codec;
        explicit ConnectionState(std::size_t max_frame_size) : codec(max_frame_size) {}
    };

    struct Entry {
        muduo::net::TcpConnectionPtr connection;
        std::shared_ptr<ConnectionState> state;
    };

    void OnConnection(const muduo::net::TcpConnectionPtr& connection);
    void OnMessage(const muduo::net::TcpConnectionPtr& connection,
                   muduo::net::Buffer* buffer, muduo::Timestamp);
    void OnTimer();
    void DispatchFrame(const muduo::net::TcpConnectionPtr& connection,
                       std::shared_ptr<ConnectionState> state,
                       const protocol::Frame& frame);
    void SendOutcome(const muduo::net::TcpConnectionPtr& connection,
                     const protocol::Frame& request, const DispatchOutcome& outcome);
    void SendError(const muduo::net::TcpConnectionPtr& connection,
                   std::uint64_t request_id, protocol::ErrorCode code,
                   const std::string& reason);
    void CloseWithFrame(const muduo::net::TcpConnectionPtr& connection,
                        std::uint64_t request_id, protocol::ErrorCode code,
                        const std::string& reason);
    std::shared_ptr<ConnectionState> FindState(const muduo::net::TcpConnectionPtr& connection);

    muduo::net::EventLoop* loop_;
    const app::Config& config_;
    FrameDispatcher* dispatcher_;
    muduo::net::TcpServer server_;
    app::BusinessExecutor executor_;
    HeartbeatMonitor heartbeat_;
    RateLimiter rate_limiter_;
    Backpressure backpressure_;

    std::mutex mutex_;
    std::unordered_map<muduo::net::TcpConnection*, Entry> states_;
    std::uint64_t next_conn_id_ = 0;
    std::atomic<std::uint64_t> active_connections_{0};
    bool stopping_ = false;
};

}  // namespace eat::tcp