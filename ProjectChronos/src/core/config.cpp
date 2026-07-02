#include "config.h"
#include <filesystem>
#include <sstream>
#include <algorithm>

Config::Config() {
    // Create config directory
    std::filesystem::create_directory(configDir);
}

bool Config::Load(const std::string& profile) {
    std::string path = GetPath(profile);
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    currentProfile = profile;
    std::string line;
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        if (value == "true" || value == "false") {
            boolValues[key] = (value == "true");
        } else if (value.find('.') != std::string::npos) {
            floatValues[key] = std::stof(value);
        } else if (value.find_first_of("0123456789") == 0) {
            intValues[key] = std::stoi(value);
        } else {
            stringValues[key] = value;
        }
    }
    
    return true;
}

bool Config::Save(const std::string& profile) {
    std::string path = GetPath(profile);
    std::ofstream file(path);
    if (!file.is_open()) return false;
    
    currentProfile = profile;
    
    file << "# Project Chronos Config\n";
    file << "# Generated: " << __DATE__ << "\n\n";
    
    // Write bools
    for (auto& [key, val] : boolValues)
        file << key << " = " << (val ? "true" : "false") << "\n";
    
    // Write ints
    for (auto& [key, val] : intValues)
        file << key << " = " << val << "\n";
    
    // Write floats
    for (auto& [key, val] : floatValues)
        file << key << " = " << val << "\n";
    
    // Write strings
    for (auto& [key, val] : stringValues)
        file << key << " = " << val << "\n";
    
    return true;
}

bool Config::Delete(const std::string& profile) {
    return std::filesystem::remove(GetPath(profile));
}

std::vector<std::string> Config::ListProfiles() {
    std::vector<std::string> profiles;
    
    for (auto& entry : std::filesystem::directory_iterator(configDir)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().stem().string();
            profiles.push_back(name);
        }
    }
    
    return profiles;
}

float Config::GetFloat(const std::string& key, float def) const {
    auto it = floatValues.find(key);
    return it != floatValues.end() ? it->second : def;
}

int Config::GetInt(const std::string& key, int def) const {
    auto it = intValues.find(key);
    return it != intValues.end() ? it->second : def;
}

bool Config::GetBool(const std::string& key, bool def) const {
    auto it = boolValues.find(key);
    return it != boolValues.end() ? it->second : def;
}

std::string Config::GetString(const std::string& key, const std::string& def) const {
    auto it = stringValues.find(key);
    return it != stringValues.end() ? it->second : def;
}

std::string Config::GetPath(const std::string& profile) const {
    return configDir + profile + ".cfg";
}