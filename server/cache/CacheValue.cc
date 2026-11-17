#include "server/cache/CacheValue.h"

#include "shared/protocol/StrictJson.h"

namespace eat::cache {

std::string CacheValue::Serialize() const {
    protocol::JsonValue root = protocol::JsonValue::Object();
    root.Set("v", protocol::JsonValue::Int(version));
    root.Set("gen", protocol::JsonValue::String(std::to_string(gen)));
    root.Set("empty", protocol::JsonValue::Bool(empty));
    root.Set("ts", protocol::JsonValue::String(std::to_string(ts_ms)));
    if (empty) {
        root.Set("data", protocol::JsonValue());
    } else {
        root.Set("data", data);
    }
    return root.Dump();
}

bool CacheValue::Parse(const std::string& text, CacheValue& out, std::string& reason) {
    const auto parsed = protocol::ParseStrictJson(text);
    if (!parsed.ok) {
        reason = "bad_json";
        return false;
    }
    if (!parsed.value.IsObject()) {
        reason = "bad_shape";
        return false;
    }
    const protocol::JsonValue* version = parsed.value.Find("v");
    const protocol::JsonValue* gen = parsed.value.Find("gen");
    const protocol::JsonValue* empty = parsed.value.Find("empty");
    const protocol::JsonValue* ts = parsed.value.Find("ts");
    const protocol::JsonValue* data = parsed.value.Find("data");
    if (version == nullptr || !version->IsInt() || version->AsInt() != 1) {
        reason = "version_mismatch";
        return false;
    }
    if (gen == nullptr || empty == nullptr || ts == nullptr || data == nullptr) {
        reason = "bad_shape";
        return false;
    }
    out.version = 1;
    out.gen = gen->IsString() ? std::strtoull(gen->AsString().c_str(), nullptr, 10)
                              : static_cast<std::uint64_t>(gen->AsInt());
    out.empty = empty->AsBool();
    out.ts_ms = ts->IsString() ? std::strtoll(ts->AsString().c_str(), nullptr, 10)
                               : ts->AsInt();
    out.data = *data;
    if (out.empty && !data->IsNull()) {
        reason = "empty_with_data";
        return false;
    }
    if (!out.empty && data->IsNull()) {
        reason = "missing_data";
        return false;
    }
    reason.clear();
    return true;
}

}  // namespace eat::cache