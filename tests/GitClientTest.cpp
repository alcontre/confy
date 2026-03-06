#include "GitClient.h"

#include <doctest/doctest.h>

TEST_CASE("GitClient extracts hosts and parses ls-remote refs")
{
   std::string host;

   // HTTPS repository URLs should yield the host and optional port segment.
   REQUIRE(confy::GitClient::ExtractHostPort("https://bitbucket.example.com/scm/prj/repo.git", host));
   CHECK(host == "bitbucket.example.com");

   const std::string refsOutput =
       "111111\trefs/heads/main\n"
       "222222\trefs/heads/release/1.0\n"
       "333333\trefs/tags/v1.0.0\n"
       "444444\trefs/tags/v1.0.0^{}\n"
       "555555\trefs/tags/v2.0.0\n";

   const auto refs = confy::GitClient::ParseLsRemoteRefs(refsOutput);

   // ls-remote parsing should deduplicate peeled tags and sort branch/tag names.
   REQUIRE(refs.size() == 4);
   CHECK(refs[0] == "main");
   CHECK(refs[1] == "release/1.0");
   CHECK(refs[2] == "v1.0.0");
   CHECK(refs[3] == "v2.0.0");
}
