#include "NexusClient.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[nexus-client-auth-test] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const confy::ServerCredentials creds{"svc-user", "P@ss word/with:symbols"};
    const auto userPwd = confy::NexusClient::BuildCurlUserPwd(creds);

    if (!Check(userPwd == "svc-user:P@ss word/with:symbols",
               "Expected CURLOPT_USERPWD payload with raw username/password")) {
        return 1;
    }

    const confy::ServerCredentials emptyPassword{"svc-user", ""};
    if (!Check(confy::NexusClient::BuildCurlUserPwd(emptyPassword) == "svc-user:",
               "Expected CURLOPT_USERPWD payload to preserve empty password")) {
        return 1;
    }

    std::cout << "[nexus-client-auth-test] OK\n";
    return 0;
}
