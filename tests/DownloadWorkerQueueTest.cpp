#include "DownloadWorkerQueue.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

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

#include <doctest/doctest.h>

TEST_CASE("DownloadWorkerQueue cancels queued jobs and emits cancellation events")
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

   // Cancelling all queued jobs should emit one cancellation event per submitted job.
   REQUIRE(events.size() == 3);

   std::vector<std::uint64_t> jobIds;
   for (const auto &queuedEvent : events) {
      // Each drained event should be marked as cancelled.
      CHECK(queuedEvent.type == confy::DownloadEventType::Cancelled);
      jobIds.push_back(queuedEvent.jobId);
   }

   std::sort(jobIds.begin(), jobIds.end());

   // The queue should report cancellation for every submitted job ID.
   CHECK(jobIds == std::vector<std::uint64_t>{1001, 1002, 1003});
}
