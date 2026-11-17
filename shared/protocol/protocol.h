#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace eat::protocol {

constexpr std::size_t kHeaderSize = 40;
constexpr std::uint32_t kMagic = 0x45415431u;
constexpr std::uint8_t kVersionMajor = 1;
constexpr std::uint8_t kVersionMinor = 0;
constexpr std::uint8_t kMinSupportedMinor = 0;
constexpr std::size_t kDefaultMaxFrameSize = 65536;
constexpr std::size_t kHardMaxFrameSize = 262144;

constexpr std::uint8_t kFlagCompressed = 0x01;
constexpr std::uint8_t kFlagFragmented = 0x02;
constexpr std::uint8_t kFlagEncrypted = 0x04;
constexpr std::uint8_t kFlagUrgent = 0x08;

enum class FrameType : std::uint8_t {
    HandshakeReq = 0x01,
    HandshakeResp = 0x02,
    Request = 0x03,
    Response = 0x04,
    Heartbeat = 0x05,
    HeartbeatAck = 0x06,
    Push = 0x07,
    Close = 0x08,
    Error = 0x09,
};

inline bool IsKnownFrameType(std::uint8_t value) {
    return value >= 0x01 && value <= 0x09;
}

inline bool AllowsEmptyBody(FrameType type) {
    return type == FrameType::Heartbeat || type == FrameType::HeartbeatAck ||
           type == FrameType::Close || type == FrameType::Error;
}

enum class ErrorCode : std::uint32_t {
    Ok = 0,
    InvalidMagic = 1001,
    UnsupportedVersion = 1002,
    UnknownFrameType = 1003,
    BadFlags = 1004,
    BadHeader = 1005,
    FrameTooLarge = 1006,
    ChecksumFailed = 1007,
    InvalidJson = 1008,
    FrameWaitTimeout = 1009,
    HandshakeRequired = 2001,
    Unauthorized = 2002,
    TokenExpired = 2003,
    Forbidden = 2004,
    AccountDisabled = 2005,
    SessionReplaced = 2006,
    TooManyAuthFailures = 2007,
    RefreshReused = 2008,
    InvalidParams = 3001,
    UserNotFound = 3002,
    IdempotencyConflict = 3003,
    NotFound = 3004,
    VersionConflict = 3005,
    RateLimited = 4001,
    Overloaded = 4002,
    MaxConnections = 4003,
    SlowConsumer = 4004,
    RequestTimeout = 4005,
    ServiceUnavailable = 5001,
    CacheUnavailable = 5002,
    InternalError = 5003,
    HopsExceeded = 5004,
    ServerShutdown = 9001,
    Replaced = 9002,
    IdleTimeout = 9003,
    ProtocolError = 9004,
};

inline const char* ErrorName(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok: return "ok";
        case ErrorCode::InvalidMagic: return "invalid_magic";
        case ErrorCode::UnsupportedVersion: return "unsupported_version";
        case ErrorCode::UnknownFrameType: return "unknown_frame_type";
        case ErrorCode::BadFlags: return "bad_flags";
        case ErrorCode::BadHeader: return "bad_header";
        case ErrorCode::FrameTooLarge: return "frame_too_large";
        case ErrorCode::ChecksumFailed: return "checksum_failed";
        case ErrorCode::InvalidJson: return "invalid_json";
        case ErrorCode::FrameWaitTimeout: return "frame_wait_timeout";
        case ErrorCode::HandshakeRequired: return "handshake_required";
        case ErrorCode::Unauthorized: return "unauthorized";
        case ErrorCode::TokenExpired: return "token_expired";
        case ErrorCode::Forbidden: return "forbidden";
        case ErrorCode::AccountDisabled: return "account_disabled";
        case ErrorCode::SessionReplaced: return "session_replaced";
        case ErrorCode::TooManyAuthFailures: return "too_many_auth_failures";
        case ErrorCode::RefreshReused: return "refresh_reused";
        case ErrorCode::InvalidParams: return "invalid_params";
        case ErrorCode::UserNotFound: return "user_not_found";
        case ErrorCode::IdempotencyConflict: return "idempotency_conflict";
        case ErrorCode::NotFound: return "not_found";
        case ErrorCode::VersionConflict: return "version_conflict";
        case ErrorCode::RateLimited: return "rate_limited";
        case ErrorCode::Overloaded: return "overloaded";
        case ErrorCode::MaxConnections: return "max_connections";
        case ErrorCode::SlowConsumer: return "slow_consumer";
        case ErrorCode::RequestTimeout: return "request_timeout";
        case ErrorCode::ServiceUnavailable: return "service_unavailable";
        case ErrorCode::CacheUnavailable: return "cache_unavailable";
        case ErrorCode::InternalError: return "internal_error";
        case ErrorCode::HopsExceeded: return "hops_exceeded";
        case ErrorCode::ServerShutdown: return "server_shutdown";
        case ErrorCode::Replaced: return "replaced";
        case ErrorCode::IdleTimeout: return "idle_timeout";
        case ErrorCode::ProtocolError: return "protocol_error";
    }
    return "internal_error";
}

struct FrameHeader {
    std::uint32_t magic = kMagic;
    std::uint8_t ver_major = kVersionMajor;
    std::uint8_t ver_minor = kVersionMinor;
    FrameType frame_type = FrameType::Request;
    std::uint8_t flags = 0;
    std::uint8_t header_len = static_cast<std::uint8_t>(kHeaderSize);
    std::uint8_t reserved0 = 0;
    std::uint32_t body_len = 0;
    std::uint64_t request_id = 0;
    std::uint64_t seq = 0;
    std::uint32_t error_code = 0;
    std::uint16_t reserved1 = 0;
    std::uint32_t checksum = 0;
};

struct Frame {
    FrameHeader header;
    std::string body;
};

}  // namespace eat::protocol