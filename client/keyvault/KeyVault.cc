#include "client/keyvault/KeyVault.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include <openssl/evp.h>

#include "shared/protocol/JsonValue.h"
#include "shared/protocol/StrictJson.h"

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#include <wincrypt.h>
#endif

namespace eat::client {
namespace {

std::string Hex(const unsigned char* data, std::size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

void SecureClear(std::string& value) {
    if (!value.empty()) {
        volatile char* p = &value[0];
        for (std::size_t i = 0; i < value.size(); ++i) p[i] = 0;
        value.clear();
    }
}

std::string EncodePayload(const std::string& key, const std::string& source) {
    protocol::JsonValue root = protocol::JsonValue::Object();
    root.Set("key", protocol::JsonValue::String(key));
    root.Set("source", protocol::JsonValue::String(source));
    return root.Dump();
}

bool DecodePayload(const std::string& text, std::string& key, std::string& source) {
    const auto parsed = protocol::ParseStrictJson(text);
    if (!parsed.ok || !parsed.value.IsObject()) return false;
    const protocol::JsonValue* key_value = parsed.value.Find("key");
    const protocol::JsonValue* source_value = parsed.value.Find("source");
    if (key_value == nullptr || source_value == nullptr) return false;
    key = key_value->AsString();
    source = source_value->AsString();
    return !key.empty();
}

}  // namespace

KeyVault::KeyVault(std::string path) : path_(std::move(path)) {}

std::string KeyVault::DefaultPath() {
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && *local != '\0') {
        return std::string(local) + "\\EatApp\\secrets.dat";
    }
    return "secrets.dat";
#else
    return "secrets.dat";
#endif
}

std::string KeyVault::Fingerprint(const std::string& key) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    EVP_Digest(key.data(), key.size(), digest, &length, EVP_sha256(), nullptr);
    return Hex(digest, length).substr(0, 16);
}

bool KeyVault::Store(const std::string& key, const std::string& source, std::string& error) {
    if (key.empty()) {
        error = "empty_key";
        return false;
    }
    std::string payload = EncodePayload(key, source);
#ifdef _WIN32
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(&payload[0]);
    input.cbData = static_cast<DWORD>(payload.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"EatAppKey", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        SecureClear(payload);
        error = "dpapi_protect_failed";
        return false;
    }
    std::vector<std::uint8_t> blob(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    SecureClear(payload);
    return WriteBlob(blob, error);
#else
    SecureClear(payload);
    error = "unsupported_platform";
    return false;
#endif
}

bool KeyVault::Load(std::string& key, std::string& source, std::string& error) const {
    std::vector<std::uint8_t> blob;
    if (!ReadBlob(blob, error)) return false;
    key.clear();
    source.clear();
#ifdef _WIN32
    DATA_BLOB input;
    input.pbData = const_cast<BYTE*>(blob.data());
    input.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        error = "dpapi_unprotect_failed";
        return false;
    }
    std::string payload(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    const bool ok = DecodePayload(payload, key, source);
    SecureClear(payload);
    if (!ok) {
        error = "bad_payload";
        SecureClear(key);
        return false;
    }
    error.clear();
    return true;
#else
    error = "unsupported_platform";
    return false;
#endif
}

bool KeyVault::Revoke(std::string& error) {
    std::remove(path_.c_str());
    error.clear();
    return true;
}

bool KeyVault::Rotate(const std::string& key, const std::string& source, std::string& error) {
    return Store(key, source, error);
}

KeyInfo KeyVault::Describe() const {
    KeyInfo info;
    std::string key;
    std::string source;
    std::string error;
    if (Load(key, source, error)) {
        info.configured = true;
        info.source = source;
        info.fingerprint = Fingerprint(key);
        SecureClear(key);
    }
    return info;
}

bool KeyVault::ReadBlob(std::vector<std::uint8_t>& blob, std::string& error) const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        error = "not_configured";
        return false;
    }
    blob.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (blob.empty()) {
        error = "empty_blob";
        return false;
    }
    error.clear();
    return true;
}

bool KeyVault::WriteBlob(const std::vector<std::uint8_t>& blob, std::string& error) const {
    const auto separator = path_.find_last_of("\\/");
    if (separator != std::string::npos) {
#ifdef _WIN32
        CreateDirectoryA(path_.substr(0, separator).c_str(), nullptr);
#else
        const std::string directory = path_.substr(0, separator);
        std::string command = "mkdir -p '" + directory + "'";
        std::system(command.c_str());
#endif
    }
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "open_failed";
        return false;
    }
    output.write(reinterpret_cast<const char*>(blob.data()),
                 static_cast<std::streamsize>(blob.size()));
    if (!output) {
        error = "write_failed";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace eat::client