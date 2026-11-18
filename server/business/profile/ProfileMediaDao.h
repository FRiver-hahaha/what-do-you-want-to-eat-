#pragma once

#include <cstdint>
#include <string>

#include "server/database/MySQLPool.h"

namespace eat::business {

// Per-user ranking artwork. Rows are keyed by the authenticated user_key and
// hold the client-side data URL verbatim; ver columns give the client a cheap
// cache busting token and are bumped on every write.
struct ProfileMediaRow {
    std::string cover;
    std::string avatar;
    std::uint64_t cover_ver = 0;
    std::uint64_t avatar_ver = 0;
};

class ProfileMediaDao {
 public:
    enum class Lookup { Found, NotFound, Unavailable };

    explicit ProfileMediaDao(db::MySQLPool& pool) : pool_(pool) {}

    Lookup Load(const std::string& user_key, ProfileMediaRow& out) const;

    // nullptr keeps the stored value; an empty string clears it and bumps the
    // matching version.
    Lookup Save(const std::string& user_key, const std::string* cover,
                const std::string* avatar, ProfileMediaRow& out);

    bool IsFriend(const std::string& owner_key, const std::string& peer_key) const;

 private:
    db::MySQLPool& pool_;
};

}  // namespace eat::business
