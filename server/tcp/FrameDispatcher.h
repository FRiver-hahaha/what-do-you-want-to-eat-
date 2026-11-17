#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "server/tcp/ConnContext.h"
#include "shared/protocol/FrameCodec.h"
#include "shared/protocol/JsonValue.h"

namespace eat::tcp {

struct DispatchOutcome {
    bool ok = false;
    protocol::FrameType response_type = protocol::FrameType::Response;
    protocol::ErrorCode error = protocol::ErrorCode::Ok;
    std::string message;
    std::string body;
    bool close_after = false;
};

class FrameDispatcher {
 public:
    using HandshakeHandler = std::function<bool(
        ConnContext&, const protocol::JsonValue& body, protocol::JsonValue& response,
        protocol::ErrorCode& error, std::string& message)>;
    using BusinessHandler = std::function<bool(
        ConnContext&, const protocol::JsonValue& params, protocol::JsonValue& data,
        protocol::ErrorCode& error, std::string& message)>;

    FrameDispatcher();

    void SetHandshakeHandler(HandshakeHandler handler);
    void Register(const std::string& method, BusinessHandler handler);
    bool HasMethod(const std::string& method) const;

    DispatchOutcome Handle(ConnContext& context, const protocol::Frame& frame);

 private:
    DispatchOutcome HandleHandshake(ConnContext& context, const protocol::Frame& frame);
    DispatchOutcome HandleRequest(ConnContext& context, const protocol::Frame& frame);

    HandshakeHandler handshake_handler_;
    std::unordered_map<std::string, BusinessHandler> handlers_;
};

}  // namespace eat::tcp