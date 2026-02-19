#include "NexusClient.h"

#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

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
    RepoInfo repo;
    if (!ParseRepoInfo(repositoryBrowseUrl, repo)) {
        errorMessage = "Unable to parse Nexus repository URL: " + repositoryBrowseUrl;
        return false;
    }

    ServerCredentials creds;
    if (!credentials_.TryGetForHost(repo.hostPort, creds)) {
        errorMessage =
            "No credentials found in ~/.m2/settings.xml for host '" + repo.hostPort + "'.";
        return false;
    }

    std::vector<NexusArtifactAsset> assets;
    if (!ListAssets(repo, creds, assets, errorMessage)) {
        return false;
    }

    const auto prefix = componentName + "/" + version + "/" + buildType + "/";
    std::vector<NexusArtifactAsset> matches;
    for (const auto& asset : assets) {
        if (asset.path.rfind(prefix, 0) == 0) {
            matches.push_back(asset);
        }
    }

    if (matches.empty()) {
        errorMessage = "No assets found for path prefix: " + prefix;
        return false;
    }

    fs::create_directories(targetDirectory);

    const std::size_t total = matches.size();
    std::size_t completed = 0;

    for (const auto& asset : matches) {
        if (cancelRequested.load()) {
            return false;
        }

        const auto relative = asset.path.substr(prefix.size());
        const fs::path outputPath = fs::path(targetDirectory) / relative;
        fs::create_directories(outputPath.parent_path());

        std::string downloadError;
        if (!HttpDownloadBinary(BuildAuthUrl(asset.downloadUrl, creds), outputPath.string(), downloadError)) {
            errorMessage = "Failed downloading '" + asset.path + "': " + downloadError;
            return false;
        }

        ++completed;
        const int percent = static_cast<int>((completed * 100) / total);
        progress(percent, "Downloading " + asset.path);
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

        std::string json;
        if (!HttpGetText(BuildAuthUrl(url, creds), json, errorMessage)) {
            return false;
        }

        const std::regex objectPattern(
            R"REGEX(\{[^\{\}]*"path"\s*:\s*"([^"]+)"[^\{\}]*"downloadUrl"\s*:\s*"([^"]+)"[^\{\}]*\}|\{[^\{\}]*"downloadUrl"\s*:\s*"([^"]+)"[^\{\}]*"path"\s*:\s*"([^"]+)"[^\{\}]*\})REGEX");
        auto begin = std::sregex_iterator(json.begin(), json.end(), objectPattern);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            NexusArtifactAsset asset;
            if (!(*it)[1].str().empty()) {
                asset.path = (*it)[1].str();
                asset.downloadUrl = (*it)[2].str();
            } else {
                asset.path = (*it)[4].str();
                asset.downloadUrl = (*it)[3].str();
            }
            if (!asset.path.empty() && !asset.downloadUrl.empty()) {
                out.push_back(std::move(asset));
            }
        }

        const std::regex continuationPattern(R"REGEX("continuationToken"\s*:\s*(null|"([^"]*)"))REGEX");
        std::smatch match;
        if (std::regex_search(json, match, continuationPattern)) {
            if (match[1].str() == "null") {
                continuation.clear();
            } else {
                continuation = match[2].str();
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
        return false;
    }

    if (statusCode < 200 || statusCode >= 300) {
        errorMessage = "HTTP status " + std::to_string(statusCode);
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
        return false;
    }

    if (statusCode < 200 || statusCode >= 300) {
        errorMessage = "HTTP status " + std::to_string(statusCode);
        return false;
    }

    if (!output.good()) {
        errorMessage = "Unable to write local output file";
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
