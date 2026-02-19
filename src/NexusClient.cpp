#include "NexusClient.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

size_t WriteToString(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
}

size_t WriteToFile(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* out = static_cast<std::ofstream*>(userp);
    out->write(static_cast<const char*>(contents), static_cast<std::streamsize>(total));
    return total;
}

std::string UrlEncode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            constexpr char kHex[] = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string TrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string ExtractHostPort(const std::string& baseUrl) {
    auto pos = baseUrl.find("://");
    if (pos == std::string::npos) {
        return {};
    }
    auto hostStart = pos + 3;
    auto slash = baseUrl.find('/', hostStart);
    if (slash == std::string::npos) {
        return baseUrl.substr(hostStart);
    }
    return baseUrl.substr(hostStart, slash - hostStart);
}

}  // namespace

namespace confy {

NexusClient::NexusClient(AuthCredentials credentials) : credentials_(std::move(credentials)) {}

bool NexusClient::DownloadArtifactTree(const std::string& repositoryBrowseUrl,
                                       const std::string& componentName,
                                       const std::string& version,
                                       const std::string& buildType,
                                       const std::string& targetDirectory,
                                       std::atomic<bool>& cancelRequested,
                                       ProgressCallback progress,
                                       std::string& errorMessage) const {
    std::cout << "[nexus] download request repoUrl='" << repositoryBrowseUrl << "' component='"
              << componentName << "' version='" << version << "' buildType='" << buildType
              << "' target='" << targetDirectory << "'" << std::endl;

    RepoInfo repo;
    if (!ParseRepoInfo(repositoryBrowseUrl, repo)) {
        errorMessage = "Unable to parse Nexus repository URL: " + repositoryBrowseUrl;
        std::cerr << "[nexus] parse repo URL failed: " << errorMessage << std::endl;
        return false;
    }

    std::cout << "[nexus] parsed baseUrl='" << repo.baseUrl << "' repository='" << repo.repository
              << "' hostPort='" << repo.hostPort << "'" << std::endl;

    ServerCredentials creds;
    if (!credentials_.TryGetForHost(repo.hostPort, creds)) {
        errorMessage =
            "No credentials found in ~/.m2/settings.xml for host '" + repo.hostPort + "'.";
        std::cerr << "[nexus] credential lookup failed for hostPort='" << repo.hostPort << "'"
                  << std::endl;
        return false;
    }

    std::cout << "[nexus] credentials resolved for hostPort='" << repo.hostPort << "' username='"
              << creds.username << "'" << std::endl;

    std::vector<NexusArtifactAsset> assets;
    if (!ListAssets(repo, creds, assets, errorMessage)) {
        std::cerr << "[nexus] list assets failed: " << errorMessage << std::endl;
        return false;
    }

    std::cout << "[nexus] total assets returned=" << assets.size() << std::endl;

    const auto prefix = componentName + "/" + version + "/" + buildType + "/";
    struct MatchedAsset {
        NexusArtifactAsset asset;
        std::string relativePath;
    };

    auto extractRelativePath = [&prefix](const std::string& rawPath, std::string& relativePath) -> bool {
        std::string normalized = rawPath;
        while (!normalized.empty() && normalized.front() == '/') {
            normalized.erase(normalized.begin());
        }

        if (normalized.rfind(prefix, 0) == 0) {
            relativePath = normalized.substr(prefix.size());
            return true;
        }

        const std::string slashPrefix = "/" + prefix;
        auto pos = normalized.find(slashPrefix);
        if (pos != std::string::npos) {
            relativePath = normalized.substr(pos + slashPrefix.size());
            return true;
        }

        pos = normalized.find(prefix);
        if (pos != std::string::npos) {
            relativePath = normalized.substr(pos + prefix.size());
            return true;
        }

        return false;
    };

    std::vector<MatchedAsset> matches;
    for (const auto& asset : assets) {
        std::string relativePath;
        if (extractRelativePath(asset.path, relativePath)) {
            matches.push_back({asset, relativePath});
        }
    }

    std::cout << "[nexus] filtered matches prefix='" << prefix << "' count=" << matches.size()
              << std::endl;

    if (matches.empty()) {
        errorMessage = "No assets found for path prefix: " + prefix;
        for (const auto& asset : assets) {
            std::cout << "[nexus] candidate asset path='" << asset.path << "'" << std::endl;
        }
        std::cerr << "[nexus] no matching assets" << std::endl;
        return false;
    }

    fs::create_directories(targetDirectory);

    const std::size_t total = matches.size();
    std::size_t completed = 0;

    for (const auto& matched : matches) {
        if (cancelRequested.load()) {
            std::cout << "[nexus] cancel requested during downloads" << std::endl;
            return false;
        }

        const fs::path outputPath = fs::path(targetDirectory) / matched.relativePath;
        fs::create_directories(outputPath.parent_path());

        std::string downloadError;
        std::cout << "[nexus] downloading path='" << matched.asset.path << "' url='" << matched.asset.downloadUrl
                  << "'" << std::endl;
        if (!HttpDownloadBinary(BuildAuthUrl(matched.asset.downloadUrl, creds), outputPath.string(), downloadError)) {
            errorMessage = "Failed downloading '" + matched.asset.path + "': " + downloadError;
            std::cerr << "[nexus] download failed path='" << matched.asset.path << "' error='" << downloadError
                      << "'" << std::endl;
            return false;
        }

        ++completed;
        const int percent = static_cast<int>((completed * 100) / total);
        progress(percent, "Downloading " + matched.asset.path);
    }

    return true;
}

bool NexusClient::ParseRepoInfo(const std::string& inputUrl, RepoInfo& out) const {
    const auto browseMarker = std::string("#browse/browse:");
    const auto markerPos = inputUrl.find(browseMarker);
    if (markerPos != std::string::npos) {
        out.baseUrl = TrimTrailingSlash(inputUrl.substr(0, markerPos));
        out.repository = inputUrl.substr(markerPos + browseMarker.size());
        out.hostPort = ExtractHostPort(out.baseUrl);
        return !out.baseUrl.empty() && !out.repository.empty();
    }

    const auto repoMarker = std::string("/repository/");
    const auto repoPos = inputUrl.find(repoMarker);
    if (repoPos != std::string::npos) {
        out.baseUrl = TrimTrailingSlash(inputUrl.substr(0, repoPos));
        auto repoStart = repoPos + repoMarker.size();
        auto repoEnd = inputUrl.find('/', repoStart);
        out.repository = repoEnd == std::string::npos ? inputUrl.substr(repoStart)
                                                      : inputUrl.substr(repoStart, repoEnd - repoStart);
        out.hostPort = ExtractHostPort(out.baseUrl);
        return !out.baseUrl.empty() && !out.repository.empty();
    }

    return false;
}

bool NexusClient::ListAssets(const RepoInfo& repo,
                             const ServerCredentials& creds,
                             std::vector<NexusArtifactAsset>& out,
                             std::string& errorMessage) const {
    std::string continuation;

    do {
        std::string url = repo.baseUrl + "/service/rest/v1/search/assets?repository=" + UrlEncode(repo.repository);
        if (!continuation.empty()) {
            url += "&continuationToken=" + UrlEncode(continuation);
        }

        std::cout << "[nexus] list assets url='" << url << "'" << std::endl;

        std::string responseBody;
        if (!HttpGetText(BuildAuthUrl(url, creds), responseBody, errorMessage)) {
            std::cerr << "[nexus] list assets request failed: " << errorMessage << std::endl;
            return false;
        }

        std::cout << "[nexus] list assets response bytes=" << responseBody.size() << std::endl;

        json parsedJson;
        try {
            parsedJson = json::parse(responseBody);
        } catch (const std::exception& ex) {
            errorMessage = std::string("Failed to parse Nexus JSON response: ") + ex.what();
            std::cerr << "[nexus] JSON parse failed: " << errorMessage << std::endl;
            return false;
        }

        std::size_t pageAssets = 0;
        if (parsedJson.contains("items") && parsedJson["items"].is_array()) {
            for (const auto& item : parsedJson["items"]) {
                if (!item.is_object()) {
                    continue;
                }
                if (!item.contains("path") || !item["path"].is_string()) {
                    continue;
                }
                if (!item.contains("downloadUrl") || !item["downloadUrl"].is_string()) {
                    continue;
                }

                out.push_back({item["path"].get<std::string>(), item["downloadUrl"].get<std::string>()});
                ++pageAssets;
            }
        }

        std::cout << "[nexus] parsed assets this page count=" << pageAssets << std::endl;

        if (parsedJson.contains("continuationToken") && !parsedJson["continuationToken"].is_null()) {
            if (parsedJson["continuationToken"].is_string()) {
                continuation = parsedJson["continuationToken"].get<std::string>();
            } else {
                continuation.clear();
            }
        } else {
            continuation.clear();
        }
    } while (!continuation.empty());

    return true;
}

bool NexusClient::HttpGetText(const std::string& urlWithAuth,
                              std::string& out,
                              std::string& errorMessage) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMessage = "Failed to initialize curl";
        return false;
    }

    out.clear();
    curl_easy_setopt(curl, CURLOPT_URL, urlWithAuth.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        errorMessage = std::string("HTTP request failed: ") + curl_easy_strerror(result);
        std::cerr << "[nexus] http get failed error='" << errorMessage << "'" << std::endl;
        return false;
    }

    if (statusCode < 200 || statusCode >= 300) {
        errorMessage = "HTTP status " + std::to_string(statusCode);
        std::cerr << "[nexus] http get status=" << statusCode << std::endl;
        return false;
    }

    return true;
}

bool NexusClient::HttpDownloadBinary(const std::string& urlWithAuth,
                                     const std::string& outFile,
                                     std::string& errorMessage) const {
    std::ofstream output(outFile, std::ios::binary);
    if (!output) {
        errorMessage = "Unable to open local output file";
        std::cerr << "[nexus] open output file failed path='" << outFile << "'" << std::endl;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMessage = "Failed to initialize curl";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, urlWithAuth.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    const CURLcode result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        errorMessage = std::string("HTTP download failed: ") + curl_easy_strerror(result);
        std::cerr << "[nexus] http download failed path='" << outFile << "' error='" << errorMessage
                  << "'" << std::endl;
        return false;
    }

    if (statusCode < 200 || statusCode >= 300) {
        errorMessage = "HTTP status " + std::to_string(statusCode);
        std::cerr << "[nexus] http download status=" << statusCode << " path='" << outFile << "'"
                  << std::endl;
        return false;
    }

    if (!output.good()) {
        errorMessage = "Unable to write local output file";
        std::cerr << "[nexus] write output file failed path='" << outFile << "'" << std::endl;
        return false;
    }

    return true;
}

std::string NexusClient::BuildAuthUrl(const std::string& url, const ServerCredentials& creds) const {
    const auto schemePos = url.find("://");
    if (schemePos == std::string::npos) {
        return url;
    }

    const auto prefix = url.substr(0, schemePos + 3);
    const auto suffix = url.substr(schemePos + 3);
    return prefix + UrlEncode(creds.username) + ":" + UrlEncode(creds.password) + "@" + suffix;
}

}  // namespace confy
