#include <cassert>
#include <iostream>
#include <string>

#include "client/keyvault/KeyVault.h"

int main() {
    const std::string path = "build/test_keyvault_platform.dat";
    std::remove(path.c_str());

    eat::client::KeyVault vault(path);
    std::string error;
    assert(!vault.Describe().configured);
    assert(!vault.Store("sk-test-123", "self", error));
    assert(error == "unsupported_platform");
    assert(!vault.Describe().configured);
    assert(eat::client::KeyVault::Fingerprint("sk-test-123") ==
           eat::client::KeyVault::Fingerprint("sk-test-123"));
    assert(eat::client::KeyVault::Fingerprint("sk-test-123") !=
           eat::client::KeyVault::Fingerprint("sk-test-456"));

    std::cout << "keyvault-platform-ok\n";
    return 0;
}