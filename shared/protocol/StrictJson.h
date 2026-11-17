#pragma once

#include <string>

#include "shared/protocol/JsonValue.h"

namespace eat::protocol {

struct StrictJsonResult {
    bool ok = false;
    std::string reason;
    JsonValue value;
};

StrictJsonResult ParseStrictJson(const std::string& body);

}  // namespace eat::protocol