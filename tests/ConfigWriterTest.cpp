#include "ConfigLoader.h"
#include "ConfigWriter.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[config-writer] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    confy::ConfigModel model;
    model.version = 42;
    model.rootPath = "/tmp/confy";

    confy::ComponentConfig enabledArtifact;
    enabledArtifact.name = "core_lib";
    enabledArtifact.displayName = "Core Library";
    enabledArtifact.path = "core";
    enabledArtifact.source.enabled = true;
    enabledArtifact.source.url = "https://example.com/core.git";
    enabledArtifact.source.branchOrTag = "main";
    enabledArtifact.source.shallow = false;
    enabledArtifact.artifact.enabled = true;
    enabledArtifact.artifact.url = "https://repo.example.com/releases";
    enabledArtifact.artifact.version = "1.2.3";
    enabledArtifact.artifact.buildType = "Release";
    enabledArtifact.artifact.regexIncludes = {"\\.dll$"};

    confy::ComponentConfig disabledArtifact;
    disabledArtifact.name = "optional_tooling";
    disabledArtifact.displayName = "Optional Tooling";
    disabledArtifact.path = "tooling";
    disabledArtifact.artifact.enabled = false;
    disabledArtifact.artifact.url = "https://repo.example.com/tooling";
    disabledArtifact.artifact.version = "9.9.9";
    disabledArtifact.artifact.buildType = "Debug";

    model.components.push_back(enabledArtifact);
    model.components.push_back(disabledArtifact);

    const auto outputPath = std::filesystem::temp_directory_path() / "confy_config_writer_test.xml";
    const auto saveResult = confy::SaveConfigToFile(model, outputPath.string());
    if (!Check(saveResult.success, "SaveConfigToFile should succeed")) {
        std::cerr << "[config-writer] save error: " << saveResult.errorMessage << '\n';
        return 1;
    }

    confy::ConfigLoader loader;
    const auto loadResult = loader.LoadFromFile(outputPath.string());
    if (!Check(loadResult.success, "Saved XML should reload successfully")) {
        std::cerr << "[config-writer] load error: " << loadResult.errorMessage << '\n';
        return 1;
    }

    const auto& loaded = loadResult.config;
    if (!Check(loaded.version == 42, "Version should round-trip")) return 1;
    if (!Check(loaded.rootPath == "/tmp/confy", "Root path should round-trip")) return 1;
    if (!Check(loaded.components.size() == 2, "Component count should round-trip")) return 1;
    if (!Check(loaded.components[0].source.enabled, "Source enabled should round-trip")) return 1;
    if (!Check(!loaded.components[0].source.shallow, "NoShallow should round-trip")) return 1;
    if (!Check(loaded.components[0].artifact.enabled, "Artifact enabled should round-trip")) return 1;
    if (!Check(loaded.components[0].artifact.version == "1.2.3", "Version should round-trip")) return 1;
    if (!Check(loaded.components[0].artifact.buildType == "Release", "Build type should round-trip")) return 1;

    const auto summary = confy::BuildHumanReadableConfigSummary(model);
    if (!Check(summary.find("Core Library (core_lib)") != std::string::npos,
               "Summary should include display and internal names")) {
        return 1;
    }
    if (!Check(summary.find("version: 1.2.3") != std::string::npos,
               "Summary should include selected version")) {
        return 1;
    }
    if (!Check(summary.find("buildtype: Release") != std::string::npos,
               "Summary should include selected build type")) {
        return 1;
    }
    if (!Check(summary.find("Optional Tooling") == std::string::npos,
               "Summary should exclude disabled artifact entries")) {
        return 1;
    }

    std::filesystem::remove(outputPath);

    std::cout << "[config-writer] OK\n";
    return 0;
}
