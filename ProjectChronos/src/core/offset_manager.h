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
    void FetchFromRemote();
    void LoadDefaults();
    
    OffsetDatabase GetOffsets() const;
    void ApplyOffsets(StateEngine& engine);
    
private:
    void ParseOffsets(const std::string& json);
};