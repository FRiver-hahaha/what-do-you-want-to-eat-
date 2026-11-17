#pragma once

#include <cstdint>
#include <string>

namespace eat::cache {

class CacheKeyBuilder {
 public:
    CacheKeyBuilder(std::string env, std::uint64_t gen);

    std::string Profile(const std::string& user_key) const;
    std::string Meal(const std::string& user_key, const std::string& date_from,
                     const std::string& date_to, const std::string& cursor) const;
    std::string Gap(const std::string& user_key, const std::string& date,
                    std::uint64_t prof_version) const;
    std::string Week(const std::string& user_key, const std::string& iso_week,
                     std::uint64_t prof_version) const;
    std::string Menu(const std::string& meal_type, const std::string& price_group,
                     std::uint32_t page, std::uint32_t limit) const;
    std::string Search(const std::string& meal_type, const std::string& nature,
                       const std::string& tags_hash, std::uint32_t limit,
                       std::uint64_t prof_version) const;
    std::string Friend(const std::string& user_key) const;
    std::string Idem(const std::string& user_key, const std::string& idem_key) const;
    std::string OffQueue(const std::string& user_key) const;

    static std::string HashText(const std::string& text);
    static std::string NormalizeSegment(const std::string& value);

 private:
    std::string Prefix(const std::string& domain) const;

    std::string env_;
    std::uint64_t gen_;
};

}  // namespace eat::cache