#include "server/identity/PasswordHasher.h"

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace eat::identity {
namespace {

std::string Base64Encode(const unsigned char* data, std::size_t len) {
    if (len == 0) return "";
    std::string out;
    out.resize(4 * ((len + 2) / 3) + 1);
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]),
                                        data, static_cast<int>(len));
    out.resize(static_cast<std::size_t>(written));
    return out;
}

bool Base64Decode(const std::string& text, std::vector<unsigned char>& out) {
    if (text.empty()) return false;
    out.resize(3 * (text.size() / 4) + 3);
    const int written = EVP_DecodeBlock(out.data(),
                                        reinterpret_cast<const unsigned char*>(text.data()),
                                        static_cast<int>(text.size()));
    if (written < 0) return false;
    std::size_t padding = 0;
    if (!text.empty() && text.back() == '=') ++padding;
    if (text.size() > 1 && text[text.size() - 2] == '=') ++padding;
    out.resize(static_cast<std::size_t>(written) - padding);
    return true;
}

std::vector<std::string> Split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == delimiter) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

bool Derive(const std::string& password, const std::vector<unsigned char>& salt,
            const ScryptParams& params, std::vector<unsigned char>& out) {
    out.resize(64);
    const int ok = EVP_PBE_scrypt(password.data(), password.size(), salt.data(), salt.size(),
                                  params.n, params.r, params.p, 128u * 1024u * 1024u,
                                  out.data(), out.size());
    return ok == 1;
}

}  // namespace

std::string PasswordHasher::Hash(const std::string& password, const ScryptParams& params) {
    std::vector<unsigned char> salt(16);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        return "";
    }
    std::vector<unsigned char> digest;
    if (!Derive(password, salt, params, digest)) {
        return "";
    }
    return "scrypt$" + std::to_string(params.n) + "$" + std::to_string(params.r) + "$" +
           std::to_string(params.p) + "$" + Base64Encode(salt.data(), salt.size()) + "$" +
           Base64Encode(digest.data(), digest.size());
}

bool PasswordHasher::Verify(const std::string& password, const std::string& digest) {
    const auto parts = Split(digest, '$');
    if (parts.size() != 6 || parts[0] != "scrypt") return false;
    ScryptParams params;
    params.n = std::strtoull(parts[1].c_str(), nullptr, 10);
    params.r = static_cast<std::uint32_t>(std::strtoul(parts[2].c_str(), nullptr, 10));
    params.p = static_cast<std::uint32_t>(std::strtoul(parts[3].c_str(), nullptr, 10));
    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!Base64Decode(parts[4], salt) || !Base64Decode(parts[5], expected)) return false;
    std::vector<unsigned char> actual;
    if (!Derive(password, salt, params, actual)) return false;
    if (actual.size() != expected.size()) return false;
    return CRYPTO_memcmp(actual.data(), expected.data(), actual.size()) == 0;
}

bool PasswordHasher::NeedsRehash(const std::string& digest, const ScryptParams& params) {
    const auto parts = Split(digest, '$');
    if (parts.size() != 6 || parts[0] != "scrypt") return true;
    const std::uint64_t n = std::strtoull(parts[1].c_str(), nullptr, 10);
    const std::uint32_t r = static_cast<std::uint32_t>(std::strtoul(parts[2].c_str(), nullptr, 10));
    const std::uint32_t p = static_cast<std::uint32_t>(std::strtoul(parts[3].c_str(), nullptr, 10));
    return n != params.n || r != params.r || p != params.p;
}

}  // namespace eat::identity