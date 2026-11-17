#include "server/cache/TtlPolicy.h"

#include <cstddef>

namespace eat::cache {
namespace {

const TtlRule kRules[] = {
    {"prof", 60, 15, 0.10, 0.20},
    {"meal", 30, 15, 0.10, 0.20},
    {"gap", 30, 15, 0.10, 0.20},
    {"week", 60, 15, 0.10, 0.20},
    {"menu", 300, 60, 0.10, 0.20},
    {"search", 120, 30, 0.10, 0.20},
    {"friend", 60, 15, 0.10, 0.20},
    {"idem", 60, 0, 0.10, 0.0},
};

}  // namespace

const TtlRule* TtlFor(const std::string& domain) {
    for (const auto& rule : kRules) {
        if (domain == rule.domain) return &rule;
    }
    return nullptr;
}

bool IsCacheable(const std::string& domain) {
    if (domain == "ranking") return false;
    if (domain == "session" || domain == "online" || domain == "auth_failure") return false;
    return TtlFor(domain) != nullptr;
}

std::uint32_t ApplyJitter(std::uint32_t base_s, double jitter, std::uint64_t seed) {
    if (base_s == 0 || jitter <= 0.0) return base_s;
    const std::uint64_t span = static_cast<std::uint64_t>(static_cast<double>(base_s) * jitter * 2.0);
    if (span == 0) return base_s;
    const std::int64_t offset = static_cast<std::int64_t>(seed % (span + 1)) -
                                static_cast<std::int64_t>(span / 2);
    const std::int64_t value = static_cast<std::int64_t>(base_s) + offset;
    return value < 1 ? 1 : static_cast<std::uint32_t>(value);
}

}  // namespace eat::cache