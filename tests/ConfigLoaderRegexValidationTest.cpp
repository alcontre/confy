#include "ConfigLoader.h"

#include <doctest/doctest.h>

TEST_CASE("ConfigLoader rejects invalid artifact regex filters")
{
   static constexpr char kInvalidRegexConfigXml[] = R"xml(<Config>
    <version>1</version>
    <path>/tmp/confy-downloads</path>
    <components>
        <Component>
            <name>bad_regex_component</name>
            <DisplayName>Bad Regex Component</DisplayName>
            <Path>componentBad</Path>
            <Artifact>
                <IsEnabled/>
                <url>http://localhost:8081/#browse/browse:raw-asdf-hosted</url>
                <version>myProduct</version>
                <buildtype>Debug</buildtype>
                <regex-include>
                    <regex>[abc</regex>
                </regex-include>
            </Artifact>
        </Component>
    </components>
</Config>
)xml";

   confy::ConfigLoader loader;
   const auto result = loader.LoadFromString(kInvalidRegexConfigXml);

   // Invalid regex filters should fail validation with a regex-specific error.
   CHECK_FALSE(result.success);
   CHECK(result.errorMessage.find("Invalid regex") != std::string::npos);
}
