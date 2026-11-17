#include <cassert>
#include <string>

#include "server/identity/PasswordHasher.h"
#include "server/identity/SessionRegistry.h"

namespace {

void TestPasswordHash() {
    const std::string digest = eat::identity::PasswordHasher::Hash("correct horse");
    assert(!digest.empty());
    assert(digest.rfind("scrypt$", 0) == 0);
    assert(eat::identity::PasswordHasher::Verify("correct horse", digest));
    assert(!eat::identity::PasswordHasher::Verify("wrong", digest));
    assert(!eat::identity::PasswordHasher::NeedsRehash(digest));
    eat::identity::ScryptParams stronger;
    stronger.n = 65536;
    assert(eat::identity::PasswordHasher::NeedsRehash(digest, stronger));
}

void TestPasswordHashInvalidFormat() {
    assert(!eat::identity::PasswordHasher::Verify("x", "sha256$deadbeef"));
    assert(!eat::identity::PasswordHasher::Verify("x", ""));
}

void TestSessionRegistry() {
    eat::identity::SessionRegistry registry(3);
    std::uint64_t replaced = 0;
    assert(registry.Bind("u1", 10, "device-a", replaced));
    assert(replaced == 0);
    assert(registry.IsOnline("u1"));
    assert(registry.ActiveDevices("u1") == 1);

    assert(registry.Bind("u1", 11, "device-a", replaced));
    assert(replaced == 10);
    assert(registry.ActiveDevices("u1") == 1);

    assert(registry.Bind("u1", 12, "device-b", replaced));
    assert(registry.Bind("u1", 13, "device-c", replaced));
    assert(!registry.Bind("u1", 14, "device-d", replaced));

    assert(registry.Unbind("u1", 11));
    assert(registry.ActiveDevices("u1") == 2);

    assert(registry.NextEventSeq("u1") == 1);
    assert(registry.NextEventSeq("u1") == 2);
}

void TestSessionRegistryUnbind() {
    eat::identity::SessionRegistry registry(3);
    std::uint64_t replaced = 0;
    assert(registry.Bind("u2", 20, "d", replaced));
    assert(!registry.Unbind("u2", 99));
    assert(registry.Unbind("u2", 20));
    assert(!registry.IsOnline("u2"));
    assert(registry.OnlineUsers() == 0);
}

}  // namespace

int main() {
    TestPasswordHash();
    TestPasswordHashInvalidFormat();
    TestSessionRegistry();
    TestSessionRegistryUnbind();
    return 0;
}