#pragma once

#include "JobTypes.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace confy {

class DownloadWorkerQueue final {
public:
    explicit DownloadWorkerQueue(std::size_t workerCount);
    ~DownloadWorkerQueue();

    void Start();
    void Stop();
    void Submit(NexusDownloadJob job);
    void RequestCancelAll();
    bool TryPopEvent(DownloadEvent& outEvent);

private:
    void WorkerLoop();
    void PushEvent(DownloadEvent event);
    void ProcessJob(const NexusDownloadJob& job);

    std::size_t workerCount_{0};
    std::vector<std::thread> workers_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::queue<NexusDownloadJob> pendingJobs_;

    std::mutex eventMutex_;
    std::queue<DownloadEvent> events_;

    bool started_{false};
    bool stopping_{false};
    std::atomic<bool> cancelAllRequested_{false};
};

}  // namespace confy
