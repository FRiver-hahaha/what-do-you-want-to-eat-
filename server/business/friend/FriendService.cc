#include "server/business/friend/FriendService.h"

#include <string>
#include <vector>

#include "server/business/friend/FriendDao.h"
#include "server/business/user/UserDao.h"
#include "server/cache/CacheClient.h"
#include "server/cache/InvalidationSet.h"

namespace eat::business {
namespace {

std::string PeerOf(const protocol::JsonValue& params) {
    for (const char* key : {"peer_key", "user_key", "id", "friend_key"}) {
        const protocol::JsonValue* value = params.Find(key);
        if (value != nullptr && value->IsString() && !value->AsString().empty()) {
            return value->AsString();
        }
    }
    return std::string();
}

protocol::JsonValue RelationJson(const std::string& peer_key, const std::string& name,
                                 const char* relation) {
    protocol::JsonValue item = protocol::JsonValue::Object();
    item.Set("id", protocol::JsonValue::String(peer_key));
    item.Set("user_key", protocol::JsonValue::String(peer_key));
    item.Set("name", protocol::JsonValue::String(name.empty() ? peer_key : name));
    item.Set("relation", protocol::JsonValue::String(relation));
    return item;
}

BusinessResult CheckPeer(db::MySQLPool& pool, const std::string& user_key,
                         const std::string& peer_key) {
    if (peer_key.empty()) return errors::InvalidParams("peer_key is required");
    if (peer_key == user_key) return errors::InvalidParams("cannot add yourself");
    const UserDao::Lookup lookup = UserDao(pool).FindByUserKey(peer_key);
    if (lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (lookup == UserDao::Lookup::NotFound) {
        return errors::NotFound("peer not found");
    }
    return BusinessResult::Success(protocol::JsonValue());
}

void InvalidateFriendCache(cache::CacheClient* cache, const std::string& user_key,
                           const std::string& peer_key) {
    if (cache == nullptr) return;
    cache->Delete(cache::FriendKeys(cache->keys(), user_key, peer_key));
}

}  // namespace

BusinessResult FriendService::List(const std::string& user_key,
                                    const protocol::JsonValue&) {
    if (user_key.empty()) return errors::UserNotFound();
    const UserDao::Lookup user_lookup = UserDao(pool_).FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    if (cache_ != nullptr) {
        protocol::JsonValue cached;
        if (cache_->Read(cache_->keys().Friend(user_key), cached)) {
            return BusinessResult::Success(cached);
        }
    }

    std::vector<FriendMember> friends;
    if (FriendDao(pool_).ListFriends(user_key, friends) != FriendDao::Lookup::Found) {
        return errors::Unavailable();
    }

    protocol::JsonValue list = protocol::JsonValue::Array();
    for (const auto& member : friends) {
        list.AsArray().push_back(RelationJson(member.user_key, member.name, "friend"));
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("friends", list);
    data.Set("count", protocol::JsonValue::Int(static_cast<std::int64_t>(list.AsArray().size())));
    if (cache_ != nullptr) {
        cache_->Write(cache_->keys().Friend(user_key), data, "friend");
    }
    return BusinessResult::Success(data);
}

BusinessResult FriendService::Request(const std::string& user_key,
                                      const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    const std::string peer_key = PeerOf(params);
    BusinessResult peer = CheckPeer(pool_, user_key, peer_key);
    if (!peer.ok) return peer;

    FriendDao dao(pool_);
    FriendRelation reverse;
    const FriendDao::Lookup reverse_lookup = dao.LoadRelation(peer_key, user_key, reverse);
    if (reverse_lookup == FriendDao::Lookup::Unavailable) return errors::Unavailable();
    if (reverse.found && reverse.status == 3 && reverse.blocked_by == peer_key) {
        return BusinessResult::Error(protocol::ErrorCode::Forbidden, "blocked by peer");
    }

    const bool mutually_pending =
        reverse.found && reverse.status == 0 && reverse.blocked_by.empty();
    if (!dao.UpsertRelation(user_key, peer_key, mutually_pending ? 2 : 0, "")) {
        return errors::Unavailable();
    }
    if (mutually_pending && !dao.UpsertRelation(peer_key, user_key, 2, "")) {
        return errors::Unavailable();
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("friend", RelationJson(peer_key, "", mutually_pending ? "friend" : "pending"));
    data.Set("status", protocol::JsonValue::String(mutually_pending ? "friend" : "pending"));
    InvalidateFriendCache(cache_, user_key, peer_key);
    return BusinessResult::Success(data);
}

BusinessResult FriendService::Accept(const std::string& user_key,
                                     const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    const std::string peer_key = PeerOf(params);
    BusinessResult peer = CheckPeer(pool_, user_key, peer_key);
    if (!peer.ok) return peer;

    FriendDao dao(pool_);
    FriendRelation request;
    const FriendDao::Lookup lookup = dao.LoadRelation(peer_key, user_key, request);
    if (lookup == FriendDao::Lookup::Unavailable) return errors::Unavailable();
    if (!request.found || request.status != 0 || !request.blocked_by.empty()) {
        return errors::NotFound("friend request not found");
    }
    if (!dao.UpsertRelation(peer_key, user_key, 2, "") ||
        !dao.UpsertRelation(user_key, peer_key, 2, "")) {
        return errors::Unavailable();
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("friend", RelationJson(peer_key, "", "friend"));
    data.Set("status", protocol::JsonValue::String("friend"));
    InvalidateFriendCache(cache_, user_key, peer_key);
    return BusinessResult::Success(data);
}

BusinessResult FriendService::Remove(const std::string& user_key,
                                     const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    const std::string peer_key = PeerOf(params);
    if (peer_key.empty()) return errors::InvalidParams("peer_key is required");
    if (peer_key == user_key) return errors::InvalidParams("cannot remove yourself");

    FriendDao dao(pool_);
    if (!dao.DeleteBoth(user_key, peer_key)) return errors::Unavailable();

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("peer_key", protocol::JsonValue::String(peer_key));
    data.Set("removed", protocol::JsonValue::Bool(true));
    InvalidateFriendCache(cache_, user_key, peer_key);
    return BusinessResult::Success(data);
}

BusinessResult FriendService::Block(const std::string& user_key,
                                    const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    const std::string peer_key = PeerOf(params);
    BusinessResult peer = CheckPeer(pool_, user_key, peer_key);
    if (!peer.ok) return peer;

    FriendDao dao(pool_);
    if (!dao.UpsertRelation(user_key, peer_key, 3, user_key)) {
        return errors::Unavailable();
    }
    FriendRelation reverse;
    const FriendDao::Lookup reverse_lookup = dao.LoadRelation(peer_key, user_key, reverse);
    if (reverse_lookup == FriendDao::Lookup::Unavailable) return errors::Unavailable();
    if (reverse.found && reverse.status != 3) {
        if (!dao.DeleteRelation(peer_key, user_key)) return errors::Unavailable();
    }

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("peer_key", protocol::JsonValue::String(peer_key));
    data.Set("blocked", protocol::JsonValue::Bool(true));
    InvalidateFriendCache(cache_, user_key, peer_key);
    return BusinessResult::Success(data);
}

}  // namespace eat::business
