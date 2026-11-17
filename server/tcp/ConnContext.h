#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "shared/protocol/protocol.h"

namespace eat::tcp {

enum class ConnState {
    Connected,
    Handshaking,
    Authenticated,
    Replaced,
    Draining,
    Closing,
    Closed,
};

const char* ConnStateName(ConnState state);

struct ConnContext {
    std::uint64_t conn_id = 0;
    std::uint32_t reactor_index = 0;
    ConnState state = ConnState::Connected;
    std::string user_key;
    std::string device_id;
    std::string session_id_hash;
    std::chrono::steady_clock::time_point last_recv_mono = std::chrono::steady_clock::now();
    std::size_t read_buf_used = 0;
    std::size_t send_queued_bytes = 0;
    std::uint32_t auth_fail_count = 0;
    std::uint32_t inflight_business = 0;
    std::uint64_t recv_frame_count = 0;
    std::uint64_t bad_frame_count = 0;
    std::uint64_t identity_spoof_attempt = 0;
    std::uint64_t last_request_id = 0;

    void Touch(std::chrono::steady_clock::time_point now);
    bool IsAuthenticated() const;
    bool Close(protocol::ErrorCode reason);
};

}  // namespace eat::tcp