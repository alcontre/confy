#pragma once

#include <string>
#include <unordered_map>

namespace confy {

struct ServerCredentials {
    std::string username;
    std::string password;
};

class AuthCredentials final {
public:
    bool LoadFromM2SettingsXml(const std::string& filePath, std::string& errorMessage);
    bool TryGetForHost(const std::string& hostPort, ServerCredentials& out) const;

private:
    std::unordered_map<std::string, ServerCredentials> credentialsByHostPort_;
};

}  // namespace confy
