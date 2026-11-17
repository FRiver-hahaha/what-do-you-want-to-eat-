#pragma once

#include <string>
#include <utility>

#include "shared/protocol/JsonValue.h"
#include "shared/protocol/protocol.h"

namespace eat::business {

struct BusinessResult {
    bool ok = false;
    protocol::ErrorCode code = protocol::ErrorCode::Ok;
    std::string message;
    protocol::JsonValue data;

    static BusinessResult Success(protocol::JsonValue value) {
        BusinessResult result;
        result.ok = true;
        result.data = std::move(value);
        return result;
    }

    static BusinessResult Error(protocol::ErrorCode code, std::string message) {
        BusinessResult result;
        result.code = code;
        result.message = std::move(message);
        return result;
    }
};

namespace errors {

inline BusinessResult InvalidParams(const std::string& message) {
    return BusinessResult::Error(protocol::ErrorCode::InvalidParams, message);
}

inline BusinessResult UserNotFound() {
    return BusinessResult::Error(protocol::ErrorCode::UserNotFound, "user not found");
}

inline BusinessResult Unavailable() {
    return BusinessResult::Error(protocol::ErrorCode::ServiceUnavailable,
                                 "database unavailable");
}

inline BusinessResult NotFound(const std::string& message) {
    return BusinessResult::Error(protocol::ErrorCode::NotFound, message);
}

inline BusinessResult Conflict(const std::string& message) {
    return BusinessResult::Error(protocol::ErrorCode::IdempotencyConflict, message);
}

inline BusinessResult VersionConflict() {
    return BusinessResult::Error(protocol::ErrorCode::VersionConflict,
                                 "meal version conflict");
}

}  // namespace errors

}  // namespace eat::business
