#include "DownloadWorkerQueue.h"

#include "AuthCredentials.h"
#include "GitClient.h"
#include "NexusClient.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <filesystem>
#include <thread>
#include <vector>

#include <wx/init.h>
#include <wx/log.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <array>
#include <cstdio>
#include <sys/wait.h>
#endif

namespace confy {

namespace {

std::string TrimScript(const std::string& script) {
    const auto first = script.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = script.find_last_not_of(" \t\r\n");
    return script.substr(first, last - first + 1);
}

std::string EscapeShArg(const std::string& value) {
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

int DecodeExitCode(int rawExitCode) {
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

}  // namespace

DownloadWorkerQueue::DownloadWorkerQueue(std::size_t workerCount)
    : workerCount_(workerCount == 0 ? 1 : workerCount) {}

DownloadWorkerQueue::~DownloadWorkerQueue() {
    Stop();
}

void DownloadWorkerQueue::Start() {
    std::scoped_lock lock(queueMutex_);
    if (started_) {
        return;
    }

    started_ = true;
    stopping_ = false;
    cancelAllRequested_.store(false);

    workers_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        workers_.emplace_back(&DownloadWorkerQueue::WorkerLoop, this);
    }

    wxLogMessage("[download-worker] started with workerCount=%zu", workerCount_);
}

void DownloadWorkerQueue::Stop() {
    {
        std::scoped_lock lock(queueMutex_);
        if (!started_) {
            return;
        }
        stopping_ = true;
        cancelAllRequested_.store(true);
    }

    queueCv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();

    {
        std::scoped_lock lock(queueMutex_);
        started_ = false;
        stopping_ = false;
        std::queue<DownloadJob> empty;
        pendingJobs_.swap(empty);
    }

    wxLogMessage("[download-worker] stopped");
}

void DownloadWorkerQueue::Submit(DownloadJob job) {
    if (job.kind == DownloadJobKind::NexusArtifact) {
        wxLogMessage("[download-worker] enqueue artifact jobId=%llu component='%s' version='%s' buildType='%s'",
                     static_cast<unsigned long long>(job.artifact.jobId),
                     job.artifact.componentName.c_str(),
                     job.artifact.version.c_str(),
                     job.artifact.buildType.c_str());
    } else {
        wxLogMessage("[download-worker] enqueue source jobId=%llu component='%s' ref='%s' shallow=%d",
                     static_cast<unsigned long long>(job.source.jobId),
                     job.source.componentName.c_str(),
                     job.source.branchOrTag.c_str(),
                     job.source.shallow ? 1 : 0);
    }
    {
        std::scoped_lock lock(queueMutex_);
        cancelAllRequested_.store(false);
        pendingJobs_.push(std::move(job));
    }
    queueCv_.notify_one();
}

void DownloadWorkerQueue::RequestCancelAll() {
    std::vector<DownloadJob> cancelledJobs;
    {
        std::scoped_lock lock(queueMutex_);
        cancelAllRequested_.store(true);
        while (!pendingJobs_.empty()) {
            cancelledJobs.push_back(std::move(pendingJobs_.front()));
            pendingJobs_.pop();
        }
    }

    for (const auto& job : cancelledJobs) {
        PushEvent({job.JobId(), job.ComponentIndex(), DownloadEventType::Cancelled, 0, 0, "Cancelled"});
    }

    queueCv_.notify_all();
    wxLogWarning("[download-worker] cancel-all requested; drained %zu queued job(s)", cancelledJobs.size());
}

bool DownloadWorkerQueue::TryPopEvent(DownloadEvent& outEvent) {
    std::scoped_lock lock(eventMutex_);
    if (events_.empty()) {
        return false;
    }

    outEvent = std::move(events_.front());
    events_.pop();
    return true;
}

void DownloadWorkerQueue::WorkerLoop() {
    wxInitializer wxInit;
    if (!wxInit.IsOk()) {
        wxLogError("[download-worker] wx runtime init failed in worker thread");
        while (true) {
            DownloadJob job;
            {
                std::unique_lock lock(queueMutex_);
                if (pendingJobs_.empty()) {
                    return;
                }
                job = std::move(pendingJobs_.front());
                pendingJobs_.pop();
            }
            PushEvent({job.JobId(),
                       job.ComponentIndex(),
                       DownloadEventType::Failed,
                       0,
                       0,
                       "Failed to initialize wx runtime in worker thread"});
        }
    }

    wxLogMessage("[download-worker] worker thread started");

    while (true) {
        DownloadJob job;

        {
            std::unique_lock lock(queueMutex_);
            queueCv_.wait(lock, [this]() { return stopping_ || !pendingJobs_.empty(); });

            if (stopping_ && pendingJobs_.empty()) {
                wxLogMessage("[download-worker] worker thread exiting");
                return;
            }

            job = std::move(pendingJobs_.front());
            pendingJobs_.pop();
        }

        if (cancelAllRequested_.load()) {
            wxLogWarning("[download-worker] skip jobId=%llu due to cancellation",
                         static_cast<unsigned long long>(job.JobId()));
            PushEvent({job.JobId(), job.ComponentIndex(), DownloadEventType::Cancelled, 0, 0, "Cancelled"});
            continue;
        }

        ProcessJob(job);
    }
}

void DownloadWorkerQueue::PushEvent(DownloadEvent event) {
    // Keep only the latest progress update per job as an "easy"
    // backpressure mechanism to avoid overwhelming the UI with progress updates
    // when there are many files to download. For other event types, or if there
    // is no existing progress event for the same job, just push as normal.
    std::scoped_lock lock(eventMutex_);
    if (event.type == DownloadEventType::Progress && !events_.empty()) {
        DownloadEvent &tail = events_.back();
        if (tail.type == DownloadEventType::Progress &&
            tail.jobId == event.jobId) {
            tail = std::move(event);
            return;
        }
    }
    events_.push(std::move(event));
}

void DownloadWorkerQueue::ProcessJob(const NexusDownloadJob& job) {
    wxLogMessage("[download-worker] start jobId=%llu component='%s' repoUrl='%s' target='%s'",
                 static_cast<unsigned long long>(job.jobId),
                 job.componentName.c_str(),
                 job.repositoryUrl.c_str(),
                 job.targetDirectory.c_str());
    PushEvent({job.jobId, job.componentIndex, DownloadEventType::Started, 0, 0, "Starting"});

#ifdef _WIN32
    std::string homeDir;
    if (const char* h = std::getenv("USERPROFILE")) {
        homeDir = h;
    } else {
        const char* drive = std::getenv("HOMEDRIVE");
        const char* homepath = std::getenv("HOMEPATH");
        if (drive && homepath) {
            homeDir = std::string(drive) + homepath;
        }
    }
#else
    const char* homeEnv = std::getenv("HOME");
    const std::string homeDir = homeEnv ? homeEnv : "";
#endif
    if (homeDir.empty()) {
        wxLogError("[download-worker] jobId=%llu failed: home directory not available",
                   static_cast<unsigned long long>(job.jobId));
        PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, 0, "Missing home directory"});
        return;
    }

    AuthCredentials credentials;
    std::string credentialError;
    const std::string settingsPath = (std::filesystem::path(homeDir) / ".m2" / "settings.xml").string();
    if (!credentials.LoadFromM2SettingsXml(settingsPath, credentialError)) {
        wxLogError("[download-worker] jobId=%llu auth load failed: %s",
                   static_cast<unsigned long long>(job.jobId),
                                     credentialError.c_str());
        PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, 0, credentialError});
        return;
    }

    wxLogMessage("[download-worker] jobId=%llu using m2 settings: %s",
                 static_cast<unsigned long long>(job.jobId),
                                 settingsPath.c_str());

    NexusClient client(std::move(credentials));
    std::string error;

    const auto ok = client.DownloadArtifactTree(
        job.repositoryUrl,
        job.componentName,
        job.version,
        job.buildType,
        job.targetDirectory,
        job.regexIncludes,
        job.regexExcludes,
        cancelAllRequested_,
        // Progress callback
        [this, &job](int percent, std::uint64_t downloadedBytes, const std::string& message) {
            wxLogMessage("[download-worker] progress jobId=%llu percent=%d message='%s'",
                         static_cast<unsigned long long>(job.jobId),
                         percent,
                         message.c_str());
            PushEvent({job.jobId,
                       job.componentIndex,
                       DownloadEventType::Progress,
                       percent,
                       downloadedBytes,
                       message});
        },
        error);

    if (!ok) {
        if (cancelAllRequested_.load()) {
            wxLogWarning("[download-worker] cancelled jobId=%llu",
                         static_cast<unsigned long long>(job.jobId));
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Cancelled, 0, 0, "Cancelled"});
        } else {
            wxLogError("[download-worker] failed jobId=%llu error='%s'",
                       static_cast<unsigned long long>(job.jobId),
                       error.c_str());
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, 0, error});
        }
        return;
    }

    std::string scriptError;
    if (!ExecutePostDownloadScript(job.postDownloadScript, job.targetDirectory, scriptError)) {
        wxLogError("[download-worker] script failed jobId=%llu error='%s'",
                   static_cast<unsigned long long>(job.jobId),
                   scriptError.c_str());
        PushEvent({job.jobId,
                   job.componentIndex,
                   DownloadEventType::Failed,
                   0,
                   0,
                   "Post-download script failed: " + scriptError});
        return;
    }

    wxLogMessage("[download-worker] completed jobId=%llu", static_cast<unsigned long long>(job.jobId));
    PushEvent({job.jobId, job.componentIndex, DownloadEventType::Completed, 100, 0, "Completed"});
}

void DownloadWorkerQueue::ProcessJob(const DownloadJob& job) {
    if (job.kind == DownloadJobKind::NexusArtifact) {
        ProcessJob(job.artifact);
        return;
    }

    const auto& source = job.source;
    wxLogMessage("[download-worker] start source jobId=%llu component='%s' repoUrl='%s' target='%s' shallow=%d",
                 static_cast<unsigned long long>(source.jobId),
                 source.componentName.c_str(),
                 source.repositoryUrl.c_str(),
                 source.targetDirectory.c_str(),
                 source.shallow ? 1 : 0);

    PushEvent({source.jobId, source.componentIndex, DownloadEventType::Started, 0, 0, "Starting"});

#ifdef _WIN32
    std::string homeDir;
    if (const char* h = std::getenv("USERPROFILE")) {
        homeDir = h;
    } else {
        const char* drive = std::getenv("HOMEDRIVE");
        const char* homepath = std::getenv("HOMEPATH");
        if (drive && homepath) {
            homeDir = std::string(drive) + homepath;
        }
    }
#else
    const char* homeEnv = std::getenv("HOME");
    const std::string homeDir = homeEnv ? homeEnv : "";
#endif
    if (homeDir.empty()) {
        wxLogError("[download-worker] failed source jobId=%llu component='%s' reason='Missing home directory'",
                   static_cast<unsigned long long>(source.jobId),
                   source.componentName.c_str());
        PushEvent({source.jobId, source.componentIndex, DownloadEventType::Failed, 0, 0, "Missing home directory"});
        return;
    }

    AuthCredentials credentials;
    std::string credentialError;
    const std::string settingsPath = (std::filesystem::path(homeDir) / ".m2" / "settings.xml").string();
    if (!credentials.LoadFromM2SettingsXml(settingsPath, credentialError)) {
        wxLogError("[download-worker] failed source jobId=%llu component='%s' reason='Credential load failed: %s'",
                   static_cast<unsigned long long>(source.jobId),
                   source.componentName.c_str(),
                   credentialError.c_str());
        PushEvent({source.jobId,
                   source.componentIndex,
                   DownloadEventType::Failed,
                   0,
                   0,
                   "Credential load failed: " + credentialError});
        return;
    }

    GitClient client(std::move(credentials));
    std::string error;
    const auto ok = client.CloneRepository(
        source.repositoryUrl,
        source.branchOrTag,
        source.targetDirectory,
        source.shallow,
        cancelAllRequested_,
        [this, &source](int percent, const std::string& message) {
            PushEvent({source.jobId, source.componentIndex, DownloadEventType::Progress, percent, 0, message});
        },
        error);

    if (!ok) {
        if (cancelAllRequested_.load() || error == "Cancelled") {
            wxLogWarning("[download-worker] cancelled source jobId=%llu component='%s'",
                         static_cast<unsigned long long>(source.jobId),
                         source.componentName.c_str());
            PushEvent({source.jobId, source.componentIndex, DownloadEventType::Cancelled, 0, 0, "Cancelled"});
        } else {
            wxLogError("[download-worker] failed source jobId=%llu component='%s' error='%s'",
                       static_cast<unsigned long long>(source.jobId),
                       source.componentName.c_str(),
                       error.c_str());
            PushEvent({source.jobId, source.componentIndex, DownloadEventType::Failed, 0, 0, error});
        }
        return;
    }

    std::string scriptError;
    if (!ExecutePostDownloadScript(source.postDownloadScript, source.targetDirectory, scriptError)) {
        wxLogError("[download-worker] source script failed jobId=%llu error='%s'",
                   static_cast<unsigned long long>(source.jobId),
                   scriptError.c_str());
        PushEvent({source.jobId,
                   source.componentIndex,
                   DownloadEventType::Failed,
                   0,
                   0,
                   "Post-download script failed: " + scriptError});
        return;
    }

    wxLogMessage("[download-worker] completed source jobId=%llu component='%s'",
                 static_cast<unsigned long long>(source.jobId),
                 source.componentName.c_str());
    PushEvent({source.jobId, source.componentIndex, DownloadEventType::Completed, 100, 0, "Completed"});
}

bool DownloadWorkerQueue::ExecutePostDownloadScript(const std::string& script,
                                                    const std::string& workingDirectory,
                                                    std::string& errorMessage) {
    errorMessage.clear();
    const std::string trimmedScript = TrimScript(script);
    if (trimmedScript.empty()) {
        return true;
    }

    const std::filesystem::path workDirPath(workingDirectory);
    if (!std::filesystem::exists(workDirPath)) {
        errorMessage = "Working directory does not exist: " + workingDirectory;
        return false;
    }
    if (!std::filesystem::is_directory(workDirPath)) {
        errorMessage = "Working directory is not a directory: " + workingDirectory;
        return false;
    }

#ifdef _WIN32
    const std::filesystem::path scriptPath =
        workDirPath / (".confy-post-download-" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                       "-" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())) +
                       ".ps1");
#else
    const std::filesystem::path scriptPath =
        workDirPath / (".confy-post-download-" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) +
                       "-" + std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())) +
                       ".sh");
#endif

    {
        std::ofstream out(scriptPath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            errorMessage = "Failed to create temporary script file in: " + workingDirectory;
            return false;
        }
        out << trimmedScript;
        if (!out.good()) {
            errorMessage = "Failed to write temporary script file: " + scriptPath.string();
            return false;
        }
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        std::filesystem::remove(scriptPath);
        errorMessage = "Failed to create output pipe for script process.";
        return false;
    }

    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        std::filesystem::remove(scriptPath);
        errorMessage = "Failed to configure output pipe for script process.";
        return false;
    }

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInfo{};
    const std::string commandLine =
        "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + scriptPath.string() + "\"";
    std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back('\0');

    const std::string workingDirNative = workDirPath.string();
    const BOOL started = CreateProcessA(nullptr,
                                        commandLineBuffer.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        workingDirNative.c_str(),
                                        &startupInfo,
                                        &processInfo);
    CloseHandle(writePipe);

    if (!started) {
        CloseHandle(readPipe);
        std::filesystem::remove(scriptPath);
        errorMessage = "Failed to start script with PowerShell.";
        return false;
    }

    std::string output;
    std::vector<char> buffer(512);
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }
    CloseHandle(readPipe);

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        std::filesystem::remove(scriptPath);
        errorMessage = "Failed to read script process exit code.";
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    std::filesystem::remove(scriptPath);

    if (exitCode != 0) {
        if (output.empty()) {
            errorMessage = "Script failed with exit code " + std::to_string(static_cast<unsigned long>(exitCode));
        } else {
            errorMessage = output;
        }
        return false;
    }

    return true;
#else
    std::array<char, 512> buffer{};
    std::string output;
    const std::string command =
        "cd " + EscapeShArg(workDirPath.string()) + " && /bin/sh " + EscapeShArg(scriptPath.string()) + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        std::filesystem::remove(scriptPath);
        errorMessage = "Failed to start script with /bin/sh.";
        return false;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output.append(buffer.data());
    }

    const int rawExitCode = pclose(pipe);
    const int exitCode = DecodeExitCode(rawExitCode);
    std::filesystem::remove(scriptPath);

    if (exitCode != 0) {
        if (output.empty()) {
            errorMessage = "Script failed with exit code " + std::to_string(exitCode);
        } else {
            errorMessage = output;
        }
        return false;
    }

    return true;
#endif
}

}  // namespace confy
