#include "BitbucketClient.h"

#include <doctest/doctest.h>

TEST_CASE("BitbucketClient parses supported repository URL formats")
{
   confy::BitbucketClient::RepoCoordinates repo;
   std::string error;

   // /scm repository URLs should map to the expected coordinates.
   REQUIRE(confy::BitbucketClient::ParseRepositoryUrl(
       "https://bitbucket.example.com/scm/OPS/confy-configs.git", repo, error));

   CHECK(repo.baseUrl == "https://bitbucket.example.com");
   CHECK(repo.hostPort == "bitbucket.example.com");
   CHECK(repo.projectKey == "OPS");
   CHECK(repo.repositorySlug == "confy-configs");

   // /projects repository URLs should map to the same coordinates.
   REQUIRE(confy::BitbucketClient::ParseRepositoryUrl(
       "https://bitbucket.example.com/projects/OPS/repos/confy-configs/browse", repo, error));

   CHECK(repo.baseUrl == "https://bitbucket.example.com");
   CHECK(repo.projectKey == "OPS");
   CHECK(repo.repositorySlug == "confy-configs");

   // Unsupported URL layouts should be rejected.
   CHECK_FALSE(confy::BitbucketClient::ParseRepositoryUrl(
       "https://bitbucket.example.com/OPS/confy-configs", repo, error));
}
