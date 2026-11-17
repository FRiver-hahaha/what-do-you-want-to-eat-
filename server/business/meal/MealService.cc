#include "server/business/meal/MealService.h"

#include <openssl/sha.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "server/business/common/BusinessJson.h"
#include "server/business/common/DateUtil.h"
#include "server/business/common/Utf8Util.h"
#include "server/business/meal/IdempotencyDao.h"
#include "server/business/user/UserDao.h"
#include "server/cache/CacheClient.h"
#include "server/cache/InvalidationSet.h"
#include "server/database/DbConn.h"

namespace eat::business {
namespace {

constexpr std::size_t kMaxIdemKeyBytes = 64;
constexpr std::size_t kMaxTypeChars = 8;
constexpr std::size_t kMaxTextChars = 512;
constexpr std::size_t kMaxItemChars = 60;
constexpr std::size_t kMaxItems = 50;
constexpr std::size_t kMaxItemsBytes = 1024;
constexpr double kMaxNutrition = 10000.0;
constexpr int kMaxDays = 90;

bool ReadNumber(const protocol::JsonValue* value, double& out) {
    if (value == nullptr) return false;
    if (value->IsInt()) {
        out = static_cast<double>(value->AsInt());
        return true;
    }
    if (value->IsDouble()) {
        out = value->AsDouble();
        return std::isfinite(out);
    }
    if (value->IsString()) {
        const std::string text = value->AsString();
        if (text.empty()) {
            out = 0;
            return true;
        }
        char* end = nullptr;
        out = std::strtod(text.c_str(), &end);
        return end != nullptr && *end == '\0' && std::isfinite(out);
    }
    return false;
}

bool ReadId(const protocol::JsonValue* value, unsigned long long& out) {
    if (value == nullptr) return false;
    if (value->IsInt() && value->AsInt() > 0) {
        out = static_cast<unsigned long long>(value->AsInt());
        return true;
    }
    if (value->IsString()) {
        const std::string text = value->AsString();
        if (text.empty()) return false;
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
        if (end == nullptr || *end != '\0' || parsed == 0) return false;
        out = parsed;
        return true;
    }
    return false;
}

bool ReadVersion(const protocol::JsonValue* value, unsigned int& out) {
    if (value == nullptr) return false;
    if (value->IsInt() && value->AsInt() > 0) {
        out = static_cast<unsigned int>(value->AsInt());
        return true;
    }
    return false;
}

int PenaltyFor(const std::string& text) {
    int penalty = 0;
    if (text.find("奶茶") != std::string::npos || text.find("可乐") != std::string::npos ||
        text.find("汽水") != std::string::npos || text.find("甜饮") != std::string::npos) {
        penalty += 4;
    }
    if (text.find("炸") != std::string::npos || text.find("薯条") != std::string::npos ||
        text.find("烧烤") != std::string::npos || text.find("肥肉") != std::string::npos) {
        penalty += 3;
    }
    return penalty;
}

bool ValidIdemKey(const std::string& key) {
    if (key.empty() || key.size() > kMaxIdemKeyBytes) return false;
    for (const char c : key) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z');
        if (!alnum && c != '_' && c != '-' && c != '.' && c != ':') return false;
    }
    return true;
}

std::string IdemKeyOf(const protocol::JsonValue& params) {
    const protocol::JsonValue* value = params.Find("idem_key");
    if (value == nullptr || !value->IsString()) value = params.Find("idempotency_key");
    if (value == nullptr || !value->IsString()) return std::string();
    return value->AsString();
}

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(hex, SHA256_DIGEST_LENGTH * 2);
}

std::string CanonicalPayload(const std::string& date, const std::string& text,
                             const std::string& type) {
    protocol::JsonValue canonical = protocol::JsonValue::Object();
    canonical.Set("date", protocol::JsonValue::String(date));
    canonical.Set("text", protocol::JsonValue::String(text));
    canonical.Set("type", protocol::JsonValue::String(type));
    return canonical.Dump();
}

protocol::JsonValue MealJson(const MealRow& row, unsigned long long id,
                             unsigned int version) {
    protocol::JsonValue meal = protocol::JsonValue::Object();
    meal.Set("id", protocol::JsonValue::Int(static_cast<std::int64_t>(id)));
    meal.Set("date", protocol::JsonValue::String(row.date));
    meal.Set("type", protocol::JsonValue::String(row.type));
    meal.Set("time", protocol::JsonValue::String(row.time));
    meal.Set("text", protocol::JsonValue::String(row.text));
    meal.Set("items", ParseStringArray(row.items));
    meal.Set("protein", JsonNumber(row.protein));
    meal.Set("fiber", JsonNumber(row.fiber));
    meal.Set("veggies", JsonNumber(row.veggies));
    meal.Set("water", JsonNumber(row.water));
    meal.Set("penalty", protocol::JsonValue::Int(row.penalty));
    meal.Set("version", protocol::JsonValue::Int(static_cast<std::int64_t>(version)));
    return meal;
}

protocol::JsonValue CreateResult(const std::string& user_key, const MealRow& row,
                                 unsigned long long id, unsigned int version,
                                 bool idempotent) {
    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("receipt_id", protocol::JsonValue::String("meal-" + std::to_string(id)));
    data.Set("meal", MealJson(row, id, version));
    data.Set("idempotent", protocol::JsonValue::Bool(idempotent));
    return data;
}

bool ApplyItem(const protocol::JsonValue* value, std::string& items_text) {
    if (value == nullptr) return true;
    if (!value->IsArray()) return false;
    protocol::JsonValue items = protocol::JsonValue::Array();
    for (const auto& item : value->AsArray()) {
        if (!item.IsString()) return false;
        if (items.AsArray().size() >= kMaxItems) break;
        items.AsArray().push_back(
            protocol::JsonValue::String(utf8::Truncate(item.AsString(), kMaxItemChars)));
        if (items.Dump().size() > kMaxItemsBytes) {
            items.AsArray().pop_back();
            break;
        }
    }
    items_text = items.Dump();
    return true;
}

BusinessResult ApplyFields(const protocol::JsonValue& params, MealRow& row,
                           bool require_date) {
    const protocol::JsonValue* value = nullptr;
    std::string text;

    bool has_date = false;
    if ((value = params.Find("date")) != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("date must be a YYYY-MM-DD string");
        text = value->AsString();
        if (!text.empty()) {
            row.date = text;
            has_date = true;
            if (!dateutil::ValidDate(row.date)) {
                return errors::InvalidParams("date must be a valid YYYY-MM-DD within 2000-2100");
            }
        }
    }
    if (!has_date && require_date) row.date = dateutil::Today();

    if ((value = params.Find("type")) != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("type must be a string");
        row.type = utf8::Truncate(value->AsString(), kMaxTypeChars);
        if (row.type.empty()) row.type = "加餐";
    }

    if ((value = params.Find("time")) != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("time must be a string");
        text = value->AsString();
        if (!text.empty()) {
            if (!dateutil::ValidClock(text)) {
                return errors::InvalidParams("time must be HH:MM");
            }
            row.time = text;
        }
    }

    if ((value = params.Find("text")) != nullptr && !value->IsNull()) {
        if (!value->IsString()) return errors::InvalidParams("text must be a string");
        row.text = utf8::Truncate(value->AsString(), kMaxTextChars);
    }

    if ((value = params.Find("items")) != nullptr && !value->IsNull()) {
        if (!ApplyItem(value, row.items)) {
            return errors::InvalidParams("items must be an array of strings");
        }
    }

    if (row.text.empty() && row.items != "[]") {
        std::string joined;
        const protocol::JsonValue items = ParseStringArray(row.items);
        std::size_t index = 0;
        for (const auto& item : items.AsArray()) {
            if (index++ != 0) joined += "、";
            joined += item.AsString();
        }
        row.text = utf8::Truncate(joined, kMaxTextChars);
    }

    struct Field {
        const char* key;
        double* target;
    };
    const Field fields[] = {{"protein", &row.protein},
                            {"fiber", &row.fiber},
                            {"veggies", &row.veggies},
                            {"water", &row.water}};
    for (const auto& field : fields) {
        value = params.Find(field.key);
        if (value == nullptr || value->IsNull()) continue;
        double number = 0;
        if (!ReadNumber(value, number) || number < 0 || number > kMaxNutrition) {
            return errors::InvalidParams(std::string(field.key) +
                                         " must be a number in [0, 10000]");
        }
        *field.target = number;
    }
    return BusinessResult::Success(protocol::JsonValue());
}

void InvalidateStatsCache(cache::CacheClient* cache, const std::string& user_key,
                          const std::vector<std::string>& dates) {
    if (cache == nullptr) return;
    std::vector<std::string> valid_dates;
    std::vector<std::string> weeks;
    for (const auto& date : dates) {
        if (!dateutil::ValidDate(date)) continue;
        valid_dates.push_back(date);
        weeks.push_back(dateutil::WeekStart(date));
    }
    const std::string today = dateutil::Today();
    if (std::find(valid_dates.begin(), valid_dates.end(), today) == valid_dates.end()) {
        valid_dates.push_back(today);
    }
    weeks.push_back(dateutil::WeekStart(today));
    cache->Delete(cache::MealKeys(cache->keys(), user_key, valid_dates, weeks,
                                  cache->ProfileVersion(user_key)));
}

}  // namespace

BusinessResult MealService::Create(const std::string& user_key,
                                   const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    if (!params.IsObject()) return errors::InvalidParams("params must be an object");

    const std::string idem_key = IdemKeyOf(params);
    if (!ValidIdemKey(idem_key)) {
        return errors::InvalidParams(
            "idem_key must be 1-64 chars of [A-Za-z0-9_.:-]");
    }

    MealRow row;
    row.time = dateutil::ClockNow();
    BusinessResult applied = ApplyFields(params, row, true);
    if (!applied.ok) return applied;
    row.penalty = PenaltyFor(row.text);
    const std::string payload_hash =
        Sha256Hex(CanonicalPayload(row.date, row.text, row.type));

    UserDao users(pool_);
    const UserDao::Lookup user_lookup = users.FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    db::DbConn conn(pool_);
    if (!conn) return errors::Unavailable();

    IdempotencyRecord existing;
    const IdempotencyDao::Lookup pre =
        IdempotencyDao::FindOn(conn.get(), user_key, idem_key, existing);
    if (pre == IdempotencyDao::Lookup::Unavailable) return errors::Unavailable();
    if (pre == IdempotencyDao::Lookup::Found) {
        if (existing.payload_hash != payload_hash) {
            return errors::Conflict("idem_key already used with a different payload");
        }
        MealRow replay;
        const MealDao::Lookup load =
            MealDao::LoadByIdOn(conn.get(), user_key, existing.meal_id, replay);
        if (load == MealDao::Lookup::Unavailable) return errors::Unavailable();
        if (load == MealDao::Lookup::NotFound) {
            return errors::Unavailable();
        }
        return BusinessResult::Success(
            CreateResult(user_key, replay, existing.meal_id, replay.version, true));
    }

    db::DbTransaction tx(conn);
    if (!tx.active()) return errors::Unavailable();

    const unsigned long long meal_id = MealDao::InsertOn(conn.get(), user_key, row);
    if (meal_id == 0) {
        tx.rollback();
        return errors::Unavailable();
    }

    const IdempotencyDao::Insert insert =
        IdempotencyDao::InsertOn(conn.get(), user_key, idem_key, payload_hash, meal_id);
    if (insert == IdempotencyDao::Insert::Ok) {
        if (!tx.commit()) return errors::Unavailable();
        InvalidateStatsCache(cache_, user_key, {row.date});
        return BusinessResult::Success(CreateResult(user_key, row, meal_id, 1, false));
    }

    tx.rollback();
    if (insert == IdempotencyDao::Insert::Unavailable) return errors::Unavailable();

    IdempotencyRecord winner;
    const IdempotencyDao::Lookup winner_lookup =
        IdempotencyDao::FindOn(conn.get(), user_key, idem_key, winner);
    if (winner_lookup != IdempotencyDao::Lookup::Found) return errors::Unavailable();
    if (winner.payload_hash != payload_hash) {
        return errors::Conflict("idem_key already used with a different payload");
    }
    MealRow replay;
    const MealDao::Lookup replay_lookup =
        MealDao::LoadByIdOn(conn.get(), user_key, winner.meal_id, replay);
    if (replay_lookup != MealDao::Lookup::Found) return errors::Unavailable();
    return BusinessResult::Success(
        CreateResult(user_key, replay, winner.meal_id, replay.version, true));
}

BusinessResult MealService::Recent(const std::string& user_key,
                                   const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    int days = 7;
    const protocol::JsonValue* value = params.Find("days");
    if (value != nullptr) {
        if (!value->IsInt()) return errors::InvalidParams("days must be an integer");
        days = static_cast<int>(value->AsInt());
    }
    if (days < 1 || days > kMaxDays) {
        return errors::InvalidParams("days must be an integer in [1, 90]");
    }

    UserDao users(pool_);
    const UserDao::Lookup user_lookup = users.FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    const std::string today = dateutil::Today();
    const std::string from_date = dateutil::AddDays(today, -(days - 1));

    MealDao meals(pool_);
    std::vector<MealRow> rows;
    if (meals.ListRange(user_key, from_date, today, rows) != MealDao::Lookup::Found) {
        return errors::Unavailable();
    }

    protocol::JsonValue list = protocol::JsonValue::Array();
    for (const auto& row : rows) list.AsArray().push_back(MealJson(row, row.id, row.version));

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("days", protocol::JsonValue::Int(days));
    data.Set("meals", list);
    data.Set("count", protocol::JsonValue::Int(static_cast<std::int64_t>(rows.size())));
    return BusinessResult::Success(data);
}

BusinessResult MealService::Patch(const std::string& user_key,
                                   const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    unsigned long long id = 0;
    if (!ReadId(params.Find("id"), id)) {
        return errors::InvalidParams("id is required");
    }
    unsigned int expected = 0;
    const bool has_version = ReadVersion(params.Find("version"), expected);
    const std::string idem_key = IdemKeyOf(params);
    if (!ValidIdemKey(idem_key)) {
        return errors::InvalidParams(
            "idem_key must be 1-64 chars of [A-Za-z0-9_.:-]");
    }

    UserDao users(pool_);
    const UserDao::Lookup user_lookup = users.FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    db::DbConn conn(pool_);
    if (!conn) return errors::Unavailable();

    MealRow current;
    const MealDao::Lookup loaded =
        MealDao::LoadByIdOn(conn.get(), user_key, id, current);
    if (loaded == MealDao::Lookup::Unavailable) return errors::Unavailable();
    if (loaded == MealDao::Lookup::NotFound) return errors::NotFound("meal not found");

    const std::string old_date = current.date;
    BusinessResult applied = ApplyFields(params, current, false);
    if (!applied.ok) return applied;
    current.penalty = PenaltyFor(current.text);
    const std::string payload_hash =
        Sha256Hex("patch:" + std::to_string(id) + ":" +
                  CanonicalPayload(current.date, current.text, current.type));

    IdempotencyRecord existing;
    const IdempotencyDao::Lookup pre =
        IdempotencyDao::FindOn(conn.get(), user_key, idem_key, existing);
    if (pre == IdempotencyDao::Lookup::Unavailable) return errors::Unavailable();
    if (pre == IdempotencyDao::Lookup::Found) {
        if (existing.payload_hash != payload_hash) {
            return errors::Conflict("idem_key already used with a different payload");
        }
        MealRow replay;
        if (MealDao::LoadByIdOn(conn.get(), user_key, id, replay) != MealDao::Lookup::Found) {
            return errors::NotFound("meal not found");
        }
        protocol::JsonValue data = protocol::JsonValue::Object();
        data.Set("user_id", protocol::JsonValue::String(user_key));
        data.Set("meal", MealJson(replay, id, replay.version));
        data.Set("idempotent", protocol::JsonValue::Bool(true));
        return BusinessResult::Success(data);
    }

    db::DbTransaction tx(conn);
    if (!tx.active()) return errors::Unavailable();

    const unsigned int used_version = has_version ? expected : current.version;
    const MealDao::UpdateResult updated =
        MealDao::UpdateOn(conn.get(), user_key, id, used_version, current);
    if (updated != MealDao::UpdateResult::Ok) {
        tx.rollback();
        if (updated == MealDao::UpdateResult::NotFound) return errors::NotFound("meal not found");
        if (updated == MealDao::UpdateResult::VersionConflict) return errors::VersionConflict();
        return errors::Unavailable();
    }

    const IdempotencyDao::Insert insert =
        IdempotencyDao::InsertOn(conn.get(), user_key, idem_key, payload_hash, id);
    if (insert != IdempotencyDao::Insert::Ok) {
        tx.rollback();
        if (insert == IdempotencyDao::Insert::Duplicate) {
            return errors::Conflict("idem_key already used with a different payload");
        }
        return errors::Unavailable();
    }
    if (!tx.commit()) return errors::Unavailable();

    InvalidateStatsCache(cache_, user_key, {old_date, current.date});

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("meal", MealJson(current, id, used_version + 1));
    data.Set("idempotent", protocol::JsonValue::Bool(false));
    return BusinessResult::Success(data);
}

BusinessResult MealService::Delete(const std::string& user_key,
                                   const protocol::JsonValue& params) {
    if (user_key.empty()) return errors::UserNotFound();
    unsigned long long id = 0;
    if (!ReadId(params.Find("id"), id)) {
        return errors::InvalidParams("id is required");
    }
    unsigned int expected = 0;
    const bool has_version = ReadVersion(params.Find("version"), expected);
    const std::string idem_key = IdemKeyOf(params);
    if (!ValidIdemKey(idem_key)) {
        return errors::InvalidParams(
            "idem_key must be 1-64 chars of [A-Za-z0-9_.:-]");
    }

    UserDao users(pool_);
    const UserDao::Lookup user_lookup = users.FindByUserKey(user_key);
    if (user_lookup == UserDao::Lookup::Unavailable) return errors::Unavailable();
    if (user_lookup == UserDao::Lookup::NotFound) return errors::UserNotFound();

    db::DbConn conn(pool_);
    if (!conn) return errors::Unavailable();

    const std::string payload_hash =
        Sha256Hex("delete:" + std::to_string(id) + ":" + (has_version ? std::to_string(expected) : "*"));

    IdempotencyRecord existing;
    const IdempotencyDao::Lookup pre =
        IdempotencyDao::FindOn(conn.get(), user_key, idem_key, existing);
    if (pre == IdempotencyDao::Lookup::Unavailable) return errors::Unavailable();
    if (pre == IdempotencyDao::Lookup::Found) {
        if (existing.payload_hash != payload_hash) {
            return errors::Conflict("idem_key already used with a different payload");
        }
        protocol::JsonValue data = protocol::JsonValue::Object();
        data.Set("user_id", protocol::JsonValue::String(user_key));
        data.Set("id", protocol::JsonValue::Int(static_cast<std::int64_t>(id)));
        data.Set("deleted", protocol::JsonValue::Bool(true));
        data.Set("idempotent", protocol::JsonValue::Bool(true));
        return BusinessResult::Success(data);
    }

    MealRow current;
    const MealDao::Lookup loaded =
        MealDao::LoadByIdOn(conn.get(), user_key, id, current);
    if (loaded == MealDao::Lookup::Unavailable) return errors::Unavailable();
    if (loaded == MealDao::Lookup::NotFound) return errors::NotFound("meal not found");

    db::DbTransaction tx(conn);
    if (!tx.active()) return errors::Unavailable();

    const unsigned int used_version = has_version ? expected : current.version;
    const MealDao::UpdateResult deleted =
        MealDao::SoftDeleteOn(conn.get(), user_key, id, used_version);
    if (deleted != MealDao::UpdateResult::Ok) {
        tx.rollback();
        if (deleted == MealDao::UpdateResult::NotFound) return errors::NotFound("meal not found");
        if (deleted == MealDao::UpdateResult::VersionConflict) return errors::VersionConflict();
        return errors::Unavailable();
    }

    const IdempotencyDao::Insert insert =
        IdempotencyDao::InsertOn(conn.get(), user_key, idem_key, payload_hash, id);
    if (insert != IdempotencyDao::Insert::Ok) {
        tx.rollback();
        if (insert == IdempotencyDao::Insert::Duplicate) {
            return errors::Conflict("idem_key already used with a different payload");
        }
        return errors::Unavailable();
    }
    if (!tx.commit()) return errors::Unavailable();

    InvalidateStatsCache(cache_, user_key, {current.date});

    protocol::JsonValue data = protocol::JsonValue::Object();
    data.Set("user_id", protocol::JsonValue::String(user_key));
    data.Set("id", protocol::JsonValue::Int(static_cast<std::int64_t>(id)));
    data.Set("deleted", protocol::JsonValue::Bool(true));
    data.Set("idempotent", protocol::JsonValue::Bool(false));
    return BusinessResult::Success(data);
}

}  // namespace eat::business
