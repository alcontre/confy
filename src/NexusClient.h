#pragma once

#include "AuthCredentials.h"

#include <atomic>
#include <cstdint>
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
    bool ListComponentVersions(const std::string& repositoryBrowseUrl,
                               const std::string& componentName,
                               std::vector<std::string>& outVersions,
                               std::string& errorMessage) const;
    bool ListBuildTypes(const std::string& repositoryBrowseUrl,
                        const std::string& componentName,
                        const std::string& version,
                        std::vector<std::string>& outBuildTypes,
                        std::string& errorMessage) const;
    static std::vector<std::string> ExtractImmediateChildDirectories(
        const std::vector<std::string>& directoryPaths,
        const std::string& parentPath);
    static std::string BuildCurlUserPwd(const ServerCredentials& creds) {
        return creds.username + ":" + creds.password;
    }

private:
    using DownloadProgressCallback = std::function<void(std::uint64_t downloadedBytes, std::uint64_t totalBytes)>;

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
    bool ListChildDirectories(const RepoInfo& repo,
                              const ServerCredentials& creds,
                              const std::string& parentPath,
                              std::vector<std::string>& out,
                              std::string& errorMessage) const;
    bool HttpGetText(const std::string& url,
                     const ServerCredentials& creds,
                     std::string& out,
                     std::string& errorMessage) const;
    bool HttpDownloadBinary(const std::string& url,
                            const ServerCredentials& creds,
                            const std::string& outFile,
                            std::atomic<bool>& cancelRequested,
                            DownloadProgressCallback progress,
                            std::string& errorMessage) const;
    AuthCredentials credentials_;
};

}  // namespace confy
