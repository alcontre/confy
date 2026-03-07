#include "ConfigWriter.h"
#include "ConfigLoader.h"

#include <doctest/doctest.h>

TEST_CASE("ConfigWriter round-trips configuration and summary output")
{
   confy::ConfigModel model;
   model.version  = 42;
   model.rootPath = "/tmp/confy";

   confy::ComponentConfig enabledArtifact;
   enabledArtifact.name                   = "core_lib";
   enabledArtifact.displayName            = "Core Library";
   enabledArtifact.path                   = "core";
   enabledArtifact.sourcePresent          = true;
   enabledArtifact.artifactPresent        = true;
   enabledArtifact.source.enabled         = true;
   enabledArtifact.source.url             = "https://example.com/core.git";
   enabledArtifact.source.branchOrTag     = "main";
   enabledArtifact.source.shallow         = false;
   enabledArtifact.source.script          = "git submodule update --init --recursive";
   enabledArtifact.artifact.enabled       = true;
   enabledArtifact.artifact.url           = "https://repo.example.com/releases";
   enabledArtifact.artifact.version       = "1.2.3";
   enabledArtifact.artifact.buildType     = "Release";
   enabledArtifact.artifact.script        = "cmake -S . -B build && cmake --build build";
   enabledArtifact.artifact.regexIncludes = {"\\.dll$"};
   enabledArtifact.artifact.regexExcludes = {".*tests.*", ".*debug.*"};

   confy::ComponentConfig disabledArtifact;
   disabledArtifact.name                   = "optional_tooling";
   disabledArtifact.displayName            = "Optional Tooling";
   disabledArtifact.path                   = "tooling";
   disabledArtifact.artifactPresent        = true;
   disabledArtifact.artifact.enabled       = false;
   disabledArtifact.artifact.url           = "https://repo.example.com/tooling";
   disabledArtifact.artifact.version       = "9.9.9";
   disabledArtifact.artifact.buildType     = "Debug";
   disabledArtifact.artifact.script        = "echo tooling";
   disabledArtifact.artifact.regexIncludes = {".*tooling.*"};
   disabledArtifact.artifact.regexExcludes = {".*experimental.*"};

   confy::ComponentConfig enabledOnlySource;
   enabledOnlySource.name               = "enabled_only_source";
   enabledOnlySource.displayName        = "Enabled Only Source";
   enabledOnlySource.path               = "enabled-source";
   enabledOnlySource.sourcePresent      = true;
   enabledOnlySource.source.enabled     = true;
   enabledOnlySource.source.url         = "https://example.com/only-source.git";
   enabledOnlySource.source.branchOrTag = "release/2026.03";
   enabledOnlySource.source.script      = "./bootstrap.sh";

   confy::ComponentConfig noSections;
   noSections.name        = "no_sections";
   noSections.displayName = "No Sections";
   noSections.path        = "none";

   model.components.push_back(enabledArtifact);
   model.components.push_back(disabledArtifact);
   model.components.push_back(enabledOnlySource);
   model.components.push_back(noSections);

   const auto xml = confy::SaveConfigToString(model);
   // Serializing a populated config should produce non-empty XML.
   CHECK_FALSE(xml.empty());

   confy::ConfigLoader loader;
   const auto loadResult = loader.LoadFromString(xml);
   // Writer output should be valid loader input.
   REQUIRE(loadResult.success);

   const auto &loaded = loadResult.config;
   CHECK(loaded == model);

   const auto savedAgain = confy::SaveConfigToString(loaded);
   CHECK(savedAgain == xml);

   const auto summary = confy::BuildHumanReadableConfigSummary(model);
   // The human-readable summary should include enabled artifact entries and omit disabled ones.
   CHECK(summary.find("Core Library (core_lib)") != std::string::npos);
   CHECK(summary.find("version: 1.2.3") != std::string::npos);
   CHECK(summary.find("buildtype: Release") != std::string::npos);
   CHECK(summary.find("Optional Tooling") == std::string::npos);

   const auto loadedSummary = confy::BuildHumanReadableConfigSummary(loaded);
   CHECK(loadedSummary == summary);
}
