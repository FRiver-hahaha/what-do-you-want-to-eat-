#include "server/identity/TokenVerifier.h"

#include <chrono>
#include <cstdio>

#include <openssl/evp.h>

namespace eat::identity {
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

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

std::string TokenVerifier::Digest(const std::string& token) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    EVP_Digest(token.data(), token.size(), digest, &length, EVP_sha256(), nullptr);
    return Hex(digest, length);
}

TokenVerifier::TokenVerifier(SessionStore* store, std::chrono::seconds cache_ttl)
    : store_(store), cache_ttl_(cache_ttl) {}

bool TokenVerifier::Verify(const std::string& token, VerifiedSession& out,
                           protocol::ErrorCode& error) {
    if (token.empty()) {
        error = protocol::ErrorCode::Unauthorized;
        return false;
    }
    const std::string hash = Digest(token);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = cache_.find(hash);
        if (it != cache_.end()) {
            if (it->second.expires > Clock::now()) {
                out = it->second.session;
                error = protocol::ErrorCode::Ok;
                return true;
            }
            cache_.erase(it);
        }
    }

    if (store_ == nullptr) {
        error = protocol::ErrorCode::Unauthorized;
        return false;
    }
    SessionRecord record;
    if (!store_->LoadBySessionHash(hash, record)) {
        error = protocol::ErrorCode::Unauthorized;
        return false;
    }
    if (record.status != 1) {
        error = protocol::ErrorCode::Unauthorized;
        return false;
    }
    if (record.access_expires_at_ms <= NowMs()) {
        error = protocol::ErrorCode::TokenExpired;
        return false;
    }
    if (record.revoke_epoch != record.account_revoke_epoch) {
        error = protocol::ErrorCode::Unauthorized;
        return false;
    }

    out.user_key = record.user_key;
    out.session_id = record.session_id;
    out.account_id = record.account_id;
    out.revoke_epoch = record.revoke_epoch;

    std::lock_guard<std::mutex> lock(mutex_);
    cache_[hash] = CacheEntry{out, Clock::now() + cache_ttl_};
    error = protocol::ErrorCode::Ok;
    return true;
}

void TokenVerifier::Invalidate(const std::string& token) {
    const std::string hash = Digest(token);
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(hash);
}

void TokenVerifier::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

}  // namespace eat::identity