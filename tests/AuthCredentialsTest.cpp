#include "AuthCredentials.h"

#include <doctest/doctest.h>

TEST_CASE("AuthCredentials loads and looks up Maven server credentials")
{
   const std::string validXml = R"XML(<settings>
  <servers>
    <server>
      <id>localhost:8081</id>
      <username>aa</username>
      <password>bb</password>
    </server>
    <server>
      <id>example.org:443</id>
      <username>alice</username>
      <password>secret</password>
    </server>
    <server>
      <id>bitbucket.internal:7990</id>
      <password>token-only-value</password>
    </server>
  </servers>
</settings>
)XML";

   const std::string invalidXml = R"XML(<not-settings>
  <servers>
    <server>
      <id>localhost:8081</id>
      <username>aa</username>
    </server>
  </servers>
</not-settings>
)XML";

   confy::AuthCredentials auth;
   std::string error;

   // Valid Maven settings XML should load into the credential store.
   REQUIRE(auth.LoadFromM2SettingsXmlString(validXml, error));

   confy::ServerCredentials creds;
   // Host lookup should return the expected username and password.
   REQUIRE(auth.TryGetForHost("localhost:8081", creds));
   CHECK(creds.username == "aa");
   CHECK(creds.password == "bb");

   // Unknown hosts should not resolve to credentials.
   CHECK_FALSE(auth.TryGetForHost("missing-host:1234", creds));

   // Token-only entries should keep an empty username and preserve the password field.
   REQUIRE(auth.TryGetByServerId("bitbucket.internal:7990", creds));
   CHECK(creds.username.empty());
   CHECK(creds.password == "token-only-value");

   confy::AuthCredentials badAuth;
   std::string badError;

   // Invalid settings XML should fail with a non-empty parse error.
   CHECK_FALSE(badAuth.LoadFromM2SettingsXmlString(invalidXml, badError));
   CHECK_FALSE(badError.empty());
}
