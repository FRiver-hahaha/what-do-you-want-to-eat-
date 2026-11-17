#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "client/keyvault/KeyVault.h"

int main() {
    const std::string path = "build/test_keyvault.dat";
    std::remove(path.c_str());
    eat::client::KeyVault vault(path);
    std::string error;

    assert(!vault.Describe().configured);

    assert(vault.Store("sk-test-123", "self", error));
    const eat::client::KeyInfo info = vault.Describe();
    assert(info.configured);
    assert(info.source == "self");
    assert(info.fingerprint == eat::client::KeyVault::Fingerprint("sk-test-123"));

    std::string key;
    std::string source;
    assert(vault.Load(key, source, error));
    assert(key == "sk-test-123");
    assert(source == "self");

    assert(vault.Rotate("sk-test-456", "platform", error));
    assert(vault.Load(key, source, error));
    assert(key == "sk-test-456");
    assert(source == "platform");

    assert(vault.Revoke(error));
    assert(!vault.Describe().configured);
    assert(!vault.Load(key, source, error));

    std::remove(path.c_str());
    std::cout << "keyvault-ok\n";
    return 0;
}