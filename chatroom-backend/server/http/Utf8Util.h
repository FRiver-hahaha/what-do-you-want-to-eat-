#pragma once

#include <cstddef>
#include <string>

namespace utf8util {

// 按 UTF-8 字符数截断，绝不截断在多字节序列中间。
inline std::string truncate(const std::string& input, std::size_t maxChars) {
    std::size_t index = 0;
    std::size_t count = 0;
    while (index < input.size() && count < maxChars) {
        const unsigned char c = static_cast<unsigned char>(input[index]);
        std::size_t len = 1;
        if ((c & 0x80) == 0) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (index + len > input.size()) {
            break;
        }
        index += len;
        ++count;
    }
    return input.substr(0, index);
}

inline std::size_t length(const std::string& input) {
    std::size_t index = 0;
    std::size_t count = 0;
    while (index < input.size()) {
        const unsigned char c = static_cast<unsigned char>(input[index]);
        std::size_t len = 1;
        if ((c & 0x80) == 0) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (index + len > input.size()) {
            break;
        }
        index += len;
        ++count;
    }
    return count;
}

}  // namespace utf8util