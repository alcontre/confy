#include "NexusClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool ResetDirectoryWithRetries(const fs::path& targetDirectory,
                               std::string& errorMessage,
                               std::size_t maxAttempts = 3) {
    for (std::size_t attempt = 1; attempt <= maxAttempts; ++attempt) {
        std::error_code removeError;
        fs::remove_all(targetDirectory, removeError);
        if (removeError) {
            if (attempt == maxAttempts) {
                errorMessage = "Failed to clear target directory '" + targetDirectory.string() +
                               "': " + removeError.message();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        std::error_code createError;
        fs::create_directories(targetDirectory, createError);
        if (!createError) {
            return true;
        }

        if (attempt == maxAttempts) {
            errorMessage = "Failed to create target directory '" + targetDirectory.string() +
                           "': " + createError.message();
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    errorMessage = "Failed to prepare target directory '" + targetDirectory.string() + "'.";
    return false;
}

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

struct DownloadProgressContext {
    std::function<void(std::uint64_t, std::uint64_t)> callback;
};

int OnDownloadProgress(void* clientp,
                       curl_off_t dltotal,
                       curl_off_t dlnow,
                       curl_off_t /*ultotal*/,
                       curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<DownloadProgressContext*>(clientp);
    if (ctx == nullptr || !ctx->callback) {
        return 0;
    }

    const auto safeTotal = dltotal > 0 ? static_cast<std::uint64_t>(dltotal) : 0U;
    const auto safeNow = dlnow > 0 ? static_cast<std::uint64_t>(dlnow) : 0U;
    ctx->callback(safeNow, safeTotal);
    return 0;
}

std::string FormatDownloadedSize(std::uint64_t bytes) {
    constexpr std::uint64_t kKB = 1000ULL;
    constexpr std::uint64_t kMB = 1000ULL * 1000ULL;
    if (bytes < kMB) {
        auto roundedKB = static_cast<std::uint64_t>(std::llround(static_cast<double>(bytes) / kKB));
        if (bytes > 0 && roundedKB == 0) {
            roundedKB = 1;
        }
        return std::to_string(roundedKB) + " KB";
    }
    const auto roundedMB = static_cast<std::uint64_t>(std::llround(static_cast<double>(bytes) / kMB));
    return std::to_string(roundedMB) + " MB";
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

std::string EncodeUrlForCurl(const std::string& rawUrl) {
    std::string encoded;
    encoded.reserve(rawUrl.size());

    for (unsigned char ch : rawUrl) {
        if (ch == ' ') {
            encoded += "%20";
            continue;
        }

        encoded.push_back(static_cast<char>(ch));
    }

    return encoded;
}

std::string DecodePercentEncoding(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '%' && i + 2 < value.size() && std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            const auto hi = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(value[i + 1])));
            const auto lo = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(value[i + 2])));
            const auto nibble = [](unsigned char c) -> unsigned char {
                if (c >= '0' && c <= '9') {
                    return static_cast<unsigned char>(c - '0');
                }
                return static_cast<unsigned char>(10 + (c - 'A'));
            };
            decoded.push_back(static_cast<char>((nibble(hi) << 4) | nibble(lo)));
            i += 2;
            continue;
        }
        decoded.push_back(ch);
    }
    return decoded;
}

std::string EncodePath(const std::string& path) {
    std::string encoded;
    encoded.reserve(path.size());
    for (unsigned char c : path) {
        if (c == '/') {
            encoded.push_back('/');
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '-' || c == '_' || c == '.' || c == '~') {
            encoded.push_back(static_cast<char>(c));
        } else {
            constexpr char kHex[] = "0123456789ABCDEF";
            encoded.push_back('%');
            encoded.push_back(kHex[(c >> 4) & 0x0F]);
            encoded.push_back(kHex[c & 0x0F]);
        }
    }
    return encoded;
}

std::string TrimFragmentAndQuery(const std::string& href) {
    const auto queryPos = href.find('?');
    const auto fragmentPos = href.find('#');
    const auto cutPos = std::min(queryPos == std::string::npos ? href.size() : queryPos,
                                 fragmentPos == std::string::npos ? href.size() : fragmentPos);
    return href.substr(0, cutPos);
}

std::string NormalizeDirectoryPath(std::string path) {
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    return path;
}

std::string NormalizeFilePath(std::string path) {
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

bool ContainsParentTraversal(const std::string& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto segment = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment == "..") {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

std::vector<std::string> ExtractHrefValues(const std::string& html) {
    static const std::regex hrefPattern(R"(href\s*=\s*["']([^"']+)["'])", std::regex::icase);
    std::vector<std::string> hrefs;
    for (std::sregex_iterator it(html.begin(), html.end(), hrefPattern), end; it != end; ++it) {
        hrefs.push_back((*it)[1].str());
    }
    return hrefs;
}

bool ExtractPathFromHref(const std::string& href,
                         const std::string& baseUrl,
                         const std::string& repository,
                         std::string& outPath,
                         bool& isDirectory) {
    std::string value = TrimFragmentAndQuery(href);
    if (value.empty() || value == "." || value == "./" || value == ".." || value == "../") {
        return false;
    }

    const auto browsePrefix = "/service/rest/repository/browse/" + repository + "/";
    const auto repositoryPrefix = "/repository/" + repository + "/";
    const auto browseAbsolute = baseUrl + browsePrefix;
    const auto repositoryAbsolute = baseUrl + repositoryPrefix;

    if (value.rfind(browseAbsolute, 0) == 0) {
        value = value.substr(browseAbsolute.size());
        isDirectory = !value.empty() && value.back() == '/';
    } else if (value.rfind(repositoryAbsolute, 0) == 0) {
        value = value.substr(repositoryAbsolute.size());
        isDirectory = false;
    } else if (value.rfind(browsePrefix, 0) == 0) {
        value = value.substr(browsePrefix.size());
        isDirectory = !value.empty() && value.back() == '/';
    } else if (value.rfind(repositoryPrefix, 0) == 0) {
        value = value.substr(repositoryPrefix.size());
        isDirectory = false;
    } else if (value.find("://") != std::string::npos || value.rfind('/', 0) == 0) {
        return false;
    } else {
        isDirectory = !value.empty() && value.back() == '/';
    }

    value = DecodePercentEncoding(value);
    if (ContainsParentTraversal(value)) {
        return false;
    }
    outPath = isDirectory ? NormalizeDirectoryPath(value) : NormalizeFilePath(value);
    return !outPath.empty();
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

    const auto prefix = componentName + "/" + version + "/" + buildType + "/";

    std::vector<NexusArtifactAsset> assets;
    if (!ListAssets(repo, creds, prefix, assets, errorMessage)) {
        std::cerr << "[nexus] list assets failed: " << errorMessage << std::endl;
        return false;
    }

    std::cout << "[nexus] total assets returned (query='" << prefix << "')=" << assets.size()
              << std::endl;
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

    if (!ResetDirectoryWithRetries(fs::path(targetDirectory), errorMessage)) {
        std::cerr << "[nexus] target directory reset failed target='" << targetDirectory
                  << "' error='" << errorMessage << "'" << std::endl;
        return false;
    }

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
        if (!HttpDownloadBinary(
                matched.asset.downloadUrl,
                creds,
                outputPath.string(),
                [&](std::uint64_t downloadedBytes, std::uint64_t totalBytes) {
                    const double fileProgress = totalBytes > 0
                                                    ? static_cast<double>(downloadedBytes) /
                                                          static_cast<double>(totalBytes)
                                                    : 0.0;
                    const int percent = static_cast<int>(((static_cast<double>(completed) + fileProgress) * 100.0) /
                                                         static_cast<double>(total));
                    progress(percent,
                             "Downloading " + matched.asset.path + " (" +
                                 FormatDownloadedSize(downloadedBytes) + ")");
                },
                downloadError)) {
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
                             const std::string& query,
                             std::vector<NexusArtifactAsset>& out,
                             std::string& errorMessage) const {
    const std::string startDirectory = NormalizeDirectoryPath(query);
    std::vector<std::string> directories{startDirectory};
    std::unordered_set<std::string> visitedDirectories;
    std::unordered_set<std::string> seenFiles;

    while (!directories.empty()) {
        const std::string currentDirectory = directories.back();
        directories.pop_back();

        if (!visitedDirectories.insert(currentDirectory).second) {
            continue;
        }

        std::string browseUrl = repo.baseUrl + "/service/rest/repository/browse/" + UrlEncode(repo.repository) + "/";
        if (!currentDirectory.empty()) {
            browseUrl += EncodePath(currentDirectory);
        }

        std::cout << "[nexus] browse listing url='" << browseUrl << "'" << std::endl;

        std::string responseBody;
        if (!HttpGetText(browseUrl, creds, responseBody, errorMessage)) {
            std::cerr << "[nexus] browse listing request failed: " << errorMessage << std::endl;
            return false;
        }

        std::size_t discovered = 0;
        for (const auto& href : ExtractHrefValues(responseBody)) {
            std::string resolvedPath;
            bool isDirectory = false;
            if (!ExtractPathFromHref(href, repo.baseUrl, repo.repository, resolvedPath, isDirectory)) {
                continue;
            }

            if (href.find("://") == std::string::npos && href.rfind('/', 0) != 0) {
                resolvedPath = isDirectory ? NormalizeDirectoryPath(currentDirectory + resolvedPath)
                                           : NormalizeFilePath(currentDirectory + resolvedPath);
            }

            if (isDirectory) {
                directories.push_back(resolvedPath);
            } else if (seenFiles.insert(resolvedPath).second) {
                out.push_back({resolvedPath, repo.baseUrl + "/repository/" + repo.repository + "/" +
                                                 EncodePath(resolvedPath)});
                ++discovered;
            }
        }

        std::cout << "[nexus] browse listing discovered files=" << discovered << std::endl;
    }

    return true;
}

bool NexusClient::HttpGetText(const std::string& url,
                              const ServerCredentials& creds,
                              std::string& out,
                              std::string& errorMessage) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMessage = "Failed to initialize curl";
        return false;
    }

    out.clear();
    const std::string requestUrl = EncodeUrlForCurl(url);
    const std::string userPwd = BuildCurlUserPwd(creds);
    curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd.c_str());
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

bool NexusClient::HttpDownloadBinary(const std::string& url,
                                     const ServerCredentials& creds,
                                     const std::string& outFile,
                                     DownloadProgressCallback progress,
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

    const std::string requestUrl = EncodeUrlForCurl(url);
    const std::string userPwd = BuildCurlUserPwd(creds);
    DownloadProgressContext progressContext{progress};
    curl_easy_setopt(curl, CURLOPT_URL, requestUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnDownloadProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progressContext);
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

}  // namespace confy
