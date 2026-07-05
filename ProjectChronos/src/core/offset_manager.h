#pragma once
#include "types.h"
#include "memory_reader.h"
#include "state_engine.h"
#include <string>

class OffsetManager {
    MemoryReader* mem;
    OffsetDatabase db;
    
public:
    OffsetManager(MemoryReader* reader);
    
    bool Update();
    bool LoadFromFile(const std::string& path);
    bool FetchFromRemote();
    void LoadDefaults();
    
    OffsetDatabase GetOffsets() const;
    void ApplyOffsets(StateEngine& engine);
    
    bool IsUpToDate() const { return upToDate; }
    std::string GetStatus() const { return statusMsg; }
    
private:
    bool upToDate = false;
    std::string statusMsg;
    
    bool DownloadFile(const std::wstring& url, std::string& outContent);
    bool ParseHeaderFile(const std::string& content);
    void ParseClassOffsets(const std::string& csContent);
    bool SaveToLocalCache(const std::string& content);
    bool LoadFromCache(std::string& outContent);
};
