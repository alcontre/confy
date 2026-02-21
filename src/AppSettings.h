#pragma once

#include <memory>
#include <string>

class wxFileConfig;

namespace confy {

class AppSettings {
public:
    static void Initialize(const std::string& executableDir);
    static AppSettings& Get();

    std::string GetLastConfigPath() const;
    void SetLastConfigPath(const std::string& path);

private:
    explicit AppSettings(const std::string& executableDir);

    std::unique_ptr<wxFileConfig> config_;
};

}  // namespace confy
