#include "BitbucketClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

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

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ParsePagedResponse(const std::string& body,
                        Json& outRoot,
                        bool& outIsLastPage,
                        int& outNextPageStart,
                        std::string& errorMessage) {
    try {
        outRoot = Json::parse(body);
    } catch (const std::exception& ex) {
        errorMessage = std::string("Bitbucket response is not valid JSON: ") + ex.what();
        return false;
    }

    if (!outRoot.is_object() || !outRoot.contains("values") || !outRoot["values"].is_array()) {
        errorMessage = "Bitbucket response is missing a values array.";
        return false;
    }

    outIsLastPage = outRoot.value("isLastPage", true);
    outNextPageStart = outRoot.value("nextPageStart", -1);
    return true;
}

}  // namespace

namespace confy {

BitbucketClient::BitbucketClient(AuthCredentials credentials)
    : credentials_(std::move(credentials)) {}

bool BitbucketClient::ListBranches(const std::string& repositoryUrl,
                                   std::vector<std::string>& outBranches,
                                   std::string& errorMessage) const {
    outBranches.clear();

    RepoCoordinates repo;
    if (!ParseRepositoryUrl(repositoryUrl, repo, errorMessage)) {
        return false;
    }

    ServerCredentials creds;
    if (!GetCredentialsForRepo(repo, creds, errorMessage)) {
        return false;
    }

    constexpr int kPageLimit = 100;
    int start = 0;
    bool isLastPage = false;
    std::set<std::string> uniqueBranches;

    while (!isLastPage) {
        const auto endpoint = repo.baseUrl + "/rest/api/1.0/projects/" + UrlEncode(repo.projectKey) +
            "/repos/" + UrlEncode(repo.repositorySlug) + "/branches?limit=" + std::to_string(kPageLimit) +
            "&start=" + std::to_string(start);

        std::string body;
        if (!HttpGetText(endpoint, creds, body, errorMessage)) {
            return false;
        }

        Json json;
        int nextPageStart = -1;
        if (!ParsePagedResponse(body, json, isLastPage, nextPageStart, errorMessage)) {
            return false;
        }

        for (const auto& item : json["values"]) {
            if (!item.is_object()) {
                continue;
            }
            const auto displayId = item.value("displayId", std::string());
            if (!displayId.empty()) {
                uniqueBranches.insert(displayId);
            }
        }

        if (isLastPage) {
            break;
        }
        if (nextPageStart < 0) {
            errorMessage = "Bitbucket response pagination is invalid for branch listing.";
            return false;
        }
        start = nextPageStart;
    }

    outBranches.assign(uniqueBranches.begin(), uniqueBranches.end());
    return true;
}

bool BitbucketClient::ListTopLevelXmlFiles(const std::string& repositoryUrl,
                                           const std::string& branch,
                                           std::vector<std::string>& outFiles,
                                           std::string& errorMessage) const {
    outFiles.clear();

    RepoCoordinates repo;
    if (!ParseRepositoryUrl(repositoryUrl, repo, errorMessage)) {
        return false;
    }

    ServerCredentials creds;
    if (!GetCredentialsForRepo(repo, creds, errorMessage)) {
        return false;
    }

    const auto branchRef = branch.empty() ? "master" : branch;
    constexpr int kPageLimit = 100;
    int start = 0;
    bool isLastPage = false;
    std::set<std::string> uniqueFiles;

    while (!isLastPage) {
        const auto endpoint = repo.baseUrl + "/rest/api/1.0/projects/" + UrlEncode(repo.projectKey) +
            "/repos/" + UrlEncode(repo.repositorySlug) + "/files?at=" + UrlEncode("refs/heads/" + branchRef) +
            "&limit=" + std::to_string(kPageLimit) + "&start=" + std::to_string(start);

        std::string body;
        if (!HttpGetText(endpoint, creds, body, errorMessage)) {
            return false;
        }

        Json json;
        int nextPageStart = -1;
        if (!ParsePagedResponse(body, json, isLastPage, nextPageStart, errorMessage)) {
            return false;
        }

        for (const auto& item : json["values"]) {
            if (!item.is_string()) {
                continue;
            }
            const auto path = item.get<std::string>();
            if (IsXmlTopLevelPath(path)) {
                uniqueFiles.insert(path);
            }
        }

        if (isLastPage) {
            break;
        }
        if (nextPageStart < 0) {
            errorMessage = "Bitbucket response pagination is invalid for file listing.";
            return false;
        }
        start = nextPageStart;
    }

    outFiles.assign(uniqueFiles.begin(), uniqueFiles.end());
    return true;
}

bool BitbucketClient::DownloadFile(const std::string& repositoryUrl,
                                   const std::string& branch,
                                   const std::string& filePath,
                                   const std::string& outputPath,
                                   std::string& errorMessage) const {
    RepoCoordinates repo;
    if (!ParseRepositoryUrl(repositoryUrl, repo, errorMessage)) {
        return false;
    }

    ServerCredentials creds;
    if (!GetCredentialsForRepo(repo, creds, errorMessage)) {
        return false;
    }

    const auto branchRef = branch.empty() ? "master" : branch;
    const auto endpoint = repo.baseUrl + "/rest/api/1.0/projects/" + UrlEncode(repo.projectKey) +
        "/repos/" + UrlEncode(repo.repositorySlug) + "/raw/" + EncodePath(filePath) +
        "?at=" + UrlEncode("refs/heads/" + branchRef);

    return HttpDownloadBinary(endpoint, creds, outputPath, errorMessage);
}

bool BitbucketClient::ParseRepositoryUrl(const std::string& repositoryUrl,
                                         RepoCoordinates& out,
                                         std::string& errorMessage) {
    static const std::regex kScmPattern(R"(^\s*(https?)://([^/]+)/scm/([^/]+)/([^/]+?)(?:\.git)?/?\s*$)",
                                        std::regex::icase);
    static const std::regex kProjectsPattern(
        R"(^\s*(https?)://([^/]+)/projects/([^/]+)/repos/([^/]+?)(?:\.git)?(?:/.*)?\s*$)", std::regex::icase);

    std::smatch match;
    if (!std::regex_match(repositoryUrl, match, kScmPattern) &&
        !std::regex_match(repositoryUrl, match, kProjectsPattern)) {
        errorMessage = "Unsupported Bitbucket repository URL. Expected /scm/<project>/<repo>.git or /projects/<project>/repos/<repo>.";
        return false;
    }

    out.baseUrl = ToLower(match[1].str()) + "://" + match[2].str();
    out.hostPort = match[2].str();
    out.projectKey = match[3].str();
    out.repositorySlug = match[4].str();
    if (out.projectKey.empty() || out.repositorySlug.empty()) {
        errorMessage = "Bitbucket repository URL is missing project or repository segment.";
        return false;
    }
    return true;
}

bool BitbucketClient::GetCredentialsForRepo(const RepoCoordinates& repo,
                                            ServerCredentials& outCredentials,
                                            std::string& errorMessage) const {
    if (!credentials_.TryGetForHost(repo.hostPort, outCredentials) || outCredentials.password.empty()) {
        errorMessage = "No credentials found in ~/.m2/settings.xml for host '" + repo.hostPort + "'.";
        return false;
    }
    return true;
}

bool BitbucketClient::HttpGetText(const std::string& url,
                                  const ServerCredentials& creds,
                                  std::string& outBody,
                                  std::string& errorMessage) const {
    outBody.clear();

    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMessage = "Unable to initialize HTTP client.";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, EncodeUrlForCurl(url).c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBody);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    const auto userPwd = BuildCurlUserPwd(creds);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd.c_str());

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const auto result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        errorMessage = "HTTP request failed: " + std::string(curl_easy_strerror(result));
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        errorMessage = "Bitbucket API request failed with status " + std::to_string(statusCode) + ".";
        return false;
    }

    return true;
}

bool BitbucketClient::HttpDownloadBinary(const std::string& url,
                                         const ServerCredentials& creds,
                                         const std::string& outFile,
                                         std::string& errorMessage) const {
    std::ofstream output(outFile, std::ios::binary | std::ios::trunc);
    if (!output) {
        errorMessage = "Unable to open output file: " + outFile;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMessage = "Unable to initialize HTTP client.";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, EncodeUrlForCurl(url).c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    const auto userPwd = BuildCurlUserPwd(creds);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userPwd.c_str());

    const auto result = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    curl_easy_cleanup(curl);
    output.close();

    if (result != CURLE_OK) {
        errorMessage = "File download failed: " + std::string(curl_easy_strerror(result));
        return false;
    }
    if (statusCode < 200 || statusCode >= 300) {
        errorMessage = "Bitbucket raw file request failed with status " + std::to_string(statusCode) + ".";
        return false;
    }

    return true;
}

std::string BitbucketClient::BuildCurlUserPwd(const ServerCredentials& creds) {
    return creds.username + ":" + creds.password;
}

std::string BitbucketClient::UrlEncode(const std::string& value) {
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

std::string BitbucketClient::EncodePath(const std::string& path) {
    std::string encoded;
    encoded.reserve(path.size());
    for (unsigned char c : path) {
        if (c == '/') {
            encoded.push_back('/');
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
            c == '_' || c == '.' || c == '~') {
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

std::string BitbucketClient::EncodeUrlForCurl(const std::string& rawUrl) {
    std::string encoded;
    encoded.reserve(rawUrl.size());
    for (unsigned char ch : rawUrl) {
        if (ch == ' ') {
            encoded += "%20";
        } else {
            encoded.push_back(static_cast<char>(ch));
        }
    }
    return encoded;
}

bool BitbucketClient::IsXmlTopLevelPath(const std::string& path) {
    if (path.empty() || path.find('/') != std::string::npos) {
        return false;
    }
    const auto lower = ToLower(path);
    return lower.size() > 4 && lower.rfind(".xml") == lower.size() - 4;
}

}  // namespace confy
