#include "server/tcp/FrameDispatcher.h"

#include "shared/protocol/StrictJson.h"
#include "shared/protocol/methods.h"

namespace eat::tcp {
namespace {

DispatchOutcome ErrorOutcome(protocol::ErrorCode error, const std::string& message,
                             bool close_after = false) {
    DispatchOutcome outcome;
    outcome.error = error;
    outcome.message = message;
    outcome.close_after = close_after;
    protocol::JsonValue body = protocol::JsonValue::Object();
    body.Set("code", protocol::JsonValue::Int(static_cast<std::int64_t>(error)));
    body.Set("message", protocol::JsonValue::String(message));
    body.Set("reason", protocol::JsonValue::String(protocol::ErrorName(error)));
    outcome.body = body.Dump();
    outcome.response_type = protocol::FrameType::Error;
    return outcome;
}

}  // namespace

FrameDispatcher::FrameDispatcher() = default;

void FrameDispatcher::SetHandshakeHandler(HandshakeHandler handler) {
    handshake_handler_ = std::move(handler);
}

void FrameDispatcher::Register(const std::string& method, BusinessHandler handler) {
    handlers_[method] = std::move(handler);
}

bool FrameDispatcher::HasMethod(const std::string& method) const {
    return handlers_.find(method) != handlers_.end();
}

DispatchOutcome FrameDispatcher::Handle(ConnContext& context, const protocol::Frame& frame) {
    switch (frame.header.frame_type) {
        case protocol::FrameType::HandshakeReq:
            return HandleHandshake(context, frame);
        case protocol::FrameType::Request:
            return HandleRequest(context, frame);
        case protocol::FrameType::Heartbeat: {
            DispatchOutcome outcome;
            outcome.ok = true;
            outcome.response_type = protocol::FrameType::HeartbeatAck;
            outcome.body = "{}";
            return outcome;
        }
        case protocol::FrameType::HeartbeatAck:
            return DispatchOutcome{};
        default:
            return ErrorOutcome(protocol::ErrorCode::UnknownFrameType, "unexpected_frame", true);
    }
}

DispatchOutcome FrameDispatcher::HandleHandshake(ConnContext& context,
                                                 const protocol::Frame& frame) {
    if (context.state != ConnState::Connected) {
        return ErrorOutcome(protocol::ErrorCode::BadHeader, "duplicate_handshake", true);
    }
    const auto parsed = protocol::ParseStrictJson(frame.body);
    if (!parsed.ok) {
        context.bad_frame_count += 1;
        return ErrorOutcome(protocol::ErrorCode::InvalidJson, parsed.reason, false);
    }
    if (!parsed.value.IsObject()) {
        return ErrorOutcome(protocol::ErrorCode::InvalidJson, "syntax", false);
    }
    context.state = ConnState::Handshaking;
    protocol::JsonValue response = protocol::JsonValue::Object();
    protocol::ErrorCode error = protocol::ErrorCode::Ok;
    std::string message;
    if (!handshake_handler_) {
        return ErrorOutcome(protocol::ErrorCode::InternalError, "handshake_unavailable", true);
    }
    if (!handshake_handler_(context, parsed.value, response, error, message)) {
        context.auth_fail_count += 1;
        return ErrorOutcome(error, message, context.auth_fail_count >= 3);
    }
    context.state = ConnState::Authenticated;
    DispatchOutcome outcome;
    outcome.ok = true;
    outcome.response_type = protocol::FrameType::HandshakeResp;
    outcome.body = response.Dump();
    return outcome;
}

DispatchOutcome FrameDispatcher::HandleRequest(ConnContext& context,
                                               const protocol::Frame& frame) {
    if (!context.IsAuthenticated()) {
        context.bad_frame_count += 1;
        return ErrorOutcome(protocol::ErrorCode::HandshakeRequired, "handshake_required", true);
    }
    const auto parsed = protocol::ParseStrictJson(frame.body);
    if (!parsed.ok) {
        context.bad_frame_count += 1;
        return ErrorOutcome(protocol::ErrorCode::InvalidJson, parsed.reason, false);
    }
    if (!parsed.value.IsObject()) {
        return ErrorOutcome(protocol::ErrorCode::InvalidJson, "syntax", false);
    }
    const protocol::JsonValue* method = parsed.value.Find("method");
    if (method == nullptr || !method->IsString()) {
        return ErrorOutcome(protocol::ErrorCode::InvalidParams, "missing_method", false);
    }
    const std::string name = method->AsString();
    const auto handler = handlers_.find(name);
    if (handler == handlers_.end()) {
        return ErrorOutcome(protocol::ErrorCode::InvalidParams, "unknown_method", false);
    }
    const protocol::JsonValue* params = parsed.value.Find("params");
    if (params == nullptr || !params->IsObject()) {
        return ErrorOutcome(protocol::ErrorCode::InvalidParams, "missing_params", false);
    }
    protocol::JsonValue sanitized = *params;
    if (sanitized.Find("user_key") != nullptr || sanitized.Find("user_id") != nullptr) {
        context.identity_spoof_attempt += 1;
        sanitized.Erase("user_key");
        sanitized.Erase("user_id");
    }
    if (protocol::method::IsWriteMethod(name)) {
        const protocol::JsonValue* idem = sanitized.Find("idem_key");
        if (idem == nullptr) idem = sanitized.Find("idempotency_key");
        if (idem == nullptr || !idem->IsString() || idem->AsString().empty()) {
            return ErrorOutcome(protocol::ErrorCode::InvalidParams, "missing_idem_key", false);
        }
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    protocol::ErrorCode error = protocol::ErrorCode::Ok;
    std::string message;
    if (!handler->second(context, sanitized, data, error, message)) {
        return ErrorOutcome(error, message, false);
    }
    protocol::JsonValue body = protocol::JsonValue::Object();
    body.Set("ok", protocol::JsonValue::Bool(true));
    body.Set("data", std::move(data));
    DispatchOutcome outcome;
    outcome.ok = true;
    outcome.response_type = protocol::FrameType::Response;
    outcome.body = body.Dump();
    return outcome;
}

}  // namespace eat::tcp