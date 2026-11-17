#include "shared/protocol/FrameCodec.h"

#include <cstring>

#include "shared/protocol/Crc32c.h"

namespace eat::protocol {
namespace {

std::uint32_t ReadU32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) |
                                      static_cast<std::uint16_t>(p[1]));
}

std::uint64_t ReadU64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | p[i];
    }
    return value;
}

void WriteU32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(value & 0xFFu);
}

void WriteU16(std::uint8_t* p, std::uint16_t value) {
    p[0] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    p[1] = static_cast<std::uint8_t>(value & 0xFFu);
}

void WriteU64(std::uint8_t* p, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        p[7 - i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFFu);
    }
}

bool ParseHeader(const std::uint8_t* p, FrameHeader& out, ErrorCode& error,
                 std::string& reason) {
    out.magic = ReadU32(p);
    if (out.magic != kMagic) {
        error = ErrorCode::InvalidMagic;
        reason = "magic";
        return false;
    }
    out.ver_major = p[4];
    out.ver_minor = p[5];
    if (out.ver_major != kVersionMajor || out.ver_minor < kMinSupportedMinor ||
        out.ver_minor > kVersionMinor) {
        error = ErrorCode::UnsupportedVersion;
        reason = "version";
        return false;
    }
    if (!IsKnownFrameType(p[6])) {
        error = ErrorCode::UnknownFrameType;
        reason = "frame_type";
        return false;
    }
    out.frame_type = static_cast<FrameType>(p[6]);
    out.flags = p[7];
    if ((out.flags & ~kFlagUrgent) != 0) {
        error = ErrorCode::BadFlags;
        reason = "flags";
        return false;
    }
    if ((out.flags & kFlagUrgent) != 0 && out.frame_type != FrameType::Close) {
        error = ErrorCode::BadFlags;
        reason = "flags";
        return false;
    }
    out.header_len = p[8];
    out.reserved0 = p[9];
    if (out.header_len != kHeaderSize || out.reserved0 != 0) {
        error = ErrorCode::BadHeader;
        reason = "header_len";
        return false;
    }
    out.body_len = ReadU32(p + 10);
    out.request_id = ReadU64(p + 14);
    out.seq = ReadU64(p + 22);
    out.error_code = ReadU32(p + 30);
    out.reserved1 = ReadU16(p + 34);
    if (out.reserved1 != 0) {
        error = ErrorCode::BadHeader;
        reason = "reserved";
        return false;
    }
    out.checksum = ReadU32(p + 36);
    return true;
}

}  // namespace

FrameCodec::FrameCodec(std::size_t max_frame_size) : max_frame_size_(max_frame_size) {
    if (max_frame_size_ > kHardMaxFrameSize) {
        max_frame_size_ = kHardMaxFrameSize;
    }
    if (max_frame_size_ < 1) {
        max_frame_size_ = 1;
    }
}

void FrameCodec::Append(const std::uint8_t* data, std::size_t len) {
    if (len == 0) {
        return;
    }
    buffer_.insert(buffer_.end(), data, data + len);
}

std::size_t FrameCodec::available() const {
    return buffer_.size() - read_pos_;
}

std::size_t FrameCodec::buffered() const {
    return available();
}

std::size_t FrameCodec::max_frame_size() const {
    return max_frame_size_;
}

void FrameCodec::Reset() {
    buffer_.clear();
    read_pos_ = 0;
}

void FrameCodec::Compact() {
    if (read_pos_ == 0) {
        return;
    }
    if (read_pos_ >= 4096 || available() < 4096) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
        read_pos_ = 0;
    }
}

FrameCodec::Result FrameCodec::Next(Frame& out, ErrorCode& error, std::string& reason) {
    if (available() < kHeaderSize) {
        return Result::NeedMore;
    }

    const std::uint8_t* header = buffer_.data() + read_pos_;
    FrameHeader parsed;
    if (!ParseHeader(header, parsed, error, reason)) {
        return Result::Error;
    }

    if (parsed.body_len > max_frame_size_) {
        error = ErrorCode::FrameTooLarge;
        reason = "body_len";
        return Result::Error;
    }
    if ((parsed.frame_type == FrameType::Request ||
         parsed.frame_type == FrameType::Response) &&
        parsed.body_len == 0) {
        error = ErrorCode::InvalidJson;
        reason = "syntax";
        return Result::Error;
    }
    if (available() < kHeaderSize + parsed.body_len) {
        return Result::NeedMore;
    }

    const std::uint8_t* body = header + kHeaderSize;
    const std::uint32_t actual = Crc32c(header, 36, body, parsed.body_len);
    if (actual != parsed.checksum) {
        error = ErrorCode::ChecksumFailed;
        reason = "checksum";
        return Result::Error;
    }

    out.header = parsed;
    out.body.assign(reinterpret_cast<const char*>(body), parsed.body_len);
    read_pos_ += kHeaderSize + parsed.body_len;
    Compact();
    return Result::Ok;
}

std::string FrameCodec::Encode(FrameType type, std::uint64_t request_id,
                               const std::string& body, std::uint64_t seq,
                               std::uint32_t error, std::uint8_t flags) {
    std::uint8_t header[kHeaderSize] = {};
    WriteU32(header, kMagic);
    header[4] = kVersionMajor;
    header[5] = kVersionMinor;
    header[6] = static_cast<std::uint8_t>(type);
    header[7] = flags;
    header[8] = static_cast<std::uint8_t>(kHeaderSize);
    header[9] = 0;
    WriteU32(header + 10, static_cast<std::uint32_t>(body.size()));
    WriteU64(header + 14, request_id);
    WriteU64(header + 22, seq);
    WriteU32(header + 30, error);
    WriteU16(header + 34, 0);
    const std::uint32_t checksum =
        Crc32c(header, 36, reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
    WriteU32(header + 36, checksum);

    std::string frame(reinterpret_cast<const char*>(header), kHeaderSize);
    frame.append(body);
    return frame;
}

}  // namespace eat::protocol