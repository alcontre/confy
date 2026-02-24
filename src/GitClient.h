#pragma once

#include "AuthCredentials.h"

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace confy {

class GitClient final {
public:
    using ProgressCallback = std::function<void(int percent, const std::string& message)>;

    explicit GitClient(AuthCredentials credentials);

    bool ListBranchesAndTags(const std::string& repositoryUrl,
                             std::vector<std::string>& outRefs,
                             std::string& errorMessage) const;

    bool CloneRepository(const std::string& repositoryUrl,
                         const std::string& branchOrTag,
                         const std::string& targetDirectory,
                         bool shallow,
                         std::atomic<bool>& cancelRequested,
                         ProgressCallback progress,
                         std::string& errorMessage) const;

    static bool ExtractHostPort(const std::string& repositoryUrl, std::string& outHostPort);
    static std::vector<std::string> ParseLsRemoteRefs(const std::string& lsRemoteOutput);

private:
    using CommandOutputCallback = std::function<void(std::string_view)>;

    static std::string EscapeShellArg(const std::string& value);
    static bool RunCommandCapture(const std::string& command,
                                  std::string& output,
                                  std::string& errorMessage,
                                  CommandOutputCallback outputCallback = nullptr);
    static int DecodeExitCode(int rawExitCode);

    bool BuildAuthConfigArg(const std::string& repositoryUrl,
                            std::string& outConfigArg,
                            std::string& errorMessage) const;

    AuthCredentials credentials_;
};

}  // namespace confy
