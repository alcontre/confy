#include "ConfigLoader.h"

#include <doctest/doctest.h>

TEST_CASE("ConfigLoader expands %PATH% in component paths")
{
   static constexpr char kPathMacroConfigXml[] = R"xml(<Config>
    <version>9</version>
    <path>/tmp/confy-downloads</path>
    <components>
        <Component>
            <name>my_name</name>
            <DisplayName>My Name</DisplayName>
            <Path>%PATH%/componentA</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/myrepo.git</url>
                <BranchOrTag>master</BranchOrTag>
            </Source>
        </Component>
        <Component>
            <name>only_source</name>
            <DisplayName>Only Source</DisplayName>
            <Path>componentB</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/another-repo.git</url>
                <BranchOrTag>release/1.0</BranchOrTag>
            </Source>
        </Component>
    </components>
</Config>
)xml";

   confy::ConfigLoader loader;
   const auto result = loader.LoadFromString(kPathMacroConfigXml);

    // Path macro expansion input should parse successfully.
   REQUIRE(result.success);

   const auto &model = result.config;
    // The config root path and component count should come from the embedded XML.
   CHECK(model.rootPath == "/tmp/confy-downloads");
   REQUIRE(model.components.size() == 2);

   const auto &first = model.components[0];
    // %PATH% should expand in component paths while preserving section presence.
   CHECK(first.path == "/tmp/confy-downloads/componentA");
   CHECK(first.sourcePresent);
   CHECK_FALSE(first.artifactPresent);

   const auto &second = model.components[1];
    // Paths without %PATH% should be left unchanged.
   CHECK(second.path == "componentB");
   CHECK(second.sourcePresent);
   CHECK_FALSE(second.artifactPresent);
}
