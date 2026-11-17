#include <cassert>
#include <string>

#include "server/tcp/FrameDispatcher.h"

using eat::protocol::ErrorCode;
using eat::protocol::Frame;
using eat::protocol::FrameCodec;
using eat::protocol::FrameType;
using eat::protocol::JsonValue;
using eat::tcp::ConnContext;
using eat::tcp::ConnState;
using eat::tcp::FrameDispatcher;

namespace {

Frame MakeFrame(FrameType type, std::uint64_t request_id, const std::string& body) {
    const std::string wire = FrameCodec::Encode(type, request_id, body);
    FrameCodec codec;
    codec.Append(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size());
    Frame frame;
    ErrorCode error = ErrorCode::Ok;
    std::string reason;
    assert(codec.Next(frame, error, reason) == FrameCodec::Result::Ok);
    return frame;
}

void TestHandshakeRequired() {
    FrameDispatcher dispatcher;
    ConnContext context;
    const auto outcome = dispatcher.Handle(
        context, MakeFrame(FrameType::Request, 1, "{\"method\":\"profile.get\",\"params\":{}}"));
    assert(outcome.error == ErrorCode::HandshakeRequired);
    assert(outcome.close_after);
}

void TestHandshakeSuccess() {
    FrameDispatcher dispatcher;
    dispatcher.SetHandshakeHandler([](ConnContext& context, const JsonValue&,
                                      JsonValue& response, ErrorCode&, std::string&) {
        context.user_key = "user-1";
        response.Set("ok", JsonValue::Bool(true));
        response.Set("user_key", JsonValue::String("user-1"));
        return true;
    });
    ConnContext context;
    const auto outcome = dispatcher.Handle(
        context, MakeFrame(FrameType::HandshakeReq, 1, "{\"access_token\":\"t\"}"));
    assert(outcome.ok);
    assert(outcome.response_type == FrameType::HandshakeResp);
    assert(context.state == ConnState::Authenticated);
    assert(context.user_key == "user-1");
}

void TestUnknownMethod() {
    FrameDispatcher dispatcher;
    ConnContext context;
    context.state = ConnState::Authenticated;
    const auto outcome = dispatcher.Handle(
        context, MakeFrame(FrameType::Request, 2, "{\"method\":\"nope\",\"params\":{}}"));
    assert(outcome.error == ErrorCode::InvalidParams);
    assert(outcome.message == "unknown_method");
}

void TestWriteRequiresIdemKey() {
    FrameDispatcher dispatcher;
    dispatcher.Register("meal.create", [](ConnContext&, const JsonValue&, JsonValue&,
                                          ErrorCode&, std::string&) { return true; });
    ConnContext context;
    context.state = ConnState::Authenticated;
    const auto outcome = dispatcher.Handle(
        context, MakeFrame(FrameType::Request, 3, "{\"method\":\"meal.create\",\"params\":{}}"));
    assert(outcome.error == ErrorCode::InvalidParams);
    assert(outcome.message == "missing_idem_key");
}

void TestIdentitySpoofIgnored() {
    FrameDispatcher dispatcher;
    std::string seen_user;
    dispatcher.Register("profile.get", [&seen_user](ConnContext& context, const JsonValue& params,
                                                    JsonValue&, ErrorCode&, std::string&) {
        const JsonValue* spoof = params.Find("user_key");
        seen_user = spoof == nullptr ? context.user_key : spoof->AsString();
        return true;
    });
    ConnContext context;
    context.state = ConnState::Authenticated;
    context.user_key = "real-user";
    const auto outcome = dispatcher.Handle(
        context, MakeFrame(FrameType::Request, 4,
                           "{\"method\":\"profile.get\",\"params\":{\"user_key\":\"other\"}}"));
    assert(outcome.ok);
    assert(seen_user == "real-user");
    assert(context.identity_spoof_attempt == 1);
    assert(context.user_key == "real-user");
}

void TestHeartbeat() {
    FrameDispatcher dispatcher;
    ConnContext context;
    context.state = ConnState::Authenticated;
    const auto outcome = dispatcher.Handle(context, MakeFrame(FrameType::Heartbeat, 5, ""));
    assert(outcome.ok);
    assert(outcome.response_type == FrameType::HeartbeatAck);
}

}  // namespace

int main() {
    TestHandshakeRequired();
    TestHandshakeSuccess();
    TestUnknownMethod();
    TestWriteRequiresIdemKey();
    TestIdentitySpoofIgnored();
    TestHeartbeat();
    return 0;
}