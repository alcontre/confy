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

struct GitCloneJob {
    std::uint64_t jobId{0};
    std::size_t componentIndex{0};
    std::string componentName;
    std::string componentDisplayName;
    std::string repositoryUrl;
    std::string branchOrTag;
    std::string targetDirectory;
    bool shallow{true};
};

enum class DownloadJobKind {
    NexusArtifact,
    GitSource,
};

struct DownloadJob {
    DownloadJobKind kind{DownloadJobKind::NexusArtifact};
    NexusDownloadJob artifact;
    GitCloneJob source;

    static DownloadJob FromArtifact(NexusDownloadJob job) {
        DownloadJob out;
        out.kind = DownloadJobKind::NexusArtifact;
        out.artifact = std::move(job);
        return out;
    }

    static DownloadJob FromSource(GitCloneJob job) {
        DownloadJob out;
        out.kind = DownloadJobKind::GitSource;
        out.source = std::move(job);
        return out;
    }

    std::uint64_t JobId() const {
        return kind == DownloadJobKind::NexusArtifact ? artifact.jobId : source.jobId;
    }

    std::size_t ComponentIndex() const {
        return kind == DownloadJobKind::NexusArtifact ? artifact.componentIndex : source.componentIndex;
    }
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
