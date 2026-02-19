#include "DownloadWorkerQueue.h"

#include "NexusClient.h"
#include "AuthCredentials.h"

#include <cstdlib>
#include <iostream>
#include <thread>

#include <wx/init.h>

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

    std::cout << "[download-worker] started with workerCount=" << workerCount_ << std::endl;
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
        std::queue<NexusDownloadJob> empty;
        pendingJobs_.swap(empty);
    }

    std::cout << "[download-worker] stopped" << std::endl;
}

void DownloadWorkerQueue::Submit(NexusDownloadJob job) {
    std::cout << "[download-worker] enqueue jobId=" << job.jobId << " component='" << job.componentName
              << "' version='" << job.version << "' buildType='" << job.buildType << "'" << std::endl;
    {
        std::scoped_lock lock(queueMutex_);
        pendingJobs_.push(std::move(job));
    }
    queueCv_.notify_one();
}

void DownloadWorkerQueue::RequestCancelAll() {
    cancelAllRequested_.store(true);
    std::cout << "[download-worker] cancel-all requested" << std::endl;
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
        std::cerr << "[download-worker] wx runtime init failed in worker thread" << std::endl;
        while (true) {
            NexusDownloadJob job;
            {
                std::unique_lock lock(queueMutex_);
                if (pendingJobs_.empty()) {
                    return;
                }
                job = std::move(pendingJobs_.front());
                pendingJobs_.pop();
            }
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, "Failed to initialize wx runtime in worker thread"});
        }
    }

    std::cout << "[download-worker] worker thread started (id=" << std::this_thread::get_id() << ")"
              << std::endl;

    while (true) {
        NexusDownloadJob job;

        {
            std::unique_lock lock(queueMutex_);
            queueCv_.wait(lock, [this]() { return stopping_ || !pendingJobs_.empty(); });

            if (stopping_ && pendingJobs_.empty()) {
                std::cout << "[download-worker] worker thread exiting (id=" << std::this_thread::get_id() << ")"
                          << std::endl;
                return;
            }

            job = std::move(pendingJobs_.front());
            pendingJobs_.pop();
        }

        if (cancelAllRequested_.load()) {
            std::cout << "[download-worker] skip jobId=" << job.jobId << " due to cancellation" << std::endl;
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Cancelled, 0, "Cancelled"});
            continue;
        }

        ProcessJob(job);
    }
}

void DownloadWorkerQueue::PushEvent(DownloadEvent event) {
    std::scoped_lock lock(eventMutex_);
    events_.push(std::move(event));
}

void DownloadWorkerQueue::ProcessJob(const NexusDownloadJob& job) {
    std::cout << "[download-worker] start jobId=" << job.jobId << " component='" << job.componentName
              << "' repoUrl='" << job.repositoryUrl << "' target='" << job.targetDirectory << "'" << std::endl;
    PushEvent({job.jobId, job.componentIndex, DownloadEventType::Started, 0, "Starting"});

    const char* home = std::getenv("HOME");
    if (!home) {
        std::cerr << "[download-worker] jobId=" << job.jobId << " failed: HOME not set" << std::endl;
        PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, "Missing HOME environment"});
        return;
    }

    AuthCredentials credentials;
    std::string credentialError;
    const std::string settingsPath = std::string(home) + "/.m2/settings.xml";
    if (!credentials.LoadFromM2SettingsXml(settingsPath, credentialError)) {
        std::cerr << "[download-worker] jobId=" << job.jobId << " auth load failed: " << credentialError
                  << std::endl;
        PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, credentialError});
        return;
    }

    std::cout << "[download-worker] jobId=" << job.jobId << " using m2 settings: " << settingsPath
              << std::endl;

    NexusClient client(std::move(credentials));
    std::string error;

    const auto ok = client.DownloadArtifactTree(
        job.repositoryUrl,
        job.componentName,
        job.version,
        job.buildType,
        job.targetDirectory,
        cancelAllRequested_,
        [this, &job](int percent, const std::string& message) {
            std::cout << "[download-worker] progress jobId=" << job.jobId << " percent=" << percent
                      << " message='" << message << "'" << std::endl;
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Progress, percent, message});
        },
        error);

    if (!ok) {
        if (cancelAllRequested_.load()) {
            std::cout << "[download-worker] cancelled jobId=" << job.jobId << std::endl;
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Cancelled, 0, "Cancelled"});
        } else {
            std::cerr << "[download-worker] failed jobId=" << job.jobId << " error='" << error << "'"
                      << std::endl;
            PushEvent({job.jobId, job.componentIndex, DownloadEventType::Failed, 0, error});
        }
        return;
    }

    std::cout << "[download-worker] completed jobId=" << job.jobId << std::endl;
    PushEvent({job.jobId, job.componentIndex, DownloadEventType::Completed, 100, "Completed"});
}

}  // namespace confy
