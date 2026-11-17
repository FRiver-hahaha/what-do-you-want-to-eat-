#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "shared/protocol/protocol.h"

namespace eat::protocol {

class FrameCodec {
 public:
    enum class Result { Ok, NeedMore, Error };

    explicit FrameCodec(std::size_t max_frame_size = kDefaultMaxFrameSize);

    void Append(const std::uint8_t* data, std::size_t len);
    Result Next(Frame& out, ErrorCode& error, std::string& reason);

    std::size_t buffered() const;
    std::size_t max_frame_size() const;
    void Reset();

    static std::string Encode(FrameType type, std::uint64_t request_id,
                              const std::string& body, std::uint64_t seq = 0,
                              std::uint32_t error = 0, std::uint8_t flags = 0);

 private:
    void Compact();
    std::size_t available() const;

    std::vector<std::uint8_t> buffer_;
    std::size_t read_pos_ = 0;
    std::size_t max_frame_size_;
};

}  // namespace eat::protocol