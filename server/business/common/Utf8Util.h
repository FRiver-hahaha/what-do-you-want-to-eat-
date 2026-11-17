#pragma once

#include <cstddef>
#include <string>

namespace eat::business::utf8 {

inline std::string Truncate(const std::string& input, std::size_t max_chars) {
    std::size_t index = 0;
    std::size_t count = 0;
    while (index < input.size() && count < max_chars) {
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
        if (index + len > input.size()) break;
        index += len;
        ++count;
    }
    return input.substr(0, index);
}

inline std::size_t Length(const std::string& input) {
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
        if (index + len > input.size()) break;
        index += len;
        ++count;
    }
    return count;
}

}  // namespace eat::business::utf8
