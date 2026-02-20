#include "NexusClient.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[nexus-client-path-segment-test] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const std::vector<std::string> directories{
        "component-a/1.0.0/",
        "component-a/2.0.0/",
        "component-a/2.0.0/Debug/",
        "component-a/2.0.0/Release/",
        "component-b/0.1.0/",
    };

    const auto versions =
        confy::NexusClient::ExtractImmediateChildDirectories(directories, "component-a/");
    if (!Check(versions.size() == 2, "Expected two unique versions for component-a")) {
        return 1;
    }
    if (!Check(versions[0] == "1.0.0" && versions[1] == "2.0.0",
               "Expected sorted versions [1.0.0, 2.0.0]")) {
        return 1;
    }

    const auto buildTypes =
        confy::NexusClient::ExtractImmediateChildDirectories(directories, "component-a/2.0.0/");
    if (!Check(buildTypes.size() == 2, "Expected two build types for component-a/2.0.0")) {
        return 1;
    }
    if (!Check(buildTypes[0] == "Debug" && buildTypes[1] == "Release",
               "Expected sorted build types [Debug, Release]")) {
        return 1;
    }

    std::cout << "[nexus-client-path-segment-test] OK\n";
    return 0;
}
