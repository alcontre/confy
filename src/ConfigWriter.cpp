#include "ConfigWriter.h"

#include <fstream>
#include <sstream>

namespace {

bool HasSource(const confy::ComponentConfig &component)
{
   return !component.source.url.empty() || !component.source.branchOrTag.empty() ||
          !component.source.script.empty() || component.source.enabled;
}

bool HasArtifact(const confy::ComponentConfig &component)
{
   return !component.artifact.url.empty() || !component.artifact.version.empty() ||
          !component.artifact.buildType.empty() || !component.artifact.script.empty() ||
          !component.artifact.regexIncludes.empty() || !component.artifact.regexExcludes.empty() ||
          component.artifact.enabled;
}

std::string EscapeXml(const std::string &value)
{
   std::string escaped;
   escaped.reserve(value.size());

   for (const char c : value) {
      switch (c) {
         case '&':
            escaped += "&amp;";
            break;
         case '<':
            escaped += "&lt;";
            break;
         case '>':
            escaped += "&gt;";
            break;
         case '\"':
            escaped += "&quot;";
            break;
         case '\'':
            escaped += "&apos;";
            break;
         default:
            escaped.push_back(c);
            break;
      }
   }

   return escaped;
}

void WriteTag(std::ostringstream &output,
    const std::string &indent,
    const std::string &tag,
    const std::string &value)
{
   output << indent << '<' << tag << '>' << EscapeXml(value) << "</" << tag << ">\n";
}

} // namespace

namespace confy {

SaveConfigResult SaveConfigToFile(const ConfigModel &config, const std::string &filePath)
{
   SaveConfigResult result;

   std::ostringstream xml;
   xml << "<Config>\n";
   xml << "    <version>" << config.version << "</version>\n";
   WriteTag(xml, "    ", "path", config.rootPath);
   xml << "    <components>\n";

   for (const auto &component : config.components) {
      xml << "        <Component>\n";
      WriteTag(xml, "            ", "name", component.name);
      WriteTag(xml, "            ", "DisplayName", component.displayName);
      WriteTag(xml, "            ", "Path", component.path);

      if (HasSource(component)) {
         xml << "            <Source>\n";
         if (component.source.enabled) {
            xml << "                <IsEnabled/>\n";
         }
         WriteTag(xml, "                ", "url", component.source.url);
         WriteTag(xml, "                ", "BranchOrTag", component.source.branchOrTag);
         if (!component.source.shallow) {
            xml << "                <NoShallow/>\n";
         }
         WriteTag(xml, "                ", "Script", component.source.script);
         xml << "            </Source>\n";
      }

      if (HasArtifact(component)) {
         xml << "            <Artifact>\n";
         if (component.artifact.enabled) {
            xml << "                <IsEnabled/>\n";
         }
         WriteTag(xml, "                ", "url", component.artifact.url);
         WriteTag(xml, "                ", "version", component.artifact.version);
         WriteTag(xml, "                ", "buildtype", component.artifact.buildType);

         if (!component.artifact.regexIncludes.empty()) {
            xml << "                <regex-include>\n";
            for (const auto &regex : component.artifact.regexIncludes) {
               WriteTag(xml, "                    ", "regex", regex);
            }
            xml << "                </regex-include>\n";
         }

         if (!component.artifact.regexExcludes.empty()) {
            xml << "                <regex-exclude>\n";
            for (const auto &regex : component.artifact.regexExcludes) {
               WriteTag(xml, "                    ", "regex", regex);
            }
            xml << "                </regex-exclude>\n";
         }

         WriteTag(xml, "                ", "script", component.artifact.script);
         xml << "            </Artifact>\n";
      }

      xml << "        </Component>\n";
   }

   xml << "    </components>\n";
   xml << "</Config>\n";

   std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
   if (!output) {
      result.errorMessage = "Could not open output file for writing.";
      return result;
   }

   output << xml.str();
   if (!output.good()) {
      result.errorMessage = "Failed to write XML to output file.";
      return result;
   }

   result.success = true;
   return result;
}

std::string BuildHumanReadableConfigSummary(const ConfigModel &config)
{
   std::ostringstream summary;
   summary << "Confy configuration summary\n";
   summary << "===========================\n";

   std::size_t lineCount = 0;
   for (const auto &component : config.components) {
      if (!HasArtifact(component) || !component.artifact.enabled) {
         continue;
      }

      const auto version   = component.artifact.version.empty() ? "(none)" : component.artifact.version;
      const auto buildType = component.artifact.buildType.empty() ? "(none)" : component.artifact.buildType;
      summary << "- " << component.displayName << " (" << component.name << ")"
              << " | version: " << version
              << " | buildtype: " << buildType << '\n';
      ++lineCount;
   }

   if (lineCount == 0) {
      return {};
   }

   return summary.str();
}

} // namespace confy
