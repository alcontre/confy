#define private public
#include "NexusClient.h"
#undef private

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
    confy::NexusClient client(confy::AuthCredentials{});

    const confy::ServerCredentials creds{"svc-user", "P@ss word/with:symbols"};
    const auto userPwd = client.BuildCurlUserPwd(creds);

    if (!Check(userPwd == "svc-user:P@ss word/with:symbols",
               "Expected CURLOPT_USERPWD payload with raw username/password")) {
        return 1;
    }

    std::cout << "[nexus-client-auth-test] OK\n";
    return 0;
}
