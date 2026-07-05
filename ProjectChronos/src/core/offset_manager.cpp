#include "offset_manager.h"
#include "utils/logging.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <winhttp.h>
#include <regex>

#pragma comment(lib, "winhttp.lib")

OffsetManager::OffsetManager(MemoryReader* reader) : mem(reader) {}

bool OffsetManager::Update() {
    // 1. Try downloading from cs2-dumper
    LogMessage("[Offsets] Fetching latest offsets from cs2-dumper...");
    if (FetchFromRemote()) {
        LogMessage("[Offsets] SUCCESS: Loaded fresh offsets from GitHub");
        upToDate = true;
        statusMsg = "Up to date (downloaded)";
        return true;
    }
    
    // 2. Try local cache
    std::string cached;
    if (LoadFromCache(cached) && ParseHeaderFile(cached)) {
        LogMessage("[Offsets] Loaded from local cache");
        upToDate = false;
        statusMsg = "Using cached offsets (download failed)";
        return true;
    }
    
    // 3. Try resources/offsets.json
    if (LoadFromFile("resources\\offsets.json")) {
        LogMessage("[Offsets] Loaded from resources/offsets.json");
        upToDate = false;
        statusMsg = "Using file offsets";
        return true;
    }
    
    // 4. Use built-in defaults
    LoadDefaults();
    upToDate = false;
    statusMsg = "Using built-in defaults (all sources failed)";
    LogMessage("[WARNING] All offset sources failed, using defaults");
    return false;
}

bool OffsetManager::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Try parsing as header format first, then as JSON
    if (ParseHeaderFile(content)) return true;
    
    // Fallback: simple JSON key-value parser
    auto findValue = [&](const std::string& key) -> uintptr_t {
        auto pos = content.find(key);
        if (pos == std::string::npos) return 0;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return 0;
        auto start = content.find_first_of("0123456789", pos);
        if (start == std::string::npos) return 0;
        auto end = content.find_first_of(",\n\r}", start);
        std::string numStr = content.substr(start, end - start);
        numStr.erase(0, numStr.find_first_not_of(" \t"));
        numStr.erase(numStr.find_last_not_of(" \t") + 1);
        if (numStr.find("0x") == 0 || numStr.find("0X") == 0)
            return std::stoull(numStr, nullptr, 16);
        else
            return std::stoull(numStr);
    };
    
    #define PARSE_OFFSET(name) { auto v = findValue(#name); if (v) db.name = v; }
    PARSE_OFFSET(dwLocalPlayerController);
    PARSE_OFFSET(dwLocalPlayerPawn);
    PARSE_OFFSET(dwEntityList);
    PARSE_OFFSET(dwViewMatrix);
    PARSE_OFFSET(dwGlobalVars);
    PARSE_OFFSET(dwInputSystem);
    #undef PARSE_OFFSET
    
    return true;
}

void OffsetManager::LoadDefaults() {
    db = OffsetDatabase();
}

bool OffsetManager::FetchFromRemote() {
    std::string content;
    
    // 1. Download offsets.hpp (module offsets)
    LogMessage("[Offsets] Downloading offsets.hpp...");
    std::wstring offsetsUrl = L"/a2x/cs2-dumper/main/output/offsets.hpp";
    
    if (!DownloadFile(offsetsUrl, content) || content.empty()) {
        LogMessage("[Offsets] Download of offsets.hpp failed");
        return false;
    }
    
    if (!ParseHeaderFile(content)) {
        LogMessage("[Offsets] Failed to parse offsets.hpp");
        return false;
    }
    
    // 2. Download client_dll.cs (class member offsets)
    LogMessage("[Offsets] Downloading client_dll.cs (class offsets)...");
    std::string classContent;
    std::wstring classUrl = L"/a2x/cs2-dumper/main/output/client_dll.cs";
    
    if (DownloadFile(classUrl, classContent) && classContent.size() > 100) {
        ParseClassOffsets(classContent);
        SaveToLocalCache(content); // save offsets.hpp to cache
    } else {
        LogMessage("[Offsets] client_dll.cs download failed, using defaults for class offsets");
    }
    
    return true;
}

void OffsetManager::ParseClassOffsets(const std::string& cs) {
    // Parse C# format: public const nint m_FieldName = 0xHEX;
    // We need specific fields from specific classes
    
    std::regex re(R"(public\s+const\s+nint\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;)");
    std::sregex_iterator it(cs.begin(), cs.end(), re);
    std::sregex_iterator end;
    
    int parsed = 0;
    while (it != end) {
        std::string name = (*it)[1].str();
        std::string valStr = (*it)[2].str();
        
        uintptr_t value = 0;
        if (valStr.find("0x") == 0 || valStr.find("0X") == 0)
            value = std::stoull(valStr, nullptr, 16);
        else
            value = std::stoull(valStr);
        
        // Map known class member offsets
        if (name == "m_iHealth") db.m_iHealth = value;
        else if (name == "m_iTeamNum") db.m_iTeamNum = value;
        else if (name == "m_vOldOrigin") db.m_vOldOrigin = value;
        else if (name == "m_vecVelocity") db.m_vecVelocity = value;
        else if (name == "m_vecViewOffset") db.m_vecViewOffset = value;
        else if (name == "m_angEyeAngles") db.m_angEyeAngles = value;
        else if (name == "m_iClip1") db.m_iClip1 = value;
        else if (name == "m_bIsScoped") db.m_bIsScoped = value;
        else if (name == "m_flFlashDuration") db.m_flFlashDuration = value;
        else if (name == "m_ArmorValue") db.m_ArmorValue = value;
        else if (name == "m_pGameSceneNode") db.m_pGameSceneNode = value;
        else if (name == "m_pWeaponServices") db.m_pWeaponServices = value;
        else if (name == "m_hActiveWeapon") db.m_hActiveWeapon = value;
        else if (name == "m_lifeState") db.m_lifeState = value;
        else if (name == "m_fFlags") db.m_fFlags = value;
        else if (name == "m_hPawn") db.m_hPawn = value;
        else if (name == "m_iszPlayerName") db.m_iszPlayerName = value;
        else if (name == "m_iPawnHealth") db.m_iPawnHealth = value;
        else if (name == "m_bIsLocalCtrl") db.m_bIsLocalCtrl = value;
        else if (name == "m_iItemDefinitionIndex") db.m_iItemDefinitionIndex = value;
        else if (name == "m_hBomb") db.m_hBomb = value;
        
        parsed++;
        ++it;
    }
    
    LogMessage("[Offsets] Parsed " + std::to_string(parsed) + " class member offsets");
}

bool OffsetManager::DownloadFile(const std::wstring& urlPath, std::string& outContent) {
    outContent.clear();
    
    HINTERNET hSession = WinHttpOpen(L"Chronos/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"raw.githubusercontent.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(),
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Set timeouts: 5s resolve, 5s connect, 10s send, 15s receive
    WinHttpSetTimeouts(hRequest, 5000, 5000, 10000, 15000);
    
    BOOL result = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Read response
    DWORD bytesAvailable = 0;
    char buffer[8192];
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        DWORD bytesRead = 0;
        DWORD toRead = (std::min)(bytesAvailable, (DWORD)(sizeof(buffer) - 1));
        if (WinHttpReadData(hRequest, buffer, toRead, &bytesRead)) {
            buffer[bytesRead] = 0;
            outContent.append(buffer, bytesRead);
        }
        bytesAvailable = 0;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return !outContent.empty();
}

bool OffsetManager::ParseHeaderFile(const std::string& content) {
    // Parse cs2-dumper offsets.hpp format:
    // constexpr std::ptrdiff_t dwOffsetName = 0xHEXVALUE;
    
    std::regex re(R"(constexpr\s+std::ptrdiff_t\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;)");
    std::sregex_iterator it(content.begin(), content.end(), re);
    std::sregex_iterator end;
    
    int parsed = 0;
    while (it != end) {
        std::string name = (*it)[1].str();
        std::string valStr = (*it)[2].str();
        
        uintptr_t value = 0;
        if (valStr.find("0x") == 0 || valStr.find("0X") == 0)
            value = std::stoull(valStr, nullptr, 16);
        else
            value = std::stoull(valStr);
        
        // Map to our OffsetDatabase fields
        // client.dll offsets
        if (name == "dwLocalPlayerController") db.dwLocalPlayerController = value;
        else if (name == "dwLocalPlayerPawn") db.dwLocalPlayerPawn = value;
        else if (name == "dwEntityList") db.dwEntityList = value;
        else if (name == "dwViewMatrix") db.dwViewMatrix = value;
        else if (name == "dwGlobalVars") db.dwGlobalVars = value;
        else if (name == "dwPlantedC4") db.dwPlantedC4 = value;
        else if (name == "dwGameRules") db.dwGameRules = value;
        // inputsystem.dll
        else if (name == "dwInputSystem") db.dwInputSystem = value;
        
        parsed++;
        ++it;
    }
    
    // Also parse class member offsets from client_dll.cs if available
    // These are the m_ prefixed offsets
    
    LogMessage("[Offsets] Parsed " + std::to_string(parsed) + " offsets from cs2-dumper");
    
    // Verify critical offsets
    if (db.dwLocalPlayerPawn == 0 || db.dwEntityList == 0 || db.dwViewMatrix == 0) {
        LogMessage("[WARNING] Critical offsets missing after parse!");
        return false;
    }
    
    return parsed > 0;
}

bool OffsetManager::SaveToLocalCache(const std::string& content) {
    try {
        std::filesystem::create_directories("resources");
        std::ofstream file("resources\\offsets_cache.hpp");
        if (!file.is_open()) return false;
        file << content;
        file.close();
        
        // Also save timestamp
        std::ofstream ts("resources\\offsets_timestamp.txt");
        if (ts.is_open()) {
            time_t now = time(nullptr);
            ts << now;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool OffsetManager::LoadFromCache(std::string& outContent) {
    std::ifstream file("resources\\offsets_cache.hpp");
    if (!file.is_open()) return false;
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    outContent = buffer.str();
    
    // Check if cache is too old (> 24 hours)
    std::ifstream ts("resources\\offsets_timestamp.txt");
    if (ts.is_open()) {
        time_t cached = 0;
        ts >> cached;
        time_t now = time(nullptr);
        if (now - cached > 86400) {
            LogMessage("[Offsets] Cache is older than 24h, will try re-download");
            return false;
        }
    }
    
    return !outContent.empty();
}

OffsetDatabase OffsetManager::GetOffsets() const {
    return db;
}

void OffsetManager::ApplyOffsets(StateEngine& engine) {
    engine.UpdateOffsets(db);
}
