#include "GitClient.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>
#include <vector>

#if defined(__has_include)
#if __has_include(<wx/log.h>)
#include <wx/log.h>
#define CONFY_HAS_WX_LOG 1
#endif
#endif

#ifndef CONFY_HAS_WX_LOG
namespace {

void FallbackLog(const char* level, const char* format, ...) {
    std::fprintf(stderr, "[git-client][%s] ", level);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}

}  // namespace

#define wxLogMessage(...) FallbackLog("INFO", __VA_ARGS__)
#define wxLogError(...) FallbackLog("ERROR", __VA_ARGS__)
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
bool RunCommandCaptureWindows(const std::string& command,
                              std::string& output,
                              std::string& errorMessage) {
    output.clear();
    wxLogMessage("[git-client] exec: %s", command.c_str());

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        errorMessage = "Failed to create process output pipe.";
        wxLogError("[git-client] create pipe failed");
        return false;
    }

    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        errorMessage = "Failed to configure process output pipe.";
        wxLogError("[git-client] set handle information failed");
        return false;
    }

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInfo{};
    std::string commandLine = "cmd.exe /d /s /c \"" + command + "\"";
    std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back('\0');
    if (!CreateProcessA(nullptr,
                        commandLineBuffer.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW,
                        nullptr,
                        nullptr,
                        &startupInfo,
                        &processInfo)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        errorMessage = "Failed to start process: " + command;
        wxLogError("[git-client] failed to start process");
        return false;
    }

    CloseHandle(writePipe);

    std::array<char, 512> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
           bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }

    CloseHandle(readPipe);

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        errorMessage = "Failed to read process exit code: " + command;
        wxLogError("[git-client] failed to read process exit code");
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    wxLogMessage("[git-client] exit=%lu", static_cast<unsigned long>(exitCode));
    if (!output.empty()) {
        wxLogMessage("[git-client] output:\n%s", output.c_str());
    }

    if (exitCode != 0) {
        if (output.empty()) {
            errorMessage = "Command failed with exit code " + std::to_string(exitCode) + ": " + command;
        } else {
            errorMessage = output;
        }
        wxLogError("[git-client] command failed");
        return false;
    }

    return true;
}
#endif

std::string NormalizeRepositoryUrl(std::string repositoryUrl) {
    while (repositoryUrl.size() > 1 && repositoryUrl.back() == '/') {
        repositoryUrl.pop_back();
    }
    return repositoryUrl;
}

FILE* OpenCommandPipe(const char* command, const char* mode) {
#ifdef _WIN32
    return _popen(command, mode);
#else
    return popen(command, mode);
#endif
}

int CloseCommandPipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

}  // namespace

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
#ifdef _WIN32
    std::string escaped;
    escaped.reserve(value.size() * 2 + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') {
            escaped += "\\\"";
            continue;
        }
        if (c == '%') {
            escaped += "%%";
            continue;
        }
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
#else
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
#endif
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
#ifdef _WIN32
    return RunCommandCaptureWindows(command, output, errorMessage);
#else
    output.clear();
    std::array<char, 512> buffer{};

    const std::string fullCommand = command + " 2>&1";
    wxLogMessage("[git-client] exec: %s", fullCommand.c_str());
    FILE* pipe = OpenCommandPipe(fullCommand.c_str(), "r");
    if (pipe == nullptr) {
        errorMessage = "Failed to start process: " + command;
        wxLogError("[git-client] failed to open process pipe");
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output.append(buffer.data());
    }

    const int rawExit = CloseCommandPipe(pipe);
    const int exitCode = DecodeExitCode(rawExit);
    wxLogMessage("[git-client] exit=%d", exitCode);
    if (!output.empty()) {
        wxLogMessage("[git-client] output:\n%s", output.c_str());
    }
    if (exitCode != 0) {
        if (output.empty()) {
            errorMessage = "Command failed with exit code " + std::to_string(exitCode) + ": " + command;
        } else {
            errorMessage = output;
        }
        wxLogError("[git-client] command failed");
        return false;
    }

    return true;
#endif
}

bool GitClient::BuildAuthConfigArg(const std::string& repositoryUrl,
                                   std::string& outConfigArg,
                                   std::string& errorMessage) const {
    outConfigArg.clear();

    if (repositoryUrl.rfind("http://", 0) != 0 && repositoryUrl.rfind("https://", 0) != 0) {
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

    outConfigArg = "-c " + EscapeShellArg("http.extraHeader=Authorization: Bearer " + creds.password);
    return true;
}

bool GitClient::ListBranchesAndTags(const std::string& repositoryUrl,
                                    std::vector<std::string>& outRefs,
                                    std::string& errorMessage) const {
    outRefs.clear();
    const std::string normalizedRepositoryUrl = NormalizeRepositoryUrl(repositoryUrl);

    std::string authConfigArg;
    if (!BuildAuthConfigArg(normalizedRepositoryUrl, authConfigArg, errorMessage)) {
        return false;
    }

    std::string output;
    std::string command = "git ";
    if (!authConfigArg.empty()) {
        command += authConfigArg + " ";
    }
    command += "ls-remote --heads --tags " + EscapeShellArg(normalizedRepositoryUrl);
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
    const std::string normalizedRepositoryUrl = NormalizeRepositoryUrl(repositoryUrl);

    if (cancelRequested.load()) {
        errorMessage = "Cancelled";
        return false;
    }

    std::string authConfigArg;
    if (!BuildAuthConfigArg(normalizedRepositoryUrl, authConfigArg, errorMessage)) {
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
    std::string command = "git ";
    if (!authConfigArg.empty()) {
        command += authConfigArg + " ";
    }
    command += "clone --recursive ";
    if (shallow) {
        command += "--depth 1 --shallow-submodules ";
    }
    if (!branchOrTag.empty()) {
        command += "--branch " + EscapeShellArg(branchOrTag) + " ";
    }
    command += EscapeShellArg(normalizedRepositoryUrl) + " " + EscapeShellArg(targetDirectory);

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

    std::string submoduleCommand = "git ";
    if (!authConfigArg.empty()) {
        submoduleCommand += authConfigArg + " ";
    }
    submoduleCommand +=
        "-C " + EscapeShellArg(targetDirectory) + " submodule update --init --recursive";
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
