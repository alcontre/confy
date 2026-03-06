#include "NexusClient.h"

#include <doctest/doctest.h>

TEST_CASE("NexusClient builds curl auth payloads from credentials")
{
   const confy::ServerCredentials creds{"svc-user", "P@ss word/with:symbols"};
   const auto userPwd = confy::NexusClient::BuildCurlUserPwd(creds);

   // Username and password should be joined verbatim for CURLOPT_USERPWD.
   CHECK(userPwd == "svc-user:P@ss word/with:symbols");

   const confy::ServerCredentials emptyPassword{"svc-user", ""};

   // Empty passwords should still produce a trailing colon for curl.
   CHECK(confy::NexusClient::BuildCurlUserPwd(emptyPassword) == "svc-user:");
}
