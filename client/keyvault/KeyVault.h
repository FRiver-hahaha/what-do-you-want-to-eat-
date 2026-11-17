#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eat::client {

struct KeyInfo {
    bool configured = false;
    std::string source;
    std::string fingerprint;
};

class KeyVault {
 public:
    explicit KeyVault(std::string path = DefaultPath());

    bool Store(const std::string& key, const std::string& source, std::string& error);
    bool Load(std::string& key, std::string& source, std::string& error) const;
    bool Revoke(std::string& error);
    bool Rotate(const std::string& key, const std::string& source, std::string& error);
    KeyInfo Describe() const;

    static std::string DefaultPath();
    static std::string Fingerprint(const std::string& key);

 private:
    bool ReadBlob(std::vector<std::uint8_t>& blob, std::string& error) const;
    bool WriteBlob(const std::vector<std::uint8_t>& blob, std::string& error) const;

    std::string path_;
};

}  // namespace eat::client