#include "shared/protocol/StrictJson.h"

namespace eat::protocol {

StrictJsonResult ParseStrictJson(const std::string& body) {
    StrictJsonResult result;
    std::string reason;
    JsonValue value = JsonValue::Parse(body, reason);
    if (!reason.empty()) {
        result.ok = false;
        result.reason = reason;
        return result;
    }
    result.ok = true;
    result.value = std::move(value);
    return result;
}

}  // namespace eat::protocol