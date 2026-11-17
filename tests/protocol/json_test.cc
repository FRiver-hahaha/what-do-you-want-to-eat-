#include <cassert>
#include <string>

#include "shared/protocol/StrictJson.h"

using eat::protocol::JsonValue;
using eat::protocol::ParseStrictJson;

namespace {

void TestValidObject() {
    const auto result = ParseStrictJson("{\"method\":\"profile.get\",\"params\":{},\"n\":3}");
    assert(result.ok);
    assert(result.reason.empty());
    assert(result.value.IsObject());
    assert(result.value.Find("method") != nullptr);
    assert(result.value.Find("method")->AsString() == "profile.get");
    assert(result.value.Find("n")->AsInt() == 3);
}

void TestDuplicateKey() {
    const auto result = ParseStrictJson("{\"a\":1,\"a\":2}");
    assert(!result.ok);
    assert(result.reason == "duplicate_key");
}

void TestDepth() {
    std::string body;
    for (int i = 0; i < 17; ++i) body.push_back('[');
    for (int i = 0; i < 17; ++i) body.push_back(']');
    const auto result = ParseStrictJson(body);
    assert(!result.ok);
    assert(result.reason == "depth");
}

void TestTrailingData() {
    const auto result = ParseStrictJson("{} {}");
    assert(!result.ok);
    assert(result.reason == "trailing_data");
}

void TestNanRejected() {
    const auto result = ParseStrictJson("{\"n\":NaN}");
    assert(!result.ok);
    assert(result.reason == "syntax");
}

void TestBomRejected() {
    const std::string body = "\xEF\xBB\xBF{}";
    const auto result = ParseStrictJson(body);
    assert(!result.ok);
    assert(result.reason == "encoding");
}

void TestInfiniteRejected() {
    const auto result = ParseStrictJson("{\"n\":1e9999}");
    assert(!result.ok);
    assert(result.reason == "number_range");
}

void TestBigIntegerAsString() {
    const auto result = ParseStrictJson("{\"n\":9007199254740993}");
    assert(result.ok);
    assert(result.value.Find("n")->IsString());
    assert(result.value.Find("n")->AsString() == "9007199254740993");
}

void TestUnicodeEscape() {
    const auto result = ParseStrictJson("{\"s\":\"\\u4f60\\u597d\"}");
    assert(result.ok);
    assert(result.value.Find("s")->AsString() == "\xE4\xBD\xA0\xE5\xA5\xBD");
}

void TestRoundTripDump() {
    const auto result = ParseStrictJson("{\"a\":[1,true,null,\"x\"],\"b\":{\"c\":2.5}}");
    assert(result.ok);
    const std::string dumped = result.value.Dump();
    const auto again = ParseStrictJson(dumped);
    assert(again.ok);
    assert(again.value.Find("b")->Find("c")->AsDouble() == 2.5);
}

}  // namespace

int main() {
    TestValidObject();
    TestDuplicateKey();
    TestDepth();
    TestTrailingData();
    TestNanRejected();
    TestBomRejected();
    TestInfiniteRejected();
    TestBigIntegerAsString();
    TestUnicodeEscape();
    TestRoundTripDump();
    return 0;
}