#pragma once

#include <string>
#include <vector>

namespace confy {

struct SourceConfig {
    bool enabled{false};
    std::string url;
    std::string branchOrTag;
    std::string script;
};

struct ArtifactConfig {
    bool enabled{false};
    std::string url;
    std::string version;
    std::string buildType;
    std::string script;
};

struct ComponentConfig {
    std::string name;
    std::string displayName;
    std::string path;
    SourceConfig source;
    ArtifactConfig artifact;
};

struct ConfigModel {
    int version{0};
    std::string rootPath;
    std::vector<ComponentConfig> components;
};

}  // namespace confy
