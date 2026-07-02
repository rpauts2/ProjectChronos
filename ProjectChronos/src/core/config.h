#pragma once
#include "types.h"
#include <string>
#include <map>
#include <fstream>

// ==================== CONFIG SYSTEM ====================
// Save/load cheat configuration profiles

class Config {
    std::string configDir = "configs/";
    std::string currentProfile = "default";
    
    std::map<std::string, float> floatValues;
    std::map<std::string, int> intValues;
    std::map<std::string, bool> boolValues;
    std::map<std::string, std::string> stringValues;
    
public:
    Config();
    
    bool Load(const std::string& profile);
    bool Save(const std::string& profile);
    bool Delete(const std::string& profile);
    std::vector<std::string> ListProfiles();
    
    // Getters/setters
    void SetFloat(const std::string& key, float val) { floatValues[key] = val; }
    void SetInt(const std::string& key, int val) { intValues[key] = val; }
    void SetBool(const std::string& key, bool val) { boolValues[key] = val; }
    void SetString(const std::string& key, const std::string& val) { stringValues[key] = val; }
    
    float GetFloat(const std::string& key, float def = 0) const;
    int GetInt(const std::string& key, int def = 0) const;
    bool GetBool(const std::string& key, bool def = false) const;
    std::string GetString(const std::string& key, const std::string& def = "") const;
    
private:
    std::string GetPath(const std::string& profile) const;
};