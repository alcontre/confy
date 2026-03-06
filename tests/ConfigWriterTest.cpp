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
   enabledArtifact.artifact.enabled       = true;
   enabledArtifact.artifact.url           = "https://repo.example.com/releases";
   enabledArtifact.artifact.version       = "1.2.3";
   enabledArtifact.artifact.buildType     = "Release";
   enabledArtifact.artifact.regexIncludes = {"\\.dll$"};

   confy::ComponentConfig disabledArtifact;
   disabledArtifact.name               = "optional_tooling";
   disabledArtifact.displayName        = "Optional Tooling";
   disabledArtifact.path               = "tooling";
   disabledArtifact.artifactPresent    = true;
   disabledArtifact.artifact.enabled   = false;
   disabledArtifact.artifact.url       = "https://repo.example.com/tooling";
   disabledArtifact.artifact.version   = "9.9.9";
   disabledArtifact.artifact.buildType = "Debug";

   confy::ComponentConfig enabledOnlySource;
   enabledOnlySource.name           = "enabled_only_source";
   enabledOnlySource.displayName    = "Enabled Only Source";
   enabledOnlySource.path           = "enabled-source";
   enabledOnlySource.sourcePresent  = true;
   enabledOnlySource.source.enabled = true;

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
   // Core config metadata and section presence should survive a write/read round-trip.
   CHECK(loaded.version == 42);
   CHECK(loaded.rootPath == "/tmp/confy");
   REQUIRE(loaded.components.size() == 4);
   CHECK(loaded.components[0].sourcePresent);
   CHECK(loaded.components[0].artifactPresent);
   CHECK(loaded.components[0].source.enabled);
   CHECK_FALSE(loaded.components[0].source.shallow);
   CHECK(loaded.components[0].artifact.enabled);
   CHECK(loaded.components[0].artifact.version == "1.2.3");
   CHECK(loaded.components[0].artifact.buildType == "Release");
   CHECK(loaded.components[1].artifactPresent);
   CHECK(loaded.components[2].sourcePresent);
   CHECK(loaded.components[2].source.enabled);
   CHECK_FALSE(loaded.components[2].artifactPresent);
   CHECK_FALSE(loaded.components[3].sourcePresent);
   CHECK_FALSE(loaded.components[3].artifactPresent);

   const auto summary = confy::BuildHumanReadableConfigSummary(model);
   // The human-readable summary should include enabled artifact entries and omit disabled ones.
   CHECK(summary.find("Core Library (core_lib)") != std::string::npos);
   CHECK(summary.find("version: 1.2.3") != std::string::npos);
   CHECK(summary.find("buildtype: Release") != std::string::npos);
   CHECK(summary.find("Optional Tooling") == std::string::npos);
}
