#include "ConfigLoader.h"

#include <doctest/doctest.h>

TEST_CASE("ConfigLoader reports malformed or unreadable input")
{
   confy::ConfigLoader loader;

   SUBCASE("empty XML text")
   {
      const auto result = loader.LoadFromString("");

      // Empty XML text should fail because no Config root node exists.
      CHECK_FALSE(result.success);
      CHECK(result.errorMessage == "Root <Config> node not found.");
   }

   SUBCASE("missing file")
   {
      const auto result = loader.LoadFromFile("/definitely/missing/confy-config.xml");

      // Missing files should report a file-read failure before parsing starts.
      CHECK_FALSE(result.success);
      CHECK(result.errorMessage == "Could not read file.");
   }
}