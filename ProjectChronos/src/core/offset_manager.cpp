#include "offset_manager.h"
#include <fstream>
#include <sstream>
#include <filesystem>

OffsetManager::OffsetManager(MemoryReader* reader) : mem(reader) {}

bool OffsetManager::Update() {
    // Try local file first
    if (LoadFromFile("resources\\offsets.json"))
        return true;
    
    // If failed, use built-in defaults
    LoadDefaults();
    return false;
}

bool OffsetManager::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Simple JSON parser (in production use nlohmann/json)
    ParseOffsets(content);
    return true;
}

void OffsetManager::FetchFromRemote() {
    // TODO: Download latest offsets from cs2-dumper
    // URL: https://raw.githubusercontent.com/a2x/cs2-dumper/main/generated/client_dll.json
    // Store in resources/offsets.json
    
    // For now, update from backup
    LoadDefaults();
}

void OffsetManager::LoadDefaults() {
    db = OffsetDatabase(); // Uses default values from types.h
}

void OffsetManager::ParseOffsets(const std::string& json) {
    // Simple key-value parser for "key": value format
    auto findValue = [&](const std::string& key) -> uintptr_t {
        auto pos = json.find(key);
        if (pos == std::string::npos) return 0;
        
        pos = json.find(':', pos);
        if (pos == std::string::npos) return 0;
        
        // Find hex number (0x...) or decimal
        auto start = json.find_first_of("0123456789", pos);
        if (start == std::string::npos) return 0;
        
        auto end = json.find_first_of(",\n\r}", start);
        std::string numStr = json.substr(start, end - start);
        
        // Trim whitespace
        numStr.erase(0, numStr.find_first_not_of(" \t"));
        numStr.erase(numStr.find_last_not_of(" \t") + 1);
        
        if (numStr.find("0x") == 0 || numStr.find("0X") == 0)
            return std::stoull(numStr, nullptr, 16);
        else
            return std::stoull(numStr);
    };
    
    // Parse all known offsets
    #define PARSE_OFFSET(name) { auto v = findValue(#name); if (v) db.name = v; }
    
    PARSE_OFFSET(dwLocalPlayerController);
    PARSE_OFFSET(dwLocalPlayerPawn);
    PARSE_OFFSET(dwEntityList);
    PARSE_OFFSET(dwViewMatrix);
    PARSE_OFFSET(dwGlobalVars);
    PARSE_OFFSET(dwInputSystem);
    PARSE_OFFSET(m_iHealth);
    PARSE_OFFSET(m_iTeamNum);
    PARSE_OFFSET(m_vOldOrigin);
    PARSE_OFFSET(m_vVelocity);
    PARSE_OFFSET(m_angEyeAngles);
    PARSE_OFFSET(m_aimPunchAngle);
    PARSE_OFFSET(m_iClip1);
    PARSE_OFFSET(m_iShotsFired);
    PARSE_OFFSET(m_bIsScoped);
    PARSE_OFFSET(m_flFlashDuration);
    PARSE_OFFSET(m_iHasBomb);
    PARSE_OFFSET(m_szName);
    PARSE_OFFSET(m_pGameSceneNode);
    PARSE_OFFSET(m_pBoneMergeCache);
    PARSE_OFFSET(m_fAccuracyPenalty);
    PARSE_OFFSET(m_fSpread);
    PARSE_OFFSET(m_flRecoilIndex);
    PARSE_OFFSET(m_iItemDefinitionIndex);
    PARSE_OFFSET(m_nCommandNumber);
    PARSE_OFFSET(m_nTickCount);
    PARSE_OFFSET(m_viewangles);
    PARSE_OFFSET(m_nButtons);
    PARSE_OFFSET(m_pCommands);
}

OffsetDatabase OffsetManager::GetOffsets() const {
    return db;
}

void OffsetManager::ApplyOffsets(StateEngine& engine) {
    engine.UpdateOffsets(db);
}