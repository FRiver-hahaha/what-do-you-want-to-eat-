#include "http/service/FriendService.h"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/MySQLPool.h"
#include "http/dao/RankingDao.h"
#include "http/dao/UserDao.h"

namespace {

ServiceResult makeError(ServiceResult::Status status,
                        const std::string& message) {
    ServiceResult result;
    result.status = status;
    result.message = message;
    return result;
}

}  // namespace

ServiceResult FriendService::friends(const std::string& agentUid) const {
    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound,
                         "user not found");
    }

    RankingDao ranking(m_pool);
    std::vector<RankingMember> friends;
    if (ranking.listFriends(agentUid, friends) != RankingDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    // Agent 的 market.share() 用 friends[].id 判定好友关系，
    // 因此对外字段固定为 id（而不是 agent_uid）。
    nlohmann::json list = nlohmann::json::array();
    for (const auto& friendRow : friends) {
        nlohmann::json item;
        item["id"] = friendRow.agentUid;
        item["name"] = friendRow.name.empty() ? friendRow.agentUid
                                              : friendRow.name;
        item["relation"] = "friend";
        list.push_back(item);
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = agentUid;
    result.data["friends"] = list;
    return result;
}