#pragma once

#include "ConfigModel.h"

#include <string>

namespace confy {

struct SaveConfigResult
{
   bool success{false};
   std::string errorMessage;
};

std::string SaveConfigToString(const ConfigModel &config);

SaveConfigResult SaveConfigToFile(const ConfigModel &config, const std::string &filePath);

std::string BuildHumanReadableConfigSummary(const ConfigModel &config);

} // namespace confy
