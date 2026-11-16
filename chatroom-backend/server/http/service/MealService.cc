#include "http/service/MealService.h"

#include <openssl/sha.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "database/MySQLPool.h"
#include "http/DateUtil.h"
#include "http/DbConn.h"
#include "http/JsonUtil.h"
#include "http/Utf8Util.h"
#include "http/dao/IdempotencyDao.h"
#include "http/dao/MealDao.h"
#include "http/dao/UserDao.h"

namespace {

constexpr std::size_t kMaxIdemKeyBytes = 64;
constexpr std::size_t kMaxTypeChars = 8;
constexpr std::size_t kMaxTextChars = 512;
constexpr std::size_t kMaxItemChars = 60;
constexpr std::size_t kMaxItems = 50;
constexpr std::size_t kMaxItemsBytes = 1024;
constexpr double kMaxNutrition = 10000.0;
constexpr int kMaxDays = 90;

ServiceResult makeError(ServiceResult::Status status, const std::string& message) {
    ServiceResult result;
    result.status = status;
    result.message = message;
    return result;
}

bool readNumber(const nlohmann::json& value, double& out) {
    if (value.is_number()) {
        out = value.get<double>();
        return std::isfinite(out);
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.empty()) {
            out = 0;
            return true;
        }
        try {
            std::size_t pos = 0;
            const double parsed = std::stod(text, &pos);
            if (pos == text.size() && std::isfinite(parsed)) {
                out = parsed;
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

// 与前端 analyzeMealText 的罚分规则保持一致（服务端补算，Agent 不传）。
int penaltyFor(const std::string& text) {
    int penalty = 0;
    if (text.find("奶茶") != std::string::npos ||
        text.find("可乐") != std::string::npos ||
        text.find("汽水") != std::string::npos ||
        text.find("甜饮") != std::string::npos) {
        penalty += 4;
    }
    if (text.find("炸") != std::string::npos ||
        text.find("薯条") != std::string::npos ||
        text.find("烧烤") != std::string::npos ||
        text.find("肥肉") != std::string::npos) {
        penalty += 3;
    }
    return penalty;
}

bool validIdemKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxIdemKeyBytes) {
        return false;
    }
    for (const char c : key) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z');
        if (!alnum && c != '_' && c != '-' && c != '.' && c != ':') {
            return false;
        }
    }
    return true;
}

std::string canonicalPayload(const std::string& date, const std::string& text,
                             const std::string& type) {
    // 键序 date < text < type 由 nlohmann::json 的排序 map 保证；
    // 规范串只含参与幂等语义的字段，营养估算值不参与。
    nlohmann::json canonical;
    canonical["date"] = date;
    canonical["text"] = text;
    canonical["type"] = type;
    return canonical.dump();
}

std::string sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(),
           digest);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(hex, SHA256_DIGEST_LENGTH * 2);
}

nlohmann::json mealJson(const MealRow& row) {
    nlohmann::json meal;
    meal["id"] = row.id;
    meal["date"] = row.date;
    meal["type"] = row.type;
    meal["time"] = row.time;
    meal["text"] = row.text;
    nlohmann::json items = nlohmann::json::array();
    try {
        const nlohmann::json parsed = nlohmann::json::parse(row.items);
        if (parsed.is_array()) {
            items = parsed;
        }
    } catch (...) {
    }
    meal["items"] = items;
    meal["protein"] = jsonutil::number(row.protein);
    meal["fiber"] = jsonutil::number(row.fiber);
    meal["veggies"] = jsonutil::number(row.veggies);
    meal["water"] = jsonutil::number(row.water);
    meal["penalty"] = row.penalty;
    return meal;
}

ServiceResult replayResult(const std::string& agentUid, const MealRow& row) {
    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = agentUid;
    result.data["receipt_id"] = "meal-" + std::to_string(row.id);
    result.data["meal"] = mealJson(row);
    result.data["idempotent"] = true;
    return result;
}

}  // namespace

ServiceResult MealService::recordMeal(const std::string& agentUid,
                                      const nlohmann::json& body) const {
    if (!body.is_object()) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "request body must be a JSON object");
    }

    // ── 幂等键 ──
    if (!body.contains("idempotency_key") ||
        !body["idempotency_key"].is_string()) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "idempotency_key is required");
    }
    const std::string idemKey = body["idempotency_key"].get<std::string>();
    if (!validIdemKey(idemKey)) {
        return makeError(
            ServiceResult::Status::InvalidParams,
            "idempotency_key must be 1-64 chars of [A-Za-z0-9_.:-]");
    }

    MealRow row;

    // ── 日期：缺省为本地当天，显式值必须是合法 YYYY-MM-DD ──
    if (body.contains("date") && !body["date"].is_null() &&
        !(body["date"].is_string() && body["date"].get<std::string>().empty())) {
        if (!body["date"].is_string()) {
            return makeError(ServiceResult::Status::InvalidParams,
                             "date must be a YYYY-MM-DD string");
        }
        row.date = body["date"].get<std::string>();
        if (!httpdate::validDate(row.date)) {
            return makeError(ServiceResult::Status::InvalidParams,
                             "date must be a valid YYYY-MM-DD within 2000-2100");
        }
    } else {
        row.date = httpdate::today();
    }

    // ── 餐次：缺省加餐 ──
    if (body.contains("type") && !body["type"].is_null()) {
        if (!body["type"].is_string()) {
            return makeError(ServiceResult::Status::InvalidParams,
                             "type must be a string");
        }
        row.type = utf8util::truncate(body["type"].get<std::string>(),
                                      kMaxTypeChars);
        if (row.type.empty()) {
            row.type = "加餐";
        }
    }

    // ── 食物条目：数组字符串，超长截断，整体序列化不超过列宽 ──
    nlohmann::json items = nlohmann::json::array();
    if (body.contains("items") && !body["items"].is_null()) {
        if (!body["items"].is_array()) {
            return makeError(ServiceResult::Status::InvalidParams,
                             "items must be an array of strings");
        }
        for (const auto& item : body["items"]) {
            if (!item.is_string()) {
                return makeError(ServiceResult::Status::InvalidParams,
                                 "items must be an array of strings");
            }
            if (items.size() >= kMaxItems) {
                break;
            }
            items.push_back(
                utf8util::truncate(item.get<std::string>(), kMaxItemChars));
            if (items.dump().size() > kMaxItemsBytes) {
                items.erase(items.size() - 1);
                break;
            }
        }
    }

    // ── 文本：缺省用条目拼接 ──
    if (body.contains("text") && !body["text"].is_null()) {
        if (!body["text"].is_string()) {
            return makeError(ServiceResult::Status::InvalidParams,
                             "text must be a string");
        }
        row.text = utf8util::truncate(body["text"].get<std::string>(),
                                      kMaxTextChars);
    }
    if (row.text.empty() && !items.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0) {
                joined += "、";
            }
            joined += items[i].get<std::string>();
        }
        row.text = utf8util::truncate(joined, kMaxTextChars);
    }
    row.items = items.dump();

    // ── 营养值：缺省 0，必须在 [0, 10000] ──
    struct Field {
        const char* key;
        double* target;
    };
    const Field fields[] = {{"protein", &row.protein},
                            {"fiber", &row.fiber},
                            {"veggies", &row.veggies},
                            {"water", &row.water}};
    for (const auto& field : fields) {
        if (!body.contains(field.key) || body[field.key].is_null()) {
            *field.target = 0;
            continue;
        }
        double value = 0;
        if (!readNumber(body[field.key], value) || value < 0 ||
            value > kMaxNutrition) {
            return makeError(ServiceResult::Status::InvalidParams,
                             std::string(field.key) +
                                 " must be a number in [0, 10000]");
        }
        *field.target = value;
    }

    row.time = httpdate::clockNow();
    row.penalty = penaltyFor(row.text);
    const std::string payloadHash =
        sha256Hex(canonicalPayload(row.date, row.text, row.type));

    // ── 用户存在性：字符串 uid 等值查询，绝不转 int ──
    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound, "user not found");
    }

    DbConn conn(m_pool);
    if (!conn) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    // ── 快速路径：已提交过的 key 直接重放首次结果 ──
    IdempotencyRecord existing;
    const IdempotencyDao::Lookup preLookup =
        IdempotencyDao::findOn(conn.get(), agentUid, idemKey, existing);
    if (preLookup == IdempotencyDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (preLookup == IdempotencyDao::Lookup::Found) {
        if (existing.payloadHash != payloadHash) {
            return makeError(
                ServiceResult::Status::Conflict,
                "idempotency_key already used with a different payload");
        }
        MealRow replay;
        const MealDao::Lookup loadLookup =
            MealDao::loadByIdOn(conn.get(), agentUid, existing.mealId, replay);
        if (loadLookup == MealDao::Lookup::Unavailable) {
            return makeError(ServiceResult::Status::Unavailable,
                             "database unavailable");
        }
        if (loadLookup == MealDao::Lookup::NotFound) {
            return makeError(ServiceResult::Status::Unavailable,
                             "idempotency record points to a missing meal");
        }
        return replayResult(agentUid, replay);
    }

    // ── 写入路径：meal + idempotency 同一事务 ──
    DbTransaction tx(conn);
    if (!tx.active()) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    const unsigned long long mealId =
        MealDao::insertOn(conn.get(), agentUid, row);
    if (mealId == 0) {
        tx.rollback();
        return makeError(ServiceResult::Status::Unavailable,
                         "failed to persist meal");
    }

    const IdempotencyDao::Insert insert =
        IdempotencyDao::insertOn(conn.get(), agentUid, idemKey, payloadHash,
                                 mealId);
    if (insert == IdempotencyDao::Insert::Ok) {
        if (!tx.commit()) {
            return makeError(ServiceResult::Status::Unavailable,
                             "failed to commit transaction");
        }
        ServiceResult result;
        result.status = ServiceResult::Status::Ok;
        result.data["user_id"] = agentUid;
        result.data["receipt_id"] = "meal-" + std::to_string(mealId);
        result.data["meal"] = mealJson(row);
        result.data["meal"]["id"] = mealId;
        result.data["idempotent"] = false;
        return result;
    }

    // 唯一键冲突（或死锁）：本事务整体回滚，绝不留半成品 meal。
    tx.rollback();

    if (insert == IdempotencyDao::Insert::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "failed to persist idempotency record");
    }

    // 并发裁决：由先提交的请求提供结果。
    IdempotencyRecord winner;
    const IdempotencyDao::Lookup winnerLookup =
        IdempotencyDao::findOn(conn.get(), agentUid, idemKey, winner);
    if (winnerLookup != IdempotencyDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (winner.payloadHash != payloadHash) {
        return makeError(
            ServiceResult::Status::Conflict,
            "idempotency_key already used with a different payload");
    }
    MealRow replay;
    const MealDao::Lookup replayLookup =
        MealDao::loadByIdOn(conn.get(), agentUid, winner.mealId, replay);
    if (replayLookup != MealDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "idempotency record points to a missing meal");
    }
    return replayResult(agentUid, replay);
}

ServiceResult MealService::recentMeals(const std::string& agentUid,
                                       int days) const {
    if (days < 1 || days > kMaxDays) {
        return makeError(ServiceResult::Status::InvalidParams,
                         "days must be an integer in [1, 90]");
    }

    UserDao users(m_pool);
    const UserDao::Lookup userLookup = users.findByAgentUid(agentUid);
    if (userLookup == UserDao::Lookup::Unavailable) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }
    if (userLookup == UserDao::Lookup::NotFound) {
        return makeError(ServiceResult::Status::UserNotFound, "user not found");
    }

    const std::string today = httpdate::today();
    const std::string fromDate = httpdate::addDays(today, -(days - 1));

    MealDao meals(m_pool);
    std::vector<MealRow> rows;
    const MealDao::Lookup lookup = meals.listRange(agentUid, fromDate, today, rows);
    if (lookup != MealDao::Lookup::Found) {
        return makeError(ServiceResult::Status::Unavailable,
                         "database unavailable");
    }

    nlohmann::json list = nlohmann::json::array();
    for (const auto& row : rows) {
        list.push_back(mealJson(row));
    }

    ServiceResult result;
    result.status = ServiceResult::Status::Ok;
    result.data["user_id"] = agentUid;
    result.data["days"] = days;
    result.data["meals"] = list;
    result.data["count"] = list.size();
    return result;
}