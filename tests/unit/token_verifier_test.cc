#include <cassert>
#include <chrono>
#include <string>
#include <unordered_map>

#include "server/identity/TokenVerifier.h"

namespace {

class FakeStore : public eat::identity::SessionStore {
 public:
    bool LoadBySessionHash(const std::string& hash, eat::identity::SessionRecord& out) override {
        const auto it = records.find(hash);
        if (it == records.end()) return false;
        out = it->second;
        return true;
    }

    std::unordered_map<std::string, eat::identity::SessionRecord> records;
};

std::int64_t FutureMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               (std::chrono::system_clock::now() + std::chrono::hours(1)).time_since_epoch())
        .count();
}

void TestValidToken() {
    FakeStore store;
    eat::identity::SessionRecord record;
    record.user_key = "u1";
    record.session_id = "s1";
    record.account_id = 1;
    record.status = 1;
    record.revoke_epoch = 2;
    record.account_revoke_epoch = 2;
    record.access_expires_at_ms = FutureMs();
    store.records[eat::identity::TokenVerifier::Digest("token-1")] = record;

    eat::identity::TokenVerifier verifier(&store);
    eat::identity::VerifiedSession session;
    eat::protocol::ErrorCode error = eat::protocol::ErrorCode::Ok;
    assert(verifier.Verify("token-1", session, error));
    assert(session.user_key == "u1");
    assert(verifier.Verify("token-1", session, error));
}

void TestRevokedEpoch() {
    FakeStore store;
    eat::identity::SessionRecord record;
    record.status = 1;
    record.revoke_epoch = 1;
    record.account_revoke_epoch = 2;
    record.access_expires_at_ms = FutureMs();
    store.records[eat::identity::TokenVerifier::Digest("token-2")] = record;
    eat::identity::TokenVerifier verifier(&store);
    eat::identity::VerifiedSession session;
    eat::protocol::ErrorCode error = eat::protocol::ErrorCode::Ok;
    assert(!verifier.Verify("token-2", session, error));
    assert(error == eat::protocol::ErrorCode::Unauthorized);
}

void TestExpired() {
    FakeStore store;
    eat::identity::SessionRecord record;
    record.status = 1;
    record.access_expires_at_ms = 1;
    store.records[eat::identity::TokenVerifier::Digest("token-3")] = record;
    eat::identity::TokenVerifier verifier(&store);
    eat::identity::VerifiedSession session;
    eat::protocol::ErrorCode error = eat::protocol::ErrorCode::Ok;
    assert(!verifier.Verify("token-3", session, error));
    assert(error == eat::protocol::ErrorCode::TokenExpired);
}

void TestInactive() {
    FakeStore store;
    eat::identity::SessionRecord record;
    record.status = 0;
    record.access_expires_at_ms = FutureMs();
    store.records[eat::identity::TokenVerifier::Digest("token-4")] = record;
    eat::identity::TokenVerifier verifier(&store);
    eat::identity::VerifiedSession session;
    eat::protocol::ErrorCode error = eat::protocol::ErrorCode::Ok;
    assert(!verifier.Verify("token-4", session, error));
}

}  // namespace

int main() {
    TestValidToken();
    TestRevokedEpoch();
    TestExpired();
    TestInactive();
    return 0;
}