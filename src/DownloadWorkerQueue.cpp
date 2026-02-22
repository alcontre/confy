#include "DownloadWorkerQueue.h"

#include "AuthCredentials.h"
#include "GitClient.h"
#include "NexusClient.h"

#include <cstdlib>
#include <filesystem>
#include <thread>

#include <wx/init.h>
#include <wx/log.h>

namespace confy {

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
    cancelAllRequested_.store(true);
    wxLogWarning("[download-worker] cancel-all requested");
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
  // Keep only the latest progress update per component as an "easy"
  // backpressure mechanism to avoid overwhelming the UI with progress updates
  // when there are many files to download. For other event types, or if there
  // is no existing progress event for the component, just push as normal.
  std::scoped_lock lock(eventMutex_);
  if (event.type == DownloadEventType::Progress && !events_.empty()) {
    DownloadEvent &tail = events_.back();
    if (tail.type == DownloadEventType::Progress &&
        tail.componentIndex == event.componentIndex) {
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
        PushEvent({source.jobId, source.componentIndex, DownloadEventType::Failed, 0, 0, "Missing home directory"});
        return;
    }

    AuthCredentials credentials;
    std::string credentialError;
    const std::string settingsPath = (std::filesystem::path(homeDir) / ".m2" / "settings.xml").string();
    if (!credentials.LoadFromM2SettingsXml(settingsPath, credentialError)) {
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
            PushEvent({source.jobId, source.componentIndex, DownloadEventType::Cancelled, 0, 0, "Cancelled"});
        } else {
            PushEvent({source.jobId, source.componentIndex, DownloadEventType::Failed, 0, 0, error});
        }
        return;
    }

    PushEvent({source.jobId, source.componentIndex, DownloadEventType::Completed, 100, 0, "Completed"});
}

}  // namespace confy
