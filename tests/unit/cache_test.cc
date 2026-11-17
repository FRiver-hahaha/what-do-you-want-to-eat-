#include <atomic>
#include <cassert>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "server/cache/CacheKeyBuilder.h"
#include "server/cache/CacheValue.h"
#include "server/cache/InvalidationSet.h"
#include "server/cache/SingleFlight.h"
#include "server/cache/TtlPolicy.h"

namespace {

void TestHash() {
    assert(eat::cache::CacheKeyBuilder::HashText("abc") == "ba7816bf8f01cfea");
}

void TestMenuKeyNormalization() {
    eat::cache::CacheKeyBuilder builder("dev", 1);
    const std::string key = builder.Menu(" Lunch ", "20-35", 0, 999);
    assert(key == "eat:dev:1:menu:lunch:20-35:1:100");
}

void TestKeys() {
    eat::cache::CacheKeyBuilder builder("prod", 7);
    assert(builder.Profile("USER1") == "eat:prod:7:prof:USER1");
    assert(builder.Friend("USER1") == "eat:prod:7:friend:USER1");
    assert(builder.Meal("USER1", "2026-11-17", "2026-11-17", "-") ==
           "eat:prod:7:meal:USER1:2026-11-17:2026-11-17:-");
}

void TestTtl() {
    const auto* prof = eat::cache::TtlFor("prof");
    assert(prof != nullptr && prof->positive_s == 60 && prof->negative_s == 15);
    assert(!eat::cache::IsCacheable("ranking"));
    assert(eat::cache::IsCacheable("meal"));
    const std::uint32_t jittered = eat::cache::ApplyJitter(60, 0.10, 3);
    assert(jittered >= 54 && jittered <= 66);
}

void TestCacheValue() {
    eat::cache::CacheValue value;
    value.gen = 7;
    value.empty = false;
    value.ts_ms = 1730000000123;
    value.data = eat::protocol::JsonValue::Object();
    value.data.Set("name", eat::protocol::JsonValue::String("me"));
    const std::string text = value.Serialize();
    eat::cache::CacheValue parsed;
    std::string reason;
    assert(eat::cache::CacheValue::Parse(text, parsed, reason));
    assert(parsed.gen == 7);
    assert(!parsed.empty);
    assert(parsed.data.Find("name")->AsString() == "me");

    eat::cache::CacheValue negative;
    negative.gen = 7;
    negative.empty = true;
    const std::string negative_text = negative.Serialize();
    eat::cache::CacheValue negative_parsed;
    assert(eat::cache::CacheValue::Parse(negative_text, negative_parsed, reason));
    assert(negative_parsed.empty);
    assert(negative_parsed.data.IsNull());
}

void TestSingleFlight() {
    eat::cache::SingleFlight flight;
    std::atomic<int> loads{0};
    std::vector<std::thread> threads;
    std::vector<std::string> results(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&flight, &loads, &results, i] {
            results[i] = flight.Do("same-key", [&loads] {
                loads.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return std::string("value");
            }, std::chrono::milliseconds(200));
        });
    }
    for (auto& thread : threads) thread.join();
    assert(loads.load() == 1);
    for (const auto& result : results) assert(result == "value");
}

void TestInvalidation() {
    eat::cache::CacheKeyBuilder builder("prod", 7);
    const auto profile = eat::cache::ProfileKeys(builder, "USER1");
    assert(profile.size() == 1);
    const auto meal = eat::cache::MealKeys(builder, "USER1", {"2026-11-17"}, {"2026-W47"}, 99);
    assert(meal.size() == 3);
    const auto friends = eat::cache::FriendKeys(builder, "A", "B");
    assert(friends.size() == 2);
}

}  // namespace

int main() {
    TestHash();
    TestMenuKeyNormalization();
    TestKeys();
    TestTtl();
    TestCacheValue();
    TestSingleFlight();
    TestInvalidation();
    return 0;
}