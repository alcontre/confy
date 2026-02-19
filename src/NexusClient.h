#pragma once

#include "AuthCredentials.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace confy {

struct NexusArtifactAsset {
    std::string path;
    std::string downloadUrl;
};

class NexusClient final {
public:
    using ProgressCallback = std::function<void(int percent, const std::string& message)>;

    explicit NexusClient(AuthCredentials credentials);

    bool DownloadArtifactTree(const std::string& repositoryBrowseUrl,
                              const std::string& componentName,
                              const std::string& version,
                              const std::string& buildType,
                              const std::string& targetDirectory,
                              std::atomic<bool>& cancelRequested,
                              ProgressCallback progress,
                              std::string& errorMessage) const;

private:
    struct RepoInfo {
        std::string baseUrl;
        std::string repository;
        std::string hostPort;
    };

    bool ParseRepoInfo(const std::string& inputUrl, RepoInfo& out) const;
    bool ListAssets(const RepoInfo& repo,
                    const ServerCredentials& creds,
                    const std::string& query,
                    std::vector<NexusArtifactAsset>& out,
                    std::string& errorMessage) const;
    bool HttpGetText(const std::string& urlWithAuth, std::string& out, std::string& errorMessage) const;
    bool HttpDownloadBinary(const std::string& urlWithAuth,
                            const std::string& outFile,
                            std::string& errorMessage) const;
    std::string BuildAuthUrl(const std::string& url, const ServerCredentials& creds) const;

    AuthCredentials credentials_;
};

}  // namespace confy
