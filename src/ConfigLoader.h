#pragma once

#include "ConfigModel.h"

#include <string>

namespace confy {

struct LoadResult {
    bool success{false};
    std::string errorMessage;
    ConfigModel config;
};

class ConfigLoader final {
public:
    LoadResult LoadFromFile(const std::string& filePath) const;
};

}  // namespace confy
