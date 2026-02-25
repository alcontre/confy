#include "GitClient.h"

#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#if defined(__has_include)
#if __has_include(<wx/log.h>)
#include <wx/log.h>
#define CONFY_HAS_WX_LOG 1
#endif
#endif

#ifndef CONFY_HAS_WX_LOG
namespace {

void FallbackLog(const char *level, const char *format, ...)
{
   std::fprintf(stderr, "[git-client][%s] ", level);
   va_list args;
   va_start(args, format);
   std::vfprintf(stderr, format, args);
   va_end(args);
   std::fprintf(stderr, "\n");
}

} // namespace

#define wxLogMessage(...) FallbackLog("INFO", __VA_ARGS__)
#define wxLogError(...)   FallbackLog("ERROR", __VA_ARGS__)
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
bool RunCommandCaptureWindows(const std::string &command,
    std::string &output,
    std::string &errorMessage,
    const confy::GitClient::CommandOutputCallback &outputCallback,
    const std::atomic<bool> *cancelRequested)
{
   output.clear();
   wxLogMessage("[git-client] exec: %s", command.c_str());

   SECURITY_ATTRIBUTES securityAttributes{};
   securityAttributes.nLength        = sizeof(securityAttributes);
   securityAttributes.bInheritHandle = TRUE;

   HANDLE readPipe  = nullptr;
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
   startupInfo.cb         = sizeof(startupInfo);
   startupInfo.dwFlags    = STARTF_USESTDHANDLES;
   startupInfo.hStdOutput = writePipe;
   startupInfo.hStdError  = writePipe;

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
   bool cancelled = false;
   while (true) {
      DWORD availableBytes = 0;
      if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &availableBytes, nullptr)) {
         break;
      }

      if (availableBytes > 0) {
         DWORD bytesRead    = 0;
         const DWORD toRead = availableBytes < static_cast<DWORD>(buffer.size())
                                  ? availableBytes
                                  : static_cast<DWORD>(buffer.size());
         if (ReadFile(readPipe, buffer.data(), toRead, &bytesRead, nullptr) && bytesRead > 0) {
            output.append(buffer.data(), bytesRead);
            if (outputCallback) {
               outputCallback(std::string_view(buffer.data(), bytesRead));
            }
         }
      }

      const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 25);
      if (waitResult == WAIT_OBJECT_0) {
         continue;
      }

      if (cancelRequested != nullptr && cancelRequested->load()) {
         cancelled = true;
         TerminateProcess(processInfo.hProcess, 1);
         break;
      }
   }

   DWORD bytesRead = 0;
   while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
          bytesRead > 0) {
      output.append(buffer.data(), bytesRead);
      if (outputCallback) {
         outputCallback(std::string_view(buffer.data(), bytesRead));
      }
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

   if (cancelled) {
      errorMessage = "Cancelled";
      wxLogMessage("[git-client] command cancelled");
      return false;
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

#ifndef _WIN32
bool TerminateProcessGroupGracefully(pid_t processGroupId)
{
   if (processGroupId <= 0) {
      return false;
   }

   kill(-processGroupId, SIGTERM);
   for (int i = 0; i < 20; ++i) {
      if (kill(-processGroupId, 0) == -1 && errno == ESRCH) {
         return true;
      }
      ::usleep(10000);
   }

   kill(-processGroupId, SIGKILL);
   return true;
}
#endif

bool TryParsePercent(std::string_view line, int &outPercent)
{
   for (std::size_t i = 0; i < line.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
         continue;
      }

      int value     = 0;
      std::size_t j = i;
      while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) {
         value = value * 10 + (line[j] - '0');
         ++j;
      }
      if (j < line.size() && line[j] == '%') {
         outPercent = value;
         return true;
      }
      i = j;
   }
   return false;
}

void ReportMappedProgress(const confy::GitClient::ProgressCallback &progress,
    int stageMin,
    int stageMax,
    int stagePercent,
    const std::string &message,
    int &lastReportedPercent)
{
   if (!progress) {
      return;
   }

   if (stagePercent < 0) {
      stagePercent = 0;
   }
   if (stagePercent > 100) {
      stagePercent = 100;
   }

   const int mapped = stageMin + ((stageMax - stageMin) * stagePercent) / 100;
   if (mapped <= lastReportedPercent) {
      return;
   }

   lastReportedPercent = mapped;
   progress(mapped, message);
}

void ProcessGitProgressLine(const std::string &line,
    const confy::GitClient::ProgressCallback &progress,
    bool submodulePhase,
    int &lastReportedPercent)
{
   int stagePercent = 0;

   if (line.find("Counting objects:") != std::string::npos && TryParsePercent(line, stagePercent)) {
      const int stageMin = submodulePhase ? 80 : 10;
      const int stageMax = submodulePhase ? 86 : 28;
      ReportMappedProgress(progress, stageMin, stageMax, stagePercent, "Counting objects", lastReportedPercent);
      return;
   }

   if (line.find("Compressing objects:") != std::string::npos && TryParsePercent(line, stagePercent)) {
      const int stageMin = submodulePhase ? 86 : 28;
      const int stageMax = submodulePhase ? 90 : 44;
      ReportMappedProgress(progress,
          stageMin,
          stageMax,
          stagePercent,
          "Compressing objects",
          lastReportedPercent);
      return;
   }

   if (line.find("Receiving objects:") != std::string::npos && TryParsePercent(line, stagePercent)) {
      const int stageMin = submodulePhase ? 90 : 44;
      const int stageMax = submodulePhase ? 96 : 80;
      ReportMappedProgress(progress, stageMin, stageMax, stagePercent, "Receiving objects", lastReportedPercent);
      return;
   }

   if (line.find("Resolving deltas:") != std::string::npos && TryParsePercent(line, stagePercent)) {
      const int stageMin = submodulePhase ? 96 : 80;
      const int stageMax = submodulePhase ? 99 : 94;
      ReportMappedProgress(progress, stageMin, stageMax, stagePercent, "Resolving deltas", lastReportedPercent);
      return;
   }

   if (line.find("Checking out files:") != std::string::npos && TryParsePercent(line, stagePercent)) {
      const int stageMin = submodulePhase ? 99 : 94;
      const int stageMax = submodulePhase ? 100 : 99;
      ReportMappedProgress(progress, stageMin, stageMax, stagePercent, "Checking out files", lastReportedPercent);
      return;
   }
}

void PumpProgressFromChunk(const confy::GitClient::ProgressCallback &progress,
    std::string_view chunk,
    bool submodulePhase,
    std::string &partialLine,
    int &lastReportedPercent)
{
   if (!progress || chunk.empty()) {
      return;
   }

   partialLine.append(chunk.data(), chunk.size());
   std::size_t consumed = 0;

   for (std::size_t i = 0; i < partialLine.size(); ++i) {
      if (partialLine[i] != '\n' && partialLine[i] != '\r') {
         continue;
      }

      const std::string line = partialLine.substr(consumed, i - consumed);
      ProcessGitProgressLine(line, progress, submodulePhase, lastReportedPercent);

      while (i + 1 < partialLine.size() &&
             (partialLine[i + 1] == '\n' || partialLine[i + 1] == '\r')) {
         ++i;
      }
      consumed = i + 1;
   }

   if (consumed > 0) {
      partialLine.erase(0, consumed);
   }
}

std::string NormalizeRepositoryUrl(std::string repositoryUrl)
{
   while (repositoryUrl.size() > 1 && repositoryUrl.back() == '/') {
      repositoryUrl.pop_back();
   }
   return repositoryUrl;
}

#ifdef _WIN32
void ClearReadOnlyAttributeRecursive(const fs::path &rootPath)
{
   std::error_code ec;
   if (!fs::exists(rootPath, ec)) {
      return;
   }

   auto clearOne = [](const fs::path &path) {
      const std::string nativePath = path.string();
      const DWORD attrs            = GetFileAttributesA(nativePath.c_str());
      if (attrs == INVALID_FILE_ATTRIBUTES) {
         return;
      }
      if ((attrs & FILE_ATTRIBUTE_READONLY) == 0) {
         return;
      }
      SetFileAttributesA(nativePath.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
   };

   clearOne(rootPath);
   for (fs::recursive_directory_iterator it(rootPath, ec), end; !ec && it != end; it.increment(ec)) {
      clearOne(it->path());
   }
}
#endif

} // namespace

namespace confy {

GitClient::GitClient(AuthCredentials credentials) : credentials_(std::move(credentials)) {}

bool GitClient::ExtractHostPort(const std::string &repositoryUrl, std::string &outHostPort)
{
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

std::vector<std::string> GitClient::ParseLsRemoteRefs(const std::string &lsRemoteOutput)
{
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

std::string GitClient::EscapeShellArg(const std::string &value)
{
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

int GitClient::DecodeExitCode(int rawExitCode)
{
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

bool GitClient::RunCommandCapture(const std::string &command,
    std::string &output,
    std::string &errorMessage,
    CommandOutputCallback outputCallback,
    const std::atomic<bool> *cancelRequested)
{
#ifdef _WIN32
   return RunCommandCaptureWindows(command, output, errorMessage, outputCallback, cancelRequested);
#else
   output.clear();
   errorMessage.clear();
   wxLogMessage("[git-client] exec: %s", command.c_str());

   int outputPipe[2]{-1, -1};
   if (pipe(outputPipe) != 0) {
      errorMessage = "Failed to create process output pipe.";
      wxLogError("[git-client] failed to create process output pipe");
      return false;
   }

   const pid_t childPid = fork();
   if (childPid == -1) {
      close(outputPipe[0]);
      close(outputPipe[1]);
      errorMessage = "Failed to start process: " + command;
      wxLogError("[git-client] failed to fork process");
      return false;
   }

   if (childPid == 0) {
      ::setpgid(0, 0);
      ::dup2(outputPipe[1], STDOUT_FILENO);
      ::dup2(outputPipe[1], STDERR_FILENO);
      close(outputPipe[0]);
      close(outputPipe[1]);
      execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char *>(nullptr));
      _exit(127);
   }

   ::setpgid(childPid, childPid);
   close(outputPipe[1]);

   const int flags = fcntl(outputPipe[0], F_GETFL, 0);
   if (flags >= 0) {
      fcntl(outputPipe[0], F_SETFL, flags | O_NONBLOCK);
   }

   bool childExited = false;
   bool cancelled   = false;
   int rawExit      = -1;
   std::array<char, 512> buffer{};
   pollfd pfd{};
   pfd.fd     = outputPipe[0];
   pfd.events = POLLIN;

   while (!childExited) {
      const int pollResult = poll(&pfd, 1, 50);
      if (pollResult > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
         while (true) {
            const ssize_t readCount = read(outputPipe[0], buffer.data(), buffer.size());
            if (readCount > 0) {
               output.append(buffer.data(), static_cast<std::size_t>(readCount));
               if (outputCallback) {
                  outputCallback(std::string_view(buffer.data(), static_cast<std::size_t>(readCount)));
               }
               continue;
            }
            if (readCount == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
               break;
            }
            break;
         }
      }

      if (cancelRequested != nullptr && cancelRequested->load()) {
         cancelled = true;
         TerminateProcessGroupGracefully(childPid);
      }

      int status         = 0;
      const pid_t waited = waitpid(childPid, &status, WNOHANG);
      if (waited == childPid) {
         rawExit     = status;
         childExited = true;
      }
   }

   while (true) {
      const ssize_t readCount = read(outputPipe[0], buffer.data(), buffer.size());
      if (readCount > 0) {
         output.append(buffer.data(), static_cast<std::size_t>(readCount));
         if (outputCallback) {
            outputCallback(std::string_view(buffer.data(), static_cast<std::size_t>(readCount)));
         }
         continue;
      }
      break;
   }

   close(outputPipe[0]);

   const int exitCode = DecodeExitCode(rawExit);
   wxLogMessage("[git-client] exit=%d", exitCode);
   if (!output.empty()) {
      wxLogMessage("[git-client] output:\n%s", output.c_str());
   }

   if (cancelled) {
      errorMessage = "Cancelled";
      wxLogMessage("[git-client] command cancelled");
      return false;
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

bool GitClient::BuildAuthConfigArg(const std::string &repositoryUrl,
    std::string &outConfigArg,
    std::string &errorMessage) const
{
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

bool GitClient::ListBranchesAndTags(const std::string &repositoryUrl,
    std::vector<std::string> &outRefs,
    std::string &errorMessage) const
{
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

bool GitClient::CloneRepository(const std::string &repositoryUrl,
    const std::string &branchOrTag,
    const std::string &targetDirectory,
    bool shallow,
    std::atomic<bool> &cancelRequested,
    ProgressCallback progress,
    std::string &errorMessage) const
{
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
#ifdef _WIN32
   ClearReadOnlyAttributeRecursive(targetPath);
#endif
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

   int lastReportedPercent = 5;
   std::string clonePartialLine;

   std::string output;
   std::string command = "git ";
   if (!authConfigArg.empty()) {
      command += authConfigArg + " ";
   }
   command += "clone --recursive --progress ";
   if (shallow) {
      command += "--depth 1 --shallow-submodules ";
   }
   if (!branchOrTag.empty()) {
      command += "--branch " + EscapeShellArg(branchOrTag) + " ";
   }
   command += EscapeShellArg(normalizedRepositoryUrl) + " " + EscapeShellArg(targetDirectory);

   if (!RunCommandCapture(
           command,
           output,
           errorMessage,
           [&](std::string_view chunk) {
              PumpProgressFromChunk(progress,
                  chunk,
                  false,
                  clonePartialLine,
                  lastReportedPercent);
           },
           &cancelRequested)) {
      return false;
   }
   if (!clonePartialLine.empty()) {
      ProcessGitProgressLine(clonePartialLine, progress, false, lastReportedPercent);
   }

   if (cancelRequested.load()) {
      errorMessage = "Cancelled";
      return false;
   }

   if (progress) {
      const int kickoff = lastReportedPercent < 80 ? 80 : lastReportedPercent;
      progress(kickoff, "Updating submodules");
      lastReportedPercent = kickoff;
   }

   std::string submodulePartialLine;

   std::string submoduleCommand = "git ";
   if (!authConfigArg.empty()) {
      submoduleCommand += authConfigArg + " ";
   }
   submoduleCommand +=
       "-C " + EscapeShellArg(targetDirectory) + " submodule update --init --recursive --progress";
   if (shallow) {
      submoduleCommand += " --depth 1";
   }

   if (!RunCommandCapture(
           submoduleCommand,
           output,
           errorMessage,
           [&](std::string_view chunk) {
              PumpProgressFromChunk(progress,
                  chunk,
                  true,
                  submodulePartialLine,
                  lastReportedPercent);
           },
           &cancelRequested)) {
      return false;
   }
   if (!submodulePartialLine.empty()) {
      ProcessGitProgressLine(submodulePartialLine, progress, true, lastReportedPercent);
   }

   if (progress) {
      progress(100, "Completed");
   }

   return true;
}

} // namespace confy
