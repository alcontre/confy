#include "ConfigLoader.h"

#include <doctest/doctest.h>

TEST_CASE("ConfigLoader parses representative config XML")
{
   static constexpr char kSampleConfigXml[] = R"xml(<Config>
    <version>9</version>
    <path>/tmp/confy-downloads</path>
    <components>
        <Component>
            <name>my_name</name>
            <DisplayName>My Name is very very very very very very long</DisplayName>
            <Path>%PATH%/componentA</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/myrepo.git</url>
                <BranchOrTag>master</BranchOrTag>
                <Script>post_source.sh</Script>
            </Source>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <RelativePath>products</RelativePath>
                <version>myProduct</version>
                <buildtype>Debug</buildtype>
                <regex-include>
                    <regex>\.dll$</regex>
                    <regex>^bin/</regex>
                </regex-include>
                <regex-exclude>
                    <regex>/tests?/</regex>
                </regex-exclude>
                <script>post_artifact.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>only_source</name>
            <DisplayName>Only Source</DisplayName>
            <Path>componentB</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/another-repo.git</url>
                <BranchOrTag>release/1.0</BranchOrTag>
                <NoShallow/>
                <Script></Script>
            </Source>
        </Component>
        <Component>
            <name>artifact_only_01</name>
            <DisplayName>Artifact Only 01</DisplayName>
            <Path>componentC</Path>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>2.1.0</version>
                <buildtype>Release</buildtype>
                <script>post_artifact_01.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>platform_core</name>
            <DisplayName>Platform Core</DisplayName>
            <Path>componentD</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/platform-core.git</url>
                <BranchOrTag>main</BranchOrTag>
                <Script>post_platform_core.sh</Script>
            </Source>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>9.0.0</version>
                <buildtype>Debug</buildtype>
                <script>post_platform_core_artifact.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>ui_module</name>
            <DisplayName>UI Module</DisplayName>
            <Path>componentE</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/ui-module.git</url>
                <BranchOrTag>feature/new-dashboard</BranchOrTag>
                <Script></Script>
            </Source>
        </Component>
        <Component>
            <name>service_bus</name>
            <DisplayName>Service Bus</DisplayName>
            <Path>componentF</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/service-bus.git</url>
                <BranchOrTag>release/2.3</BranchOrTag>
                <Script>post_service_bus.sh</Script>
            </Source>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>2.3.7</version>
                <buildtype>RelWithDebInfo</buildtype>
                <script>post_service_bus_artifact.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>agent_tools</name>
            <DisplayName>Agent Tools</DisplayName>
            <Path>componentG</Path>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>1.5.4</version>
                <buildtype>Release</buildtype>
                <script></script>
            </Artifact>
        </Component>
        <Component>
            <name>integration_tests</name>
            <DisplayName>Integration Tests</DisplayName>
            <Path>componentH</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/integration-tests.git</url>
                <BranchOrTag>develop</BranchOrTag>
                <Script>prep_tests.sh</Script>
            </Source>
        </Component>
        <Component>
            <name>benchmark_suite</name>
            <DisplayName>Benchmark Suite</DisplayName>
            <Path>componentI</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/benchmark-suite.git</url>
                <BranchOrTag>tag/v3.0.1</BranchOrTag>
                <Script></Script>
            </Source>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>3.0.1</version>
                <buildtype>Release</buildtype>
                <script>post_benchmark.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>docs_bundle</name>
            <DisplayName>Docs Bundle</DisplayName>
            <Path>componentJ</Path>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>2026.02</version>
                <buildtype>Release</buildtype>
                <script>post_docs.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>sim_engine</name>
            <DisplayName>Simulation Engine</DisplayName>
            <Path>componentK</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/sim-engine.git</url>
                <BranchOrTag>staging</BranchOrTag>
                <Script>post_sim.sh</Script>
            </Source>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>5.4.0</version>
                <buildtype>Debug</buildtype>
                <script>post_sim_artifact.sh</script>
            </Artifact>
        </Component>
        <Component>
            <name>legacy_adapter</name>
            <DisplayName>Legacy Adapter</DisplayName>
            <Path>componentL</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/legacy-adapter.git</url>
                <BranchOrTag>maintenance/1.x</BranchOrTag>
                <Script></Script>
            </Source>
        </Component>
    </components>
</Config>
)xml";

   confy::ConfigLoader loader;
   const auto result = loader.LoadFromString(kSampleConfigXml);

   // The representative sample config should parse successfully.
   REQUIRE(result.success);

   const auto &model = result.config;
   // Top-level config metadata should round-trip from the embedded XML.
   CHECK(model.version == 9);
   CHECK(model.rootPath == "/tmp/confy-downloads");
   REQUIRE(model.components.size() == 12);

   const auto &first = model.components[0];
   // The first component should preserve full source and artifact details.
   CHECK(first.name == "my_name");
   CHECK(first.displayName == "My Name is very very very very very very long");
   CHECK(first.path == "/tmp/confy-downloads/componentA");
   CHECK(first.sourcePresent);
   CHECK(first.artifactPresent);
   CHECK(first.source.enabled);
   CHECK(first.artifact.enabled);
   CHECK(first.source.branchOrTag == "master");
   CHECK(first.artifact.relativePath == "products");
   CHECK(first.artifact.buildType == "Debug");
   REQUIRE(first.artifact.regexIncludes.size() == 2);
   CHECK(first.artifact.regexIncludes[0] == "\\.dll$");
   CHECK(first.artifact.regexIncludes[1] == "^bin/");
   REQUIRE(first.artifact.regexExcludes.size() == 1);
   CHECK(first.artifact.regexExcludes[0] == "/tests?/");

   const auto &second = model.components[1];
   // The second component should remain source-only and honor NoShallow.
   CHECK(second.name == "only_source");
   CHECK(second.sourcePresent);
   CHECK(!second.artifactPresent);
   CHECK(second.source.enabled);
   CHECK(!second.source.shallow);

   const auto &last = model.components[11];
   // The last component should remain source-only and enabled.
   CHECK(last.name == "legacy_adapter");
   CHECK(last.sourcePresent);
   CHECK(!last.artifactPresent);
   CHECK(last.source.enabled);
}
