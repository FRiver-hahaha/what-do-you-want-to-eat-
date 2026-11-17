#include "server/cache/CacheKeyBuilder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include <openssl/evp.h>

namespace eat::cache {
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

}  // namespace

CacheKeyBuilder::CacheKeyBuilder(std::string env, std::uint64_t gen)
    : env_(std::move(env)), gen_(gen) {}

std::string CacheKeyBuilder::NormalizeSegment(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "-";
    const auto end = value.find_last_not_of(" \t\r\n");
    std::string out = value.substr(begin, end - begin + 1);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string CacheKeyBuilder::HashText(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int length = 0;
    EVP_Digest(text.data(), text.size(), digest, &length, EVP_sha256(), nullptr);
    const std::string hex = Hex(digest, length);
    return hex.substr(0, 16);
}

std::string CacheKeyBuilder::Prefix(const std::string& domain) const {
    return "eat:" + env_ + ":" + std::to_string(gen_) + ":" + domain + ":";
}

std::string CacheKeyBuilder::Profile(const std::string& user_key) const {
    return Prefix("prof") + user_key;
}

std::string CacheKeyBuilder::Meal(const std::string& user_key, const std::string& date_from,
                                  const std::string& date_to,
                                  const std::string& cursor) const {
    return Prefix("meal") + user_key + ":" + NormalizeSegment(date_from) + ":" +
           NormalizeSegment(date_to) + ":" + NormalizeSegment(cursor);
}

std::string CacheKeyBuilder::Gap(const std::string& user_key, const std::string& date,
                                 std::uint64_t prof_version) const {
    return Prefix("gap") + user_key + ":" + NormalizeSegment(date) + ":" +
           std::to_string(prof_version);
}

std::string CacheKeyBuilder::Week(const std::string& user_key, const std::string& iso_week,
                                 std::uint64_t prof_version) const {
    return Prefix("week") + user_key + ":" + NormalizeSegment(iso_week) + ":" +
           std::to_string(prof_version);
}

std::string CacheKeyBuilder::Menu(const std::string& meal_type, const std::string& price_group,
                                  std::uint32_t page, std::uint32_t limit) const {
    const std::uint32_t safe_page = page < 1 ? 1 : page;
    const std::uint32_t safe_limit = limit < 1 ? 1 : (limit > 100 ? 100 : limit);
    return Prefix("menu") + NormalizeSegment(meal_type) + ":" +
           NormalizeSegment(price_group) + ":" + std::to_string(safe_page) + ":" +
           std::to_string(safe_limit);
}

std::string CacheKeyBuilder::Search(const std::string& meal_type, const std::string& nature,
                                    const std::string& tags_hash, std::uint32_t limit,
                                    std::uint64_t prof_version) const {
    const std::uint32_t safe_limit = limit < 1 ? 1 : (limit > 20 ? 20 : limit);
    return Prefix("search") + NormalizeSegment(meal_type) + ":" +
           NormalizeSegment(nature) + ":" + NormalizeSegment(tags_hash) + ":" +
           std::to_string(safe_limit) + ":" + std::to_string(prof_version);
}

std::string CacheKeyBuilder::Friend(const std::string& user_key) const {
    return Prefix("friend") + user_key;
}

std::string CacheKeyBuilder::Idem(const std::string& user_key,
                                  const std::string& idem_key) const {
    return Prefix("idem") + user_key + ":" + NormalizeSegment(idem_key);
}

std::string CacheKeyBuilder::OffQueue(const std::string& user_key) const {
    return Prefix("offq") + user_key;
}

}  // namespace eat::cache