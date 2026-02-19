#include "AuthCredentials.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[auth-credentials-test] " << message << '\n';
        return false;
    }
    return true;
}

bool WriteFile(const fs::path& filePath, const std::string& content) {
    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        return false;
    }
    out << content;
    return out.good();
}

}  // namespace

int main() {
    const fs::path tempDir = fs::temp_directory_path() / "confy-auth-tests";
    std::error_code ec;
    fs::create_directories(tempDir, ec);

    const fs::path validPath = tempDir / "settings-valid.xml";
    const fs::path invalidPath = tempDir / "settings-invalid.xml";

    const std::string validXml = R"XML(<settings>
  <servers>
    <server>
      <id>localhost:8081</id>
      <username>aa</username>
      <password>bb</password>
    </server>
    <server>
      <id>example.org:443</id>
      <username>alice</username>
      <password>secret</password>
    </server>
  </servers>
</settings>
)XML";

    const std::string invalidXml = R"XML(<not-settings>
  <servers>
    <server>
      <id>localhost:8081</id>
      <username>aa</username>
    </server>
  </servers>
</not-settings>
)XML";

    if (!Check(WriteFile(validPath, validXml), "Failed to write valid settings fixture")) return 1;
    if (!Check(WriteFile(invalidPath, invalidXml), "Failed to write invalid settings fixture")) return 1;

    confy::AuthCredentials auth;
    std::string error;

    if (!Check(auth.LoadFromM2SettingsXml(validPath.string(), error), "Expected valid settings to load")) {
        std::cerr << "[auth-credentials-test] error: " << error << '\n';
        return 1;
    }

    confy::ServerCredentials creds;
    if (!Check(auth.TryGetForHost("localhost:8081", creds), "Expected localhost:8081 lookup to succeed")) return 1;
    if (!Check(creds.username == "aa", "Expected username aa")) return 1;
    if (!Check(creds.password == "bb", "Expected password bb")) return 1;

    if (!Check(!auth.TryGetForHost("missing-host:1234", creds), "Expected unknown host lookup to fail")) return 1;

    confy::AuthCredentials badAuth;
    std::string badError;
    if (!Check(!badAuth.LoadFromM2SettingsXml(invalidPath.string(), badError),
               "Expected invalid settings root to fail")) {
        return 1;
    }

    if (!Check(!badError.empty(), "Expected parse error message for invalid settings")) return 1;

    std::cout << "[auth-credentials-test] OK\n";
    return 0;
}
