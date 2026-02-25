#include "BitbucketClient.h"

#include <iostream>
#include <string>

namespace {

bool Check(bool condition, const std::string &message)
{
   if (!condition) {
      std::cerr << "[bitbucket-client-test] " << message << '\n';
      return false;
   }
   return true;
}

} // namespace

int main()
{
   confy::BitbucketClient::RepoCoordinates repo;
   std::string error;
   if (!Check(
           confy::BitbucketClient::ParseRepositoryUrl("https://bitbucket.example.com/scm/OPS/confy-configs.git",
               repo,
               error),
           "Expected /scm URL to parse")) {
      return 1;
   }

   if (!Check(repo.baseUrl == "https://bitbucket.example.com", "Unexpected baseUrl from /scm URL")) {
      return 1;
   }
   if (!Check(repo.hostPort == "bitbucket.example.com", "Unexpected hostPort from /scm URL")) {
      return 1;
   }
   if (!Check(repo.projectKey == "OPS", "Unexpected projectKey from /scm URL")) {
      return 1;
   }
   if (!Check(repo.repositorySlug == "confy-configs", "Unexpected repository slug from /scm URL")) {
      return 1;
   }

   if (!Check(confy::BitbucketClient::ParseRepositoryUrl(
                  "https://bitbucket.example.com/projects/OPS/repos/confy-configs/browse", repo, error),
           "Expected /projects URL to parse")) {
      return 1;
   }

   if (!Check(repo.baseUrl == "https://bitbucket.example.com", "Unexpected baseUrl from /projects URL")) {
      return 1;
   }
   if (!Check(repo.projectKey == "OPS", "Unexpected projectKey from /projects URL")) {
      return 1;
   }
   if (!Check(repo.repositorySlug == "confy-configs", "Unexpected repository slug from /projects URL")) {
      return 1;
   }

   if (!Check(!confy::BitbucketClient::ParseRepositoryUrl("https://bitbucket.example.com/OPS/confy-configs", repo,
                  error),
           "Expected unsupported URL to fail")) {
      return 1;
   }

   std::cout << "[bitbucket-client-test] OK\n";
   return 0;
}
