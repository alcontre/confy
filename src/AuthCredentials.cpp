#include "AuthCredentials.h"

#include "rapidxml.hpp"

#include <fstream>
#include <iterator>
#include <vector>

namespace {

using rapidxml::xml_document;
using rapidxml::xml_node;

xml_node<>* FindChild(xml_node<>* parent, const char* name) {
    if (!parent) {
        return nullptr;
    }
    for (auto* child = parent->first_node(); child != nullptr; child = child->next_sibling()) {
        if (std::string(child->name()) == name) {
            return child;
        }
    }
    return nullptr;
}

std::string ValueOf(xml_node<>* parent, const char* childName) {
    if (auto* child = FindChild(parent, childName)) {
        return child->value();
    }
    return {};
}

std::string ReadAll(const std::string& filePath) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

}  // namespace

namespace confy {

bool AuthCredentials::LoadFromM2SettingsXml(const std::string& filePath, std::string& errorMessage) {
    credentialsByHostPort_.clear();

    const auto xml = ReadAll(filePath);
    if (xml.empty()) {
        errorMessage = "Unable to read m2 settings file: " + filePath;
        return false;
    }

    try {
        std::vector<char> buffer(xml.begin(), xml.end());
        buffer.push_back('\0');

        xml_document<> document;
        document.parse<0>(buffer.data());

        auto* settings = document.first_node("settings");
        if (!settings) {
            errorMessage = "Missing <settings> root in m2 settings.";
            return false;
        }

        auto* servers = FindChild(settings, "servers");
        if (!servers) {
            errorMessage = "Missing <servers> section in m2 settings.";
            return false;
        }

        for (auto* server = servers->first_node("server"); server != nullptr;
             server = server->next_sibling("server")) {
            const auto id = ValueOf(server, "id");
            const auto user = ValueOf(server, "username");
            const auto pass = ValueOf(server, "password");

            if (id.empty() || user.empty()) {
                continue;
            }

            credentialsByHostPort_[id] = ServerCredentials{user, pass};
        }

        if (credentialsByHostPort_.empty()) {
            errorMessage = "No usable <server> credentials found in m2 settings.";
            return false;
        }

        return true;
    } catch (const std::exception& ex) {
        errorMessage = std::string("Unable to parse m2 settings: ") + ex.what();
        return false;
    }
}

bool AuthCredentials::TryGetForHost(const std::string& hostPort, ServerCredentials& out) const {
    const auto it = credentialsByHostPort_.find(hostPort);
    if (it == credentialsByHostPort_.end()) {
        return false;
    }

    out = it->second;
    return true;
}

}  // namespace confy
