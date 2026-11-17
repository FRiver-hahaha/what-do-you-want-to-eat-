#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eat::protocol {

class JsonValue {
 public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };

    JsonValue();

    static JsonValue Bool(bool value);
    static JsonValue Int(std::int64_t value);
    static JsonValue Double(double value);
    static JsonValue String(std::string value);
    static JsonValue Array();
    static JsonValue Object();

    static JsonValue Parse(const std::string& text, std::string& reason);

    Type type() const;
    bool IsNull() const;
    bool IsBool() const;
    bool IsInt() const;
    bool IsDouble() const;
    bool IsString() const;
    bool IsArray() const;
    bool IsObject() const;

    bool AsBool(bool fallback = false) const;
    std::int64_t AsInt(std::int64_t fallback = 0) const;
    double AsDouble(double fallback = 0.0) const;
    const std::string& AsString() const;
    const std::vector<JsonValue>& AsArray() const;
    std::vector<JsonValue>& AsArray();
    const std::vector<std::pair<std::string, JsonValue>>& AsObject() const;

    void Set(const std::string& key, JsonValue value);
    bool Erase(const std::string& key);
    const JsonValue* Find(const std::string& key) const;
    std::string Dump() const;

 private:
    Type type_;
    bool bool_;
    std::int64_t int_;
    double double_;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<std::pair<std::string, JsonValue>> object_;
};

}  // namespace eat::protocol