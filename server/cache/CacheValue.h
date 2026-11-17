#pragma once

#include <cstdint>
#include <string>

#include "shared/protocol/JsonValue.h"

namespace eat::cache {

struct CacheValue {
    int version = 1;
    std::uint64_t gen = 0;
    bool empty = false;
    std::int64_t ts_ms = 0;
    protocol::JsonValue data;

    std::string Serialize() const;
    static bool Parse(const std::string& text, CacheValue& out, std::string& reason);
};

}  // namespace eat::cache