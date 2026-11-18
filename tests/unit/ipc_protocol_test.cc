#include <cassert>
#include <string>

#include "client/ipc/IpcProtocol.h"

namespace {

using eat::protocol::JsonValue;

void TestRoundTrip() {
    JsonValue message = JsonValue::Object();
    message.Set("op", JsonValue::String("chat.stream"));
    message.Set("id", JsonValue::Int(42));
    const std::string wire = eat::ipc::Encode(message);
    assert(!wire.empty());
    std::string buffer = wire;
    JsonValue decoded;
    std::string reason;
    assert(eat::ipc::DecodeOne(buffer, decoded, reason) == eat::ipc::DecodeStatus::Ok);
    assert(buffer.empty());
    assert(decoded.Find("op")->AsString() == "chat.stream");
    assert(decoded.Find("id")->AsInt() == 42);
}

void TestByteByByteAndPipelining() {
    JsonValue first = JsonValue::Object();
    first.Set("seq", JsonValue::Int(1));
    JsonValue second = JsonValue::Object();
    second.Set("seq", JsonValue::Int(2));
    const std::string wire = eat::ipc::Encode(first) + eat::ipc::Encode(second);

    std::string buffer;
    JsonValue decoded;
    std::string reason;
    int decoded_count = 0;
    for (char byte : wire) {
        buffer.push_back(byte);
        const eat::ipc::DecodeStatus status =
            eat::ipc::DecodeOne(buffer, decoded, reason);
        if (status == eat::ipc::DecodeStatus::Ok) {
            ++decoded_count;
        } else {
            assert(status == eat::ipc::DecodeStatus::NeedMore);
        }
    }
    assert(decoded_count == 2);
    assert(buffer.empty());
}

void TestRejectsOversizedAndBadJson() {
    std::string oversized(4, '\0');
    const std::uint32_t too_big = static_cast<std::uint32_t>(eat::ipc::kMaxPayloadBytes + 1);
    oversized[0] = static_cast<char>(too_big & 0xFF);
    oversized[1] = static_cast<char>((too_big >> 8) & 0xFF);
    oversized[2] = static_cast<char>((too_big >> 16) & 0xFF);
    oversized[3] = static_cast<char>((too_big >> 24) & 0xFF);
    JsonValue decoded;
    std::string reason;
    assert(eat::ipc::DecodeOne(oversized, decoded, reason) == eat::ipc::DecodeStatus::TooLarge);

    const std::string bad_payload = "{not-json}";
    std::string bad(4, '\0');
    const std::uint32_t size = static_cast<std::uint32_t>(bad_payload.size());
    bad[0] = static_cast<char>(size & 0xFF);
    bad += bad_payload;
    assert(eat::ipc::DecodeOne(bad, decoded, reason) == eat::ipc::DecodeStatus::BadJson);
}

}  // namespace

int main() {
    TestRoundTrip();
    TestByteByByteAndPipelining();
    TestRejectsOversizedAndBadJson();
    return 0;
}
