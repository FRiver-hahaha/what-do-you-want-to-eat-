#pragma once

#include <cstdint>
#include <string>

namespace eat::cache {

struct TtlRule {
    const char* domain;
    std::uint32_t positive_s;
    std::uint32_t negative_s;
    double positive_jitter;
    double negative_jitter;
};

const TtlRule* TtlFor(const std::string& domain);
std::uint32_t ApplyJitter(std::uint32_t base_s, double jitter, std::uint64_t seed);
bool IsCacheable(const std::string& domain);

}  // namespace eat::cache