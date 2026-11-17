#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "shared/protocol/JsonValue.h"

namespace eat::ipc {

// Agent <-> App IPC frame: 4-byte little-endian length followed by a UTF-8
// strict-JSON payload. The decoder is incremental: it accepts arbitrary byte
// slices (including one byte at a time) and reports exactly how much of the
// buffer was consumed, so the socket/pipe loop never loses pipelined frames.
constexpr std::size_t kHeaderBytes = 4;
constexpr std::size_t kMaxPayloadBytes = 1024 * 1024;

enum class DecodeStatus { Ok, NeedMore, TooLarge, BadJson };

std::string Encode(const protocol::JsonValue& message);

// `buffer` holds raw bytes; on Ok the first frame is removed from `buffer`.
DecodeStatus DecodeOne(std::string& buffer, protocol::JsonValue& message,
                       std::string& reason);

}  // namespace eat::ipc