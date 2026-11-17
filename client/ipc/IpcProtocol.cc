#include "client/ipc/IpcProtocol.h"

namespace eat::ipc {

std::string Encode(const protocol::JsonValue& message) {
    const std::string payload = message.Dump();
    if (payload.size() > kMaxPayloadBytes) return std::string();
    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    std::string out;
    out.reserve(kHeaderBytes + payload.size());
    out.push_back(static_cast<char>(length & 0xFF));
    out.push_back(static_cast<char>((length >> 8) & 0xFF));
    out.push_back(static_cast<char>((length >> 16) & 0xFF));
    out.push_back(static_cast<char>((length >> 24) & 0xFF));
    out += payload;
    return out;
}

DecodeStatus DecodeOne(std::string& buffer, protocol::JsonValue& message,
                       std::string& reason) {
    reason.clear();
    if (buffer.size() < kHeaderBytes) return DecodeStatus::NeedMore;
    const std::uint32_t length =
        static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[0])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[3])) << 24);
    if (length > kMaxPayloadBytes) {
        reason = "payload_too_large";
        return DecodeStatus::TooLarge;
    }
    if (buffer.size() < kHeaderBytes + length) return DecodeStatus::NeedMore;

    const std::string payload = buffer.substr(kHeaderBytes, length);
    std::string parse_reason;
    protocol::JsonValue parsed = protocol::JsonValue::Parse(payload, parse_reason);
    if (!parse_reason.empty()) {
        reason = parse_reason;
        return DecodeStatus::BadJson;
    }
    buffer.erase(0, kHeaderBytes + length);
    message = parsed;
    return DecodeStatus::Ok;
}

}  // namespace eat::ipc