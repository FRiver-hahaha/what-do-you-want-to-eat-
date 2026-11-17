#pragma once

#include <cstdint>
#include <string>

namespace eat::identity {

struct ScryptParams {
    std::uint64_t n = 32768;
    std::uint32_t r = 8;
    std::uint32_t p = 1;
};

class PasswordHasher {
 public:
    static std::string Hash(const std::string& password, const ScryptParams& params = {});
    static bool Verify(const std::string& password, const std::string& digest);
    static bool NeedsRehash(const std::string& digest, const ScryptParams& params = {});
};

}  // namespace eat::identity