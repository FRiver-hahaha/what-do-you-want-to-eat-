#include "server/business/profile/ProfileMediaService.h"

#include <cctype>
#include <string>

#include "server/business/profile/ProfileMediaDao.h"

namespace eat::business {
namespace {

// A data URL must fit in a single 64 KiB protocol frame together with the rest
// of the JSON envelope, so the wire cap is well below the column capacity.
constexpr std::size_t kMaxMediaBytes = 48 * 1024;
constexpr std::size_t kMaxUserKeyBytes = 64;

bool ValidUserKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxUserKeyBytes) return false;
    for (const char raw : key) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) != 0 || c == '_' || c == '-') continue;
        return false;
    }
    return true;
}

bool ValidMedia(const std::string& value) {
    if (value.empty()) return true;
    if (value.size() > kMaxMediaBytes) return false;
    return value.rfind("data:image/", 0) == 0;
}

protocol::JsonValue BuildMedia(const std::string& user_key, const ProfileMediaRow& row) {
    protocol::JsonValue out = protocol::JsonValue::Object();
    out.Set("user_id", protocol::JsonValue::String(user_key));
    out.Set("cover", protocol::JsonValue::String(row.cover));
    out.Set("avatar", protocol::JsonValue::String(row.avatar));
    out.Set("cover_ver", protocol::JsonValue::Int(static_cast<std::int64_t>(row.cover_ver)));
    out.Set("avatar_ver", protocol::JsonValue::Int(static_cast<std::int64_t>(row.avatar_ver)));
    out.Set("source", protocol::JsonValue::String("service"));
    return out;
}

BusinessResult Forbidden(const std::string& message) {
    return BusinessResult::Error(protocol::ErrorCode::Forbidden, message);
}

bool ReadPeerKey(const protocol::JsonValue& params, std::string& out) {
    for (const char* name : {"peer_key", "user_id"}) {
        const protocol::JsonValue* value = params.Find(name);
        if (value == nullptr || value->IsNull()) continue;
        if (!value->IsString()) return false;
        out = value->AsString();
        return true;
    }
    return false;
}

}  // namespace

BusinessResult ProfileMediaService::Get(const std::string& requester_key,
                                        const protocol::JsonValue& params) {
    if (requester_key.empty()) return errors::UserNotFound();

    std::string peer_key;
    if (ReadPeerKey(params, peer_key) && peer_key.empty()) peer_key.clear();
    if (peer_key.empty()) peer_key = requester_key;
    if (!ValidUserKey(peer_key)) return errors::InvalidParams("invalid user key");

    if (peer_key != requester_key) {
        if (!identity_.UserExists(peer_key)) return errors::UserNotFound();
        ProfileMediaDao dao(pool_);
        if (!dao.IsFriend(requester_key, peer_key)) {
            return Forbidden("peer_not_friend");
        }
    }

    ProfileMediaDao dao(pool_);
    ProfileMediaRow row;
    const ProfileMediaDao::Lookup lookup = dao.Load(peer_key, row);
    if (lookup == ProfileMediaDao::Lookup::Unavailable) return errors::Unavailable();

    protocol::JsonValue data = BuildMedia(peer_key, row);
    return BusinessResult::Success(data);
}

BusinessResult ProfileMediaService::Set(const std::string& user_key,
                                        const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    if (!params.IsObject()) return errors::InvalidParams("params must be an object");

    for (const char* name : {"user_id", "user_key"}) {
        const protocol::JsonValue* claimed = params.Find(name);
        if (claimed == nullptr || claimed->IsNull()) continue;
        if (!claimed->IsString() || claimed->AsString() != user_key) {
            return Forbidden("cross_user_write_denied");
        }
    }

    std::string cover;
    std::string avatar;
    bool has_cover = false;
    bool has_avatar = false;
    for (const char* name : {"cover", "avatar"}) {
        const protocol::JsonValue* value = params.Find(name);
        if (value == nullptr || value->IsNull()) continue;
        if (!value->IsString()) return errors::InvalidParams("media must be a string");
        if (name[0] == 'c') {
            cover = value->AsString();
            has_cover = true;
            if (!ValidMedia(cover)) return errors::InvalidParams("invalid cover");
        } else {
            avatar = value->AsString();
            has_avatar = true;
            if (!ValidMedia(avatar)) return errors::InvalidParams("invalid avatar");
        }
    }
    if (!has_cover && !has_avatar) return errors::InvalidParams("no media field");

    ProfileMediaDao dao(pool_);
    ProfileMediaRow row;
    const ProfileMediaDao::Lookup lookup = dao.Save(user_key, has_cover ? &cover : nullptr,
                                                    has_avatar ? &avatar : nullptr, row);
    if (lookup == ProfileMediaDao::Lookup::Unavailable) return errors::Unavailable();

    protocol::JsonValue data = BuildMedia(user_key, row);
    return BusinessResult::Success(data);
}

}  // namespace eat::business
