#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "shared/protocol/Crc32c.h"
#include "shared/protocol/FrameCodec.h"

using eat::protocol::ErrorCode;
using eat::protocol::Frame;
using eat::protocol::FrameCodec;
using eat::protocol::FrameType;

namespace {

void TestRoundTrip() {
    const std::string body = "{\"method\":\"profile.get\",\"params\":{}}";
    const std::string wire = FrameCodec::Encode(FrameType::Request, 42, body);
    assert(wire.size() == 40 + body.size());

    FrameCodec codec;
    codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Ok);
    assert(frame.header.request_id == 42);
    assert(frame.header.frame_type == FrameType::Request);
    assert(frame.body == body);
    assert(codec.buffered() == 0);
}

void TestStickyFrames() {
    const std::string a = FrameCodec::Encode(FrameType::Request, 1, "{\"a\":1}");
    const std::string b = FrameCodec::Encode(FrameType::Heartbeat, 2, "");
    const std::string wire = a + b;

    FrameCodec codec;
    codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Ok);
    assert(frame.header.request_id == 1);
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Ok);
    assert(frame.header.request_id == 2);
    assert(frame.header.frame_type == FrameType::Heartbeat);
}

void TestOneByteAtATime() {
    const std::string wire = FrameCodec::Encode(FrameType::Request, 7, "{\"text\":\"hello\"}");
    FrameCodec codec;
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    for (std::size_t i = 0; i < wire.size(); ++i) {
        codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data() + i), 1);
        const auto result = codec.Next(frame, error, reason);
        if (i + 1 < wire.size()) {
            assert(result == FrameCodec::Result::NeedMore);
        } else {
            assert(result == FrameCodec::Result::Ok);
            assert(frame.body == "{\"text\":\"hello\"}");
        }
    }
}

void TestOversizeRejectedBeforeBody() {
    std::uint8_t header[40] = {};
    header[0] = 'E';
    header[1] = 'A';
    header[2] = 'T';
    header[3] = '1';
    header[4] = 1;
    header[5] = 0;
    header[6] = 0x03;
    header[8] = 40;
    const std::uint32_t body_len = 70000;
    header[10] = static_cast<std::uint8_t>((body_len >> 24) & 0xFF);
    header[11] = static_cast<std::uint8_t>((body_len >> 16) & 0xFF);
    header[12] = static_cast<std::uint8_t>((body_len >> 8) & 0xFF);
    header[13] = static_cast<std::uint8_t>(body_len & 0xFF);

    FrameCodec codec;
    codec.Append(header, sizeof(header));
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Error);
    assert(error == ErrorCode::FrameTooLarge);
}

void TestBadMagic() {
    std::string wire = FrameCodec::Encode(FrameType::Request, 1, "{}");
    wire[0] = 'X';
    FrameCodec codec;
    codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Error);
    assert(error == ErrorCode::InvalidMagic);
}

void TestChecksumBitFlip() {
    std::string wire = FrameCodec::Encode(FrameType::Request, 1, "{\"a\":1}");
    wire[45] = static_cast<char>(wire[45] ^ 0x01);
    FrameCodec codec;
    codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Error);
    assert(error == ErrorCode::ChecksumFailed);
}

void TestZeroBodyRequestRejected() {
    const std::string wire = FrameCodec::Encode(FrameType::Request, 1, "");
    FrameCodec codec;
    codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Error);
    assert(error == ErrorCode::InvalidJson);
    assert(reason == "syntax");
}

void TestCrc32cKnownVector() {
    const std::uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(eat::protocol::Crc32c(data, sizeof(data)) == 0xE3069283u);
}

}  // namespace

int main() {
    TestRoundTrip();
    TestStickyFrames();
    TestOneByteAtATime();
    TestOversizeRejectedBeforeBody();
    TestBadMagic();
    TestChecksumBitFlip();
    TestZeroBodyRequestRejected();
    TestCrc32cKnownVector();
    return 0;
}