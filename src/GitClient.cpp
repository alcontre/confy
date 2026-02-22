#include "GitClient.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>
#include <sys/wait.h>

namespace fs = std::filesystem;

namespace confy {

GitClient::GitClient(AuthCredentials credentials) : credentials_(std::move(credentials)) {}

bool GitClient::ExtractHostPort(const std::string& repositoryUrl, std::string& outHostPort) {
    const auto schemePos = repositoryUrl.find("://");
    if (schemePos == std::string::npos) {
        return false;
    }

    const auto hostStart = schemePos + 3;
    if (hostStart >= repositoryUrl.size()) {
        return false;
    }

    const auto hostEnd = repositoryUrl.find('/', hostStart);
    if (hostEnd == std::string::npos) {
        outHostPort = repositoryUrl.substr(hostStart);
    } else {
        outHostPort = repositoryUrl.substr(hostStart, hostEnd - hostStart);
    }

    return !outHostPort.empty();
}

std::vector<std::string> GitClient::ParseLsRemoteRefs(const std::string& lsRemoteOutput) {
    std::set<std::string> refs;
    std::istringstream stream(lsRemoteOutput);
    std::string line;
    while (std::getline(stream, line)) {
        const auto tabPos = line.find('\t');
        if (tabPos == std::string::npos || tabPos + 1 >= line.size()) {
            continue;
        }

        const auto ref = line.substr(tabPos + 1);
        if (ref.rfind("refs/heads/", 0) == 0) {
            refs.insert(ref.substr(std::string("refs/heads/").size()));
            continue;
        }
        if (ref.rfind("refs/tags/", 0) == 0) {
            auto tag = ref.substr(std::string("refs/tags/").size());
            if (tag.size() > 3 && tag.rfind("^{}") == tag.size() - 3) {
                tag.erase(tag.size() - 3);
            }
            refs.insert(std::move(tag));
        }
    }

    return std::vector<std::string>(refs.begin(), refs.end());
}

std::string GitClient::EscapeShellArg(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('\'');
    for (char c : value) {
        if (c == '\'') {
            escaped += "'\\''";
            continue;
        }
        escaped.push_back(c);
    }
    escaped.push_back('\'');
    return escaped;
}

std::string GitClient::UrlEncode(const std::string& value) {
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

int GitClient::DecodeExitCode(int rawExitCode) {
    if (rawExitCode == -1) {
        return -1;
    }

#ifdef WIFEXITED
    if (WIFEXITED(rawExitCode)) {
        return WEXITSTATUS(rawExitCode);
    }
#endif

    return rawExitCode;
}

bool GitClient::RunCommandCapture(const std::string& command,
                                  std::string& output,
                                  std::string& errorMessage) {
    output.clear();
    std::array<char, 512> buffer{};

    const std::string fullCommand = command + " 2>&1";
    FILE* pipe = popen(fullCommand.c_str(), "r");
    if (pipe == nullptr) {
        errorMessage = "Failed to start process: " + command;
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output.append(buffer.data());
    }

    const int rawExit = pclose(pipe);
    const int exitCode = DecodeExitCode(rawExit);
    if (exitCode != 0) {
        if (output.empty()) {
            errorMessage = "Command failed with exit code " + std::to_string(exitCode) + ": " + command;
        } else {
            errorMessage = output;
        }
        return false;
    }

    return true;
}

bool GitClient::BuildAuthenticatedUrl(const std::string& repositoryUrl,
                                      std::string& outUrl,
                                      std::string& errorMessage) const {
    if (repositoryUrl.rfind("http://", 0) != 0 && repositoryUrl.rfind("https://", 0) != 0) {
        outUrl = repositoryUrl;
        return true;
    }

    std::string hostPort;
    if (!ExtractHostPort(repositoryUrl, hostPort)) {
        errorMessage = "Unable to parse repository host from URL: " + repositoryUrl;
        return false;
    }

    ServerCredentials creds;
    if (!credentials_.TryGetByServerId(hostPort, creds)) {
        errorMessage = "No credentials found in .m2/settings.xml for server id '" + hostPort + "'.";
        return false;
    }

    if (creds.password.empty()) {
        errorMessage = "Missing token/password in .m2/settings.xml for server id '" + hostPort + "'.";
        return false;
    }

    const auto schemeEnd = repositoryUrl.find("://");
    const auto authStart = schemeEnd + 3;
    const auto pathStart = repositoryUrl.find('/', authStart);
    const auto authority = pathStart == std::string::npos
        ? repositoryUrl.substr(authStart)
        : repositoryUrl.substr(authStart, pathStart - authStart);
    const auto path = pathStart == std::string::npos ? std::string() : repositoryUrl.substr(pathStart);

    const std::string username = creds.username.empty() ? "x-token-auth" : creds.username;
    outUrl = repositoryUrl.substr(0, authStart) + UrlEncode(username) + ":" + UrlEncode(creds.password) + "@" +
             authority + path;
    return true;
}

bool GitClient::ListBranchesAndTags(const std::string& repositoryUrl,
                                    std::vector<std::string>& outRefs,
                                    std::string& errorMessage) const {
    outRefs.clear();

    std::string authenticatedUrl;
    if (!BuildAuthenticatedUrl(repositoryUrl, authenticatedUrl, errorMessage)) {
        return false;
    }

    std::string output;
    const std::string command = "git ls-remote --heads --tags " + EscapeShellArg(authenticatedUrl);
    if (!RunCommandCapture(command, output, errorMessage)) {
        return false;
    }

    outRefs = ParseLsRemoteRefs(output);
    return true;
}

bool GitClient::CloneRepository(const std::string& repositoryUrl,
                                const std::string& branchOrTag,
                                const std::string& targetDirectory,
                                bool shallow,
                                std::atomic<bool>& cancelRequested,
                                ProgressCallback progress,
                                std::string& errorMessage) const {
    if (cancelRequested.load()) {
        errorMessage = "Cancelled";
        return false;
    }

    std::string authenticatedUrl;
    if (!BuildAuthenticatedUrl(repositoryUrl, authenticatedUrl, errorMessage)) {
        return false;
    }

    const fs::path targetPath(targetDirectory);
    std::error_code fsError;
    fs::remove_all(targetPath, fsError);
    if (fsError) {
        errorMessage = "Failed to clear target directory '" + targetDirectory + "': " + fsError.message();
        return false;
    }

    fs::create_directories(targetPath.parent_path(), fsError);
    if (fsError) {
        errorMessage = "Failed to create parent directory for '" + targetDirectory + "': " + fsError.message();
        return false;
    }

    if (progress) {
        progress(5, "Cloning repository");
    }

    std::string output;
    std::string command = "git clone --recursive ";
    if (shallow) {
        command += "--depth 1 --shallow-submodules ";
    }
    if (!branchOrTag.empty()) {
        command += "--branch " + EscapeShellArg(branchOrTag) + " ";
    }
    command += EscapeShellArg(authenticatedUrl) + " " + EscapeShellArg(targetDirectory);

    if (!RunCommandCapture(command, output, errorMessage)) {
        return false;
    }

    if (cancelRequested.load()) {
        errorMessage = "Cancelled";
        return false;
    }

    if (progress) {
        progress(75, "Updating submodules");
    }

    std::string submoduleCommand =
        "git -C " + EscapeShellArg(targetDirectory) + " submodule update --init --recursive";
    if (shallow) {
        submoduleCommand += " --depth 1";
    }

    if (!RunCommandCapture(submoduleCommand, output, errorMessage)) {
        return false;
    }

    if (progress) {
        progress(100, "Completed");
    }

    return true;
}

}  // namespace confy
