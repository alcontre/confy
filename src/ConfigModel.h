#pragma once

#include <string>
#include <vector>

namespace confy {

struct SourceConfig
{
   bool enabled{false};
   std::string url;
   std::string branchOrTag;
   bool shallow{true};
   std::string script;
};

inline bool operator==(const SourceConfig &lhs, const SourceConfig &rhs)
{
   return lhs.enabled == rhs.enabled && lhs.url == rhs.url && lhs.branchOrTag == rhs.branchOrTag && lhs.shallow == rhs.shallow && lhs.script == rhs.script;
}

struct ArtifactConfig
{
   bool enabled{false};
   std::string url;
   std::string version;
   std::string buildType;
   std::string script;
   std::vector<std::string> regexIncludes;
   std::vector<std::string> regexExcludes;
};

inline bool operator==(const ArtifactConfig &lhs, const ArtifactConfig &rhs)
{
   return lhs.enabled == rhs.enabled && lhs.url == rhs.url && lhs.version == rhs.version && lhs.buildType == rhs.buildType && lhs.script == rhs.script && lhs.regexIncludes == rhs.regexIncludes && lhs.regexExcludes == rhs.regexExcludes;
}

struct ComponentConfig
{
   std::string name;
   std::string displayName;
   std::string path;
   bool sourcePresent{false};
   bool artifactPresent{false};
   SourceConfig source;
   ArtifactConfig artifact;
};

inline bool operator==(const ComponentConfig &lhs, const ComponentConfig &rhs)
{
   return lhs.name == rhs.name && lhs.displayName == rhs.displayName && lhs.path == rhs.path && lhs.sourcePresent == rhs.sourcePresent && lhs.artifactPresent == rhs.artifactPresent && lhs.source == rhs.source && lhs.artifact == rhs.artifact;
}

struct ConfigModel
{
   int version{0};
   std::string rootPath;
   std::vector<ComponentConfig> components;
};

inline bool operator==(const ConfigModel &lhs, const ConfigModel &rhs)
{
   return lhs.version == rhs.version && lhs.rootPath == rhs.rootPath && lhs.components == rhs.components;
}

} // namespace confy
