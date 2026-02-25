#include "AppSettings.h"

#include <wx/fileconf.h>

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace confy {

namespace {
std::unique_ptr<AppSettings> s_instance;
} // namespace

void AppSettings::Initialize(const std::string &executableDir)
{
   s_instance = std::unique_ptr<AppSettings>(new AppSettings(executableDir));
}

AppSettings &AppSettings::Get()
{
   if (!s_instance) {
      throw std::logic_error("AppSettings::Initialize() must be called in App::OnInit() before using AppSettings::Get()");
   }
   return *s_instance;
}

AppSettings::AppSettings(const std::string &executableDir)
{
   const auto configFilePath = (std::filesystem::path(executableDir) / "confy.conf").string();
   config_                   = std::make_unique<wxFileConfig>(
       wxEmptyString,
       wxEmptyString,
       wxString(configFilePath),
       wxEmptyString,
       wxCONFIG_USE_LOCAL_FILE);
}

std::string AppSettings::GetLastConfigPath() const
{
   wxString value;
   config_->Read("/LastConfigPath", &value);
   return value.ToStdString();
}

void AppSettings::SetLastConfigPath(const std::string &path)
{
   config_->Write("/LastConfigPath", wxString(path));
   config_->Flush();
}

std::string AppSettings::GetXmlRepoUrl() const
{
   wxString value;
   config_->Read("/XmlRepoUrl", &value);
   return value.ToStdString();
}

void AppSettings::SetXmlRepoUrl(const std::string &url)
{
   config_->Write("/XmlRepoUrl", wxString(url));
   config_->Flush();
}

} // namespace confy
