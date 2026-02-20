#include "ConfigLoader.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: confy_config_loader_regex_validation_test <config.xml>\n";
        return 2;
    }

    confy::ConfigLoader loader;
    const auto result = loader.LoadFromFile(argv[1]);

    if (result.success) {
        std::cerr << "[config-loader-regex-validation] Expected parser failure for invalid regex\n";
        return 1;
    }

    if (result.errorMessage.find("Invalid regex") == std::string::npos) {
        std::cerr << "[config-loader-regex-validation] Unexpected error: " << result.errorMessage << '\n';
        return 1;
    }

    std::cout << "[config-loader-regex-validation] OK\n";
    return 0;
}
