#include "ConfigLoader.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string &message)
{
   if (!condition) {
      std::cerr << "[config-loader-path-macro] " << message << '\n';
      return false;
   }
   return true;
}

} // namespace

int main(int argc, char **argv)
{
   if (argc < 2) {
      std::cerr << "Usage: confy_config_loader_path_macro_test <config.xml>\n";
      return 2;
   }

   confy::ConfigLoader loader;
   const auto result = loader.LoadFromFile(argv[1]);

   if (!Check(result.success, "Expected parser success")) {
      std::cerr << "[config-loader-path-macro] error: " << result.errorMessage << '\n';
      return 1;
   }

   const auto &model = result.config;
   if (!Check(model.rootPath == "/tmp/confy-downloads", "Unexpected root path"))
      return 1;
   if (!Check(model.components.size() == 2, "Expected 2 components"))
      return 1;

   const auto &first = model.components[0];
   if (!Check(first.path == "/tmp/confy-downloads/componentA", "Failed to expand %PATH% in component path"))
      return 1;

   const auto &second = model.components[1];
   if (!Check(second.path == "componentB", "Component path without %PATH% should remain unchanged"))
      return 1;

   std::cout << "[config-loader-path-macro] OK\n";
   return 0;
}
