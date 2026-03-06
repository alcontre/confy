#include "ConfigLoader.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string &message)
{
   if (!condition) {
      std::cerr << "[config-loader-file-error] " << message << '\n';
      return false;
   }
   return true;
}

} // namespace

int main()
{
   namespace fs = std::filesystem;

   confy::ConfigLoader loader;

   const fs::path tempDir = fs::temp_directory_path() / "confy-tests";
   std::error_code ec;
   fs::create_directories(tempDir, ec);
   if (ec) {
      std::cerr << "[config-loader-file-error] Failed to create temp directory: " << ec.message() << '\n';
      return 1;
   }

   const fs::path emptyFile = tempDir / "empty-config.xml";
   {
      std::ofstream output(emptyFile);
   }

   const auto emptyResult = loader.LoadFromFile(emptyFile.string());
   if (!Check(!emptyResult.success, "Expected empty config file to fail"))
      return 1;
   if (!Check(emptyResult.errorMessage == "Root <Config> node not found.", "Unexpected empty file error: " + emptyResult.errorMessage))
      return 1;

   const auto missingResult = loader.LoadFromFile((tempDir / "missing-config.xml").string());
   if (!Check(!missingResult.success, "Expected missing config file to fail"))
      return 1;
   if (!Check(missingResult.errorMessage == "Could not read file.", "Unexpected read failure error: " + missingResult.errorMessage))
      return 1;

   fs::remove(emptyFile, ec);

   std::cout << "[config-loader-file-error] OK\n";
   return 0;
}