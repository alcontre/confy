#include "ConfigLoader.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[parser-smoke] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: confy_parser_smoke <config.xml>\n";
        return 2;
    }

    confy::ConfigLoader loader;
    const auto result = loader.LoadFromFile(argv[1]);

    if (!Check(result.success, "Expected parser success")) {
        std::cerr << "[parser-smoke] error: " << result.errorMessage << '\n';
        return 1;
    }

    const auto& model = result.config;

    if (!Check(model.version == 9, "Expected version 9")) return 1;
    if (!Check(model.rootPath == "/tmp/confy-downloads", "Unexpected root path")) return 1;
    if (!Check(model.components.size() == 12, "Expected 12 components")) return 1;

    const auto& first = model.components[0];
    if (!Check(first.name == "my_name", "First component name mismatch")) return 1;
    if (!Check(first.displayName == "My Name", "First display name mismatch")) return 1;
    if (!Check(first.source.enabled, "First source should be enabled")) return 1;
    if (!Check(first.artifact.enabled, "First artifact should be enabled")) return 1;
    if (!Check(first.source.branchOrTag == "master", "First source branch mismatch")) return 1;
    if (!Check(first.artifact.buildType == "Debug", "First artifact buildtype mismatch")) return 1;

    const auto& second = model.components[1];
    if (!Check(second.name == "only_source", "Second component name mismatch")) return 1;
    if (!Check(second.source.enabled, "Second source should be enabled")) return 1;
    if (!Check(!second.artifact.enabled, "Second artifact should be disabled")) return 1;

    const auto& last = model.components[11];
    if (!Check(last.name == "legacy_adapter", "Last component name mismatch")) return 1;
    if (!Check(last.source.enabled, "Last source should be enabled")) return 1;
    if (!Check(!last.artifact.enabled, "Last artifact should be disabled")) return 1;

    std::cout << "[parser-smoke] OK\n";
    return 0;
}
