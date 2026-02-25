#include "DownloadWorkerQueue.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string &message)
{
   if (!condition) {
      std::cerr << "[download-worker-queue-test] " << message << '\n';
      return false;
   }
   return true;
}

confy::DownloadJob MakeSourceJob(std::uint64_t jobId, std::size_t componentIndex)
{
   confy::GitCloneJob job;
   job.jobId                = jobId;
   job.componentIndex       = componentIndex;
   job.componentName        = "component" + std::to_string(componentIndex);
   job.componentDisplayName = "Component " + std::to_string(componentIndex);
   job.repositoryUrl        = "https://example.invalid/repo.git";
   job.branchOrTag          = "main";
   job.targetDirectory      = "/tmp/confy-queue-test-" + std::to_string(jobId);
   job.shallow              = true;
   return confy::DownloadJob::FromSource(std::move(job));
}

} // namespace

int main()
{
   confy::DownloadWorkerQueue queue(1);

   queue.Submit(MakeSourceJob(1001, 0));
   queue.Submit(MakeSourceJob(1002, 0));
   queue.Submit(MakeSourceJob(1003, 1));

   queue.RequestCancelAll();

   std::vector<confy::DownloadEvent> events;
   confy::DownloadEvent event;
   while (queue.TryPopEvent(event)) {
      events.push_back(event);
   }

   if (!Check(events.size() == 3, "Expected 3 cancellation events after draining queued jobs")) {
      return 1;
   }

   std::vector<std::uint64_t> jobIds;
   for (const auto &queuedEvent : events) {
      if (!Check(queuedEvent.type == confy::DownloadEventType::Cancelled,
              "Expected event type Cancelled for drained queued jobs")) {
         return 1;
      }
      jobIds.push_back(queuedEvent.jobId);
   }

   std::sort(jobIds.begin(), jobIds.end());
   if (!Check(jobIds == std::vector<std::uint64_t>{1001, 1002, 1003},
           "Expected cancellation events for all submitted job IDs")) {
      return 1;
   }

   std::cout << "[download-worker-queue-test] OK\n";
   return 0;
}
