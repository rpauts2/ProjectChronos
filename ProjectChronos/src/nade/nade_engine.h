#pragma once
#include "core/types.h"
#include <vector>
#include <string>
#include <map>

// ==================== MOVEMENT TRICKS ====================
// Auto-trick system for jump spots, bhop tricks, etc.

struct TrickAction {
    int key;            // VK code (VK_SPACE, 'W', 'A', 'S', 'D', VK_CONTROL)
    float holdTime;     // how long to hold (seconds)
    float delayBefore;  // delay before this action (seconds)
};

struct MovementTrick {
    std::string name;
    std::string map;
    Vector3 triggerPos;     // position to stand at to activate
    float triggerRadius;    // how close to trigger (units)
    QAngle faceDirection;   // direction to face
    std::vector<TrickAction> sequence; // key sequence
    std::string description;
};

// ==================== NADE ENGINE ====================
// Auto-grenade assistant: position, aim, execute with trajectory prediction

class NadeEngine {
    std::map<std::string, std::vector<NadeSpot>> database;
    std::map<std::string, std::vector<MovementTrick>> trickDatabase;
    bool recording = false;
    NadeSpot currentRecord;
    NadeSpot* activeNade = nullptr;
    bool autoAimActive = false;
    bool throwSequenceActive = false;
    float throwSequenceTimer = 0;
    int throwSequenceStep = 0;
    NadeSpot* pendingThrow = nullptr;
    
    // Auto-trick state
    bool trickSequenceActive = false;
    float trickSequenceTimer = 0;
    int trickSequenceStep = 0;
    MovementTrick* pendingTrick = nullptr;
    float trickTotalTime = 0;
    
    // Trajectory cache for rendering
    struct TrajectoryCache {
        std::vector<Vector3> points;
        Vector3 landingPos;
        float flightTime = 0;
        bool valid = false;
    } trajectoryCache;
    
public:
    NadeEngine() {}
    
    bool LoadDatabase(const std::string& mapName);
    void LoadDefaultDatabase();
    
    std::vector<NadeSpot> GetAvailableNades(GameState* state, NadeType filterType);
    NadeSpot* GetClosestNade(Vector3 playerPos, float maxDist = 500.0f);
    NadeSpot* GetNadeByName(const std::string& name);
    
    // Recording
    void StartRecording(NadeType type);
    void StopRecording(GameState* state);
    bool IsRecording() const { return recording; }
    
    // Auto-execute with smooth aim
    bool StartThrowSequence(NadeSpot& spot);  // begins the aim+throw sequence
    void UpdateThrowSequence(GameState* state, float deltaTime); // call each frame
    void UpdateAutoAim(GameState* state);     // legacy: update auto-aim for active nade
    bool IsThrowing() const { return throwSequenceActive; }
    
    // Physics simulation (improved with bounce + detonation radius)
    Vector3 SimulateTrajectory(Vector3 pos, QAngle angle, int action, float& outTime);
    std::vector<Vector3> GetTrajectoryPath(Vector3 pos, QAngle angle, int action, int maxBounces = 3);
    
    // Auto-tricks
    void LoadTricks();
    MovementTrick* GetClosestTrick(Vector3 playerPos, float maxDist = 150.0f);
    bool StartTrick(MovementTrick& trick);
    void UpdateTrickSequence(GameState* state, float deltaTime);
    bool IsTricking() const { return trickSequenceActive; }
    
    // UI
    struct NadeUI {
        Vector3 position;
        QAngle aimAngle;
        std::string actionText;
        Vector3 landingPos;
        bool canExecute;
        float distance;
        std::string typeName;
    };
    NadeUI GetUI(GameState* state, NadeSpot* spot);
    
    // Trajectory cache for overlay rendering
    const TrajectoryCache& GetTrajectoryCache() const { return trajectoryCache; }
    void UpdateTrajectoryCache(Vector3 pos, QAngle angle, int action);
    
    // Keybinds (configurable from menu)
    int throwKeyBind = 'V';     // key to execute throw
    int nadeHelperKey = 0x47;   // G key to show/toggle nade helper
    float nadeHelperAimSpeed = 0.3f; // smooth aim speed
    
    // Current map for filtering
    std::string currentMap;
    void SetCurrentMap(const std::string& map) { currentMap = map; }
    
    // Database access for rendering
    const std::map<std::string, std::vector<NadeSpot>>& GetDatabase() const { return database; }
    const std::map<std::string, std::vector<MovementTrick>>& GetTrickDatabase() const { return trickDatabase; }
    
private:
    NadeSpot CreateSpot(Vector3 pos, QAngle angle, int action, const std::string& name);
    void SaveToDatabase(NadeSpot& spot);
    float GetThrowSpeed(int action, int nadeType);
    void AimAtAngle(GameState* state, QAngle targetAngle, float smoothFactor);
    void PressKey(int key);
    void ReleaseKey(int key);
};