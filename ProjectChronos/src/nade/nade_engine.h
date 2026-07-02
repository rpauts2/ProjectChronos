#pragma once
#include "core/types.h"
#include <vector>
#include <string>
#include <map>

// ==================== NADE ENGINE ====================
// Auto-grenade assistant: position, aim, execute

class NadeEngine {
    std::map<std::string, std::vector<NadeSpot>> database; // map name → nades
    bool recording = false;
    NadeSpot currentRecord;
    
public:
    NadeEngine() {}
    
    bool LoadDatabase(const std::string& mapName);
    void LoadDefaultDatabase();
    
    // Get nades for current position/context
    std::vector<NadeSpot> GetAvailableNades(GameState* state, NadeType filterType);
    NadeSpot* GetClosestNade(Vector3 playerPos, float maxDist = 500.0f);
    
    // Recording
    void StartRecording(NadeType type);
    void StopRecording(GameState* state);
    bool IsRecording() const { return recording; }
    
    // Auto-execute
    bool ExecuteNade(NadeSpot& spot);
    
    // Physics simulation
    Vector3 SimulateTrajectory(Vector3 pos, QAngle angle, int action, float& outTime);
    
    // Get UI data
    struct NadeUI {
        Vector3 position;
        QAngle aimAngle;
        std::string actionText;
        Vector3 landingPos;
        bool canExecute;
    };
    NadeUI GetUI(GameState* state, NadeSpot* spot);
    
private:
    NadeSpot CreateSpot(Vector3 pos, QAngle angle, int action, const std::string& name);
    void SaveToDatabase(NadeSpot& spot);
};