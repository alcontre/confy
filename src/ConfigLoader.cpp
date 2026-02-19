#include "ConfigLoader.h"

#include "rapidxml.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using rapidxml::xml_document;
using rapidxml::xml_node;

std::string ReadAllText(const std::string& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string ToLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool NameEqualsCI(const xml_node<>* node, const std::string& name) {
    if (!node) {
        return false;
    }
    return ToLower(node->name()) == ToLower(name);
}

xml_node<>* FindChildCI(xml_node<>* parent, const std::string& name) {
    if (!parent) {
        return nullptr;
    }

    for (auto* child = parent->first_node(); child != nullptr; child = child->next_sibling()) {
        if (NameEqualsCI(child, name)) {
            return child;
        }
    }

    return nullptr;
}

std::string GetChildValueCI(xml_node<>* parent, const std::string& name) {
    if (auto* child = FindChildCI(parent, name)) {
        return child->value();
    }
    return {};
}

bool HasChildCI(xml_node<>* parent, const std::string& name) {
    return FindChildCI(parent, name) != nullptr;
}

}  // namespace

namespace confy {

LoadResult ConfigLoader::LoadFromFile(const std::string& filePath) const {
    LoadResult result;

    const auto xml = ReadAllText(filePath);
    if (xml.empty()) {
        result.errorMessage = "Could not read file.";
        return result;
    }

    try {
        std::vector<char> xmlBuffer(xml.begin(), xml.end());
        xmlBuffer.push_back('\0');

        xml_document<> document;
        document.parse<0>(xmlBuffer.data());

        auto* root = document.first_node();
        if (!NameEqualsCI(root, "config")) {
            result.errorMessage = "Root <Config> node not found.";
            return result;
        }

        ConfigModel model;

        const auto versionText = GetChildValueCI(root, "version");
        if (!versionText.empty()) {
            model.version = std::stoi(versionText);
        }
        model.rootPath = GetChildValueCI(root, "path");

        auto* componentsNode = FindChildCI(root, "components");
        if (componentsNode) {
            for (auto* node = componentsNode->first_node(); node != nullptr; node = node->next_sibling()) {
                if (!NameEqualsCI(node, "component")) {
                    continue;
                }

                ComponentConfig component;
                component.name = GetChildValueCI(node, "name");
                component.displayName = GetChildValueCI(node, "displayname");
                component.path = GetChildValueCI(node, "path");

                auto* sourceNode = FindChildCI(node, "source");
                if (sourceNode) {
                    component.source.enabled = HasChildCI(sourceNode, "isenabled");
                    component.source.url = GetChildValueCI(sourceNode, "url");
                    component.source.branchOrTag = GetChildValueCI(sourceNode, "branchortag");
                    component.source.script = GetChildValueCI(sourceNode, "script");
                }

                auto* artifactNode = FindChildCI(node, "artifact");
                if (artifactNode) {
                    component.artifact.enabled = HasChildCI(artifactNode, "isenabled");
                    component.artifact.url = GetChildValueCI(artifactNode, "url");
                    component.artifact.version = GetChildValueCI(artifactNode, "version");
                    component.artifact.buildType = GetChildValueCI(artifactNode, "buildtype");
                    component.artifact.script = GetChildValueCI(artifactNode, "script");
                }

                model.components.push_back(std::move(component));
            }
        }

        result.success = true;
        result.config = std::move(model);
        return result;
    } catch (const rapidxml::parse_error& ex) {
        result.errorMessage = std::string("XML parse error: ") + ex.what();
        return result;
    } catch (const std::exception& ex) {
        result.errorMessage = std::string("Parse error: ") + ex.what();
        return result;
    }
}

}  // namespace confy
