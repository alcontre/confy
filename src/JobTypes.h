#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace confy {

struct NexusDownloadJob {
    std::uint64_t jobId{0};
    std::size_t componentIndex{0};
    std::string componentName;
    std::string componentDisplayName;
    std::string repositoryUrl;
    std::string version;
    std::string buildType;
    std::string targetDirectory;
    std::vector<std::string> regexIncludes;
    std::vector<std::string> regexExcludes;
};

enum class DownloadEventType {
    Started,
    Progress,
    Completed,
    Failed,
    Cancelled,
};

struct DownloadEvent {
    std::uint64_t jobId{0};
    std::size_t componentIndex{0};
    DownloadEventType type{DownloadEventType::Progress};
    int percent{0};
    std::uint64_t downloadedBytes{0};
    std::string message;
};

}  // namespace confy
