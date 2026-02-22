#include "GitClient.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[git-client-test] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    std::string host;
    if (!Check(confy::GitClient::ExtractHostPort("https://bitbucket.example.com/scm/prj/repo.git", host),
               "Expected host extraction to succeed")) {
        return 1;
    }
    if (!Check(host == "bitbucket.example.com", "Unexpected extracted host")) {
        return 1;
    }

    const std::string refsOutput =
        "111111\trefs/heads/main\n"
        "222222\trefs/heads/release/1.0\n"
        "333333\trefs/tags/v1.0.0\n"
        "444444\trefs/tags/v1.0.0^{}\n"
        "555555\trefs/tags/v2.0.0\n";

    const auto refs = confy::GitClient::ParseLsRemoteRefs(refsOutput);
    if (!Check(refs.size() == 4, "Expected de-duplicated branch/tag refs")) {
        return 1;
    }
    if (!Check(refs[0] == "main", "Expected sorted refs[0] == main")) {
        return 1;
    }
    if (!Check(refs[1] == "release/1.0", "Expected sorted refs[1] == release/1.0")) {
        return 1;
    }
    if (!Check(refs[2] == "v1.0.0", "Expected sorted refs[2] == v1.0.0")) {
        return 1;
    }
    if (!Check(refs[3] == "v2.0.0", "Expected sorted refs[3] == v2.0.0")) {
        return 1;
    }

    std::cout << "[git-client-test] OK\n";
    return 0;
}
