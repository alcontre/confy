#include "NexusClient.h"

#include <vector>

#include <doctest/doctest.h>

TEST_CASE("NexusClient extracts immediate child directories")
{
   const std::vector<std::string> directories{
       "component-a/1.0.0/",
       "component-a/2.0.0/",
       "component-a/2.0.0/Debug/",
       "component-a/2.0.0/Release/",
       "component-b/0.1.0/",
   };

   const auto versions =
       confy::NexusClient::ExtractImmediateChildDirectories(directories, "component-a/");

   // Version extraction should deduplicate and sort immediate child directories.
   REQUIRE(versions.size() == 2);
   CHECK(versions[0] == "1.0.0");
   CHECK(versions[1] == "2.0.0");

   const auto buildTypes =
       confy::NexusClient::ExtractImmediateChildDirectories(directories, "component-a/2.0.0/");

   // Build type extraction should expose only the next directory level.
   REQUIRE(buildTypes.size() == 2);
   CHECK(buildTypes[0] == "Debug");
   CHECK(buildTypes[1] == "Release");
}
