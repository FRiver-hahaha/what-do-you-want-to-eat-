#include "shared/protocol/Crc32c.h"

#include <array>

namespace eat::protocol {
namespace {

constexpr std::uint32_t kPolynomial = 0x82F63B78u;

const std::array<std::uint32_t, 256>& Table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> value{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ ((crc & 1u) ? kPolynomial : 0u);
            }
            value[i] = crc;
        }
        return value;
    }();
    return table;
}

std::uint32_t Update(std::uint32_t crc, const std::uint8_t* data, std::size_t len) {
    const auto& table = Table();
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

}  // namespace

std::uint32_t Crc32c(const std::uint8_t* first, std::size_t first_len,
                     const std::uint8_t* second, std::size_t second_len) {
    std::uint32_t crc = 0xFFFFFFFFu;
    crc = Update(crc, first, first_len);
    if (second_len > 0) {
        crc = Update(crc, second, second_len);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t Crc32c(const std::uint8_t* data, std::size_t len) {
    return Crc32c(data, len, nullptr, 0);
}

}  // namespace eat::protocol