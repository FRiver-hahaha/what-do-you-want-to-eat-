#include "shared/protocol/JsonValue.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace eat::protocol {
namespace {

constexpr std::int64_t kMaxSafeInteger = 9007199254740991LL;
constexpr int kMaxDepth = 16;

bool IsValidUtf8(const std::string& text) {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return false;
    }
    const auto* data = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char b = data[i];
        if (b < 0x80) {
            ++i;
            continue;
        }
        if (b == 0xEF && i + 2 < text.size() && data[i + 1] == 0xBF &&
            (data[i + 2] == 0xBE || data[i + 2] == 0xBF)) {
            return false;
        }
        std::size_t extra = 0;
        if (b >= 0xC2 && b <= 0xDF) {
            extra = 1;
        } else if (b == 0xE0) {
            if (i + 1 >= text.size() || data[i + 1] < 0xA0 || data[i + 1] > 0xBF) return false;
            extra = 2;
        } else if (b >= 0xE1 && b <= 0xEC) {
            extra = 2;
        } else if (b == 0xED) {
            if (i + 1 >= text.size() || data[i + 1] < 0x80 || data[i + 1] > 0x9F) return false;
            extra = 2;
        } else if (b >= 0xEE && b <= 0xEF) {
            extra = 2;
        } else if (b == 0xF0) {
            if (i + 1 >= text.size() || data[i + 1] < 0x90 || data[i + 1] > 0xBF) return false;
            extra = 3;
        } else if (b >= 0xF1 && b <= 0xF3) {
            extra = 3;
        } else if (b == 0xF4) {
            if (i + 1 >= text.size() || data[i + 1] < 0x80 || data[i + 1] > 0x8F) return false;
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= text.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if (data[i + k] < 0x80 || data[i + k] > 0xBF) return false;
        }
        i += extra + 1;
    }
    return true;
}

void AppendUtf8(std::string& out, std::uint32_t code) {
    if (code <= 0x7F) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

class Parser {
 public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& out) {
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        if (pos_ != text_.size()) {
            reason_ = "trailing_data";
            return false;
        }
        return true;
    }

    const std::string& reason() const { return reason_; }

 private:
    void SkipWs() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool Fail(const char* reason) {
        reason_ = reason;
        return false;
    }

    bool ParseValue(JsonValue& out) {
        if (pos_ >= text_.size()) return Fail("syntax");
        const char c = text_[pos_];
        if (c == '{') return ParseObject(out);
        if (c == '[') return ParseArray(out);
        if (c == '"') {
            std::string value;
            if (!ParseString(value)) return false;
            out = JsonValue::String(std::move(value));
            return true;
        }
        if (c == 't') return ParseLiteral("true", JsonValue::Bool(true), out);
        if (c == 'f') return ParseLiteral("false", JsonValue::Bool(false), out);
        if (c == 'n') return ParseLiteral("null", JsonValue(), out);
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return Fail("syntax");
    }

    bool ParseLiteral(const char* literal, JsonValue value, JsonValue& out) {
        const std::size_t len = std::strlen(literal);
        if (text_.compare(pos_, len, literal) != 0) return Fail("syntax");
        pos_ += len;
        out = std::move(value);
        return true;
    }

    bool ParseObject(JsonValue& out) {
        if (++depth_ > kMaxDepth) return Fail("depth");
        ++pos_;
        out = JsonValue::Object();
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            --depth_;
            return true;
        }
        while (pos_ < text_.size()) {
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("syntax");
            std::string key;
            if (!ParseString(key)) return false;
            if (out.Find(key) != nullptr) return Fail("duplicate_key");
            SkipWs();
            if (pos_ >= text_.size() || text_[pos_] != ':') return Fail("syntax");
            ++pos_;
            SkipWs();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.Set(key, std::move(value));
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                --depth_;
                return true;
            }
            return Fail("syntax");
        }
        return Fail("syntax");
    }

    bool ParseArray(JsonValue& out) {
        if (++depth_ > kMaxDepth) return Fail("depth");
        ++pos_;
        out = JsonValue::Array();
        SkipWs();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            --depth_;
            return true;
        }
        while (pos_ < text_.size()) {
            SkipWs();
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.AsArray().push_back(std::move(value));
            SkipWs();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                --depth_;
                return true;
            }
            return Fail("syntax");
        }
        return Fail("syntax");
    }

    static int HexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool ParseHex4(std::uint32_t& value) {
        if (pos_ + 4 > text_.size()) return Fail("syntax");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = HexValue(text_[pos_ + i]);
            if (digit < 0) return Fail("syntax");
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        pos_ += 4;
        return true;
    }

    bool ParseString(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return Fail("syntax");
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c < 0x20) return Fail("syntax");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                ++pos_;
                continue;
            }
            ++pos_;
            if (pos_ >= text_.size()) return Fail("syntax");
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t code = 0;
                    if (!ParseHex4(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (pos_ + 6 > text_.size() || text_[pos_] != '\\' ||
                            text_[pos_ + 1] != 'u') {
                            return Fail("encoding");
                        }
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (!ParseHex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return Fail("encoding");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        return Fail("encoding");
                    }
                    if (code == 0xFFFE || code == 0xFFFF || code == 0) return Fail("encoding");
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    return Fail("syntax");
            }
        }
        return Fail("syntax");
    }

    bool ParseNumber(JsonValue& out) {
        const std::size_t start = pos_;
        if (text_[pos_] == '-') ++pos_;
        if (pos_ >= text_.size()) return Fail("syntax");
        if (text_[pos_] == '0') {
            ++pos_;
            if (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                return Fail("syntax");
            }
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        } else {
            return Fail("syntax");
        }
        bool integer = true;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            integer = false;
            ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                return Fail("syntax");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            integer = false;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                return Fail("syntax");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        const std::string token = text_.substr(start, pos_ - start);
        if (integer) {
            errno = 0;
            char* end = nullptr;
            const long long value = std::strtoll(token.c_str(), &end, 10);
            if (errno == ERANGE || end == nullptr || *end != '\0') {
                out = JsonValue::String(token);
                return true;
            }
            unsigned long long magnitude = 0;
            if (value < 0) {
                magnitude = static_cast<unsigned long long>(-(value + 1)) + 1ULL;
            } else {
                magnitude = static_cast<unsigned long long>(value);
            }
            if (magnitude > static_cast<unsigned long long>(kMaxSafeInteger)) {
                out = JsonValue::String(token);
            } else {
                out = JsonValue::Int(value);
            }
            return true;
        }
        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
            return Fail("number_range");
        }
        out = JsonValue::Double(value);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    std::string reason_;
};

void DumpString(const std::string& value, std::string& out) {
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b"); break;
            case '\f': out.append("\\f"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (c < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out.append(buffer);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void DumpValue(const JsonValue& value, std::string& out) {
    switch (value.type()) {
        case JsonValue::Type::Null:
            out.append("null");
            break;
        case JsonValue::Type::Bool:
            out.append(value.AsBool() ? "true" : "false");
            break;
        case JsonValue::Type::Int:
            out.append(std::to_string(value.AsInt()));
            break;
        case JsonValue::Type::Double: {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.AsDouble(),
                                              std::chars_format::general);
            if (result.ec == std::errc()) {
                out.append(buffer, result.ptr);
            } else {
                out.append("0");
            }
            break;
        }
        case JsonValue::Type::String:
            DumpString(value.AsString(), out);
            break;
        case JsonValue::Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : value.AsArray()) {
                if (!first) out.push_back(',');
                first = false;
                DumpValue(item, out);
            }
            out.push_back(']');
            break;
        }
        case JsonValue::Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& item : value.AsObject()) {
                if (!first) out.push_back(',');
                first = false;
                DumpString(item.first, out);
                out.push_back(':');
                DumpValue(item.second, out);
            }
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

JsonValue::JsonValue() : type_(Type::Null), bool_(false), int_(0), double_(0.0) {}

JsonValue JsonValue::Bool(bool value) {
    JsonValue out;
    out.type_ = Type::Bool;
    out.bool_ = value;
    return out;
}

JsonValue JsonValue::Int(std::int64_t value) {
    JsonValue out;
    out.type_ = Type::Int;
    out.int_ = value;
    return out;
}

JsonValue JsonValue::Double(double value) {
    JsonValue out;
    out.type_ = Type::Double;
    out.double_ = value;
    return out;
}

JsonValue JsonValue::String(std::string value) {
    JsonValue out;
    out.type_ = Type::String;
    out.string_ = std::move(value);
    return out;
}

JsonValue JsonValue::Array() {
    JsonValue out;
    out.type_ = Type::Array;
    return out;
}

JsonValue JsonValue::Object() {
    JsonValue out;
    out.type_ = Type::Object;
    return out;
}

JsonValue JsonValue::Parse(const std::string& text, std::string& reason) {
    if (!IsValidUtf8(text)) {
        reason = "encoding";
        return JsonValue();
    }
    Parser parser(text);
    JsonValue out;
    if (!parser.Parse(out)) {
        reason = parser.reason();
        return JsonValue();
    }
    reason.clear();
    return out;
}

JsonValue::Type JsonValue::type() const { return type_; }
bool JsonValue::IsNull() const { return type_ == Type::Null; }
bool JsonValue::IsBool() const { return type_ == Type::Bool; }
bool JsonValue::IsInt() const { return type_ == Type::Int; }
bool JsonValue::IsDouble() const { return type_ == Type::Double; }
bool JsonValue::IsString() const { return type_ == Type::String; }
bool JsonValue::IsArray() const { return type_ == Type::Array; }
bool JsonValue::IsObject() const { return type_ == Type::Object; }

bool JsonValue::AsBool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

std::int64_t JsonValue::AsInt(std::int64_t fallback) const {
    return type_ == Type::Int ? int_ : fallback;
}

double JsonValue::AsDouble(double fallback) const {
    if (type_ == Type::Double) return double_;
    if (type_ == Type::Int) return static_cast<double>(int_);
    return fallback;
}

const std::string& JsonValue::AsString() const {
    static const std::string empty;
    return type_ == Type::String ? string_ : empty;
}

const std::vector<JsonValue>& JsonValue::AsArray() const {
    static const std::vector<JsonValue> empty;
    return type_ == Type::Array ? array_ : empty;
}

std::vector<JsonValue>& JsonValue::AsArray() {
    return array_;
}

const std::vector<std::pair<std::string, JsonValue>>& JsonValue::AsObject() const {
    static const std::vector<std::pair<std::string, JsonValue>> empty;
    return type_ == Type::Object ? object_ : empty;
}

void JsonValue::Set(const std::string& key, JsonValue value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& item : object_) {
        if (item.first == key) {
            item.second = std::move(value);
            return;
        }
    }
    object_.emplace_back(key, std::move(value));
}

bool JsonValue::Erase(const std::string& key) {
    if (type_ != Type::Object) return false;
    for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it->first == key) {
            object_.erase(it);
            return true;
        }
    }
    return false;
}

const JsonValue* JsonValue::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& item : object_) {
        if (item.first == key) return &item.second;
    }
    return nullptr;
}

std::string JsonValue::Dump() const {
    std::string out;
    DumpValue(*this, out);
    return out;
}

}  // namespace eat::protocol