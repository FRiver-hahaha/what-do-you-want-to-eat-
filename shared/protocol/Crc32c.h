#pragma once

#include <cstddef>
#include <cstdint>

namespace eat::protocol {

std::uint32_t Crc32c(const std::uint8_t* data, std::size_t len);
std::uint32_t Crc32c(const std::uint8_t* first, std::size_t first_len,
                     const std::uint8_t* second, std::size_t second_len);

}  // namespace eat::protocol