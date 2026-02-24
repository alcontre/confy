#pragma once

#include "AuthCredentials.h"

#include <string>
#include <vector>

namespace confy {

class BitbucketClient final {
public:
    struct RepoCoordinates {
        std::string baseUrl;
        std::string hostPort;
        std::string projectKey;
        std::string repositorySlug;
    };

    explicit BitbucketClient(AuthCredentials credentials);

    bool ListBranches(const std::string& repositoryUrl,
                      std::vector<std::string>& outBranches,
                      std::string& errorMessage) const;

    bool ListTopLevelXmlFiles(const std::string& repositoryUrl,
                              const std::string& branch,
                              std::vector<std::string>& outFiles,
                              std::string& errorMessage) const;

    bool DownloadFile(const std::string& repositoryUrl,
                      const std::string& branch,
                      const std::string& filePath,
                      const std::string& outputPath,
                      std::string& errorMessage) const;

    static bool ParseRepositoryUrl(const std::string& repositoryUrl,
                                   RepoCoordinates& out,
                                   std::string& errorMessage);

private:
    bool GetCredentialsForRepo(const RepoCoordinates& repo,
                               ServerCredentials& outCredentials,
                               std::string& errorMessage) const;

    bool HttpGetText(const std::string& url,
                     const ServerCredentials& creds,
                     std::string& outBody,
                     std::string& errorMessage) const;

    bool HttpDownloadBinary(const std::string& url,
                            const ServerCredentials& creds,
                            const std::string& outFile,
                            std::string& errorMessage) const;

    static std::string BuildCurlUserPwd(const ServerCredentials& creds);
    static std::string UrlEncode(const std::string& value);
    static std::string EncodePath(const std::string& path);
    static std::string EncodeUrlForCurl(const std::string& rawUrl);
    static bool IsXmlTopLevelPath(const std::string& path);

    AuthCredentials credentials_;
};

}  // namespace confy
