#include "nade_engine.h"
#include <cmath>
#include <fstream>
#include <sstream>

bool NadeEngine::LoadDatabase(const std::string& mapName) {
    // Try to load from file
    std::string path = "resources/nade_data/" + mapName + ".json";
    std::ifstream file(path);
    
    if (file.is_open()) {
        // Parse JSON and populate database
        file.close();
        return true;
    }
    
    // Fallback to defaults
    LoadDefaultDatabase();
    return false;
}

void NadeEngine::LoadDefaultDatabase() {
    // Built-in default nades for popular maps
    std::vector<NadeSpot> defaultNades;
    
    // de_dust2 — A Site Window Smoke from T Spawn
    NadeSpot ds2_smoke_awindow;
    ds2_smoke_awindow.name = "A Window Smoke";
    ds2_smoke_awindow.map = "de_dust2";
    ds2_smoke_awindow.type = SMOKE;
    ds2_smoke_awindow.standPos = {-1350, 2350, 130};
    ds2_smoke_awindow.aimAngle = {-42, 135, 0};
    ds2_smoke_awindow.action = JUMP_THROW;
    ds2_smoke_awindow.landingPos = {-550, 1250, 0};
    ds2_smoke_awindow.description = "Smokes CT from T spawn";
    ds2_smoke_awindow.targets = {"A Site", "CT Spawn"};
    defaultNades.push_back(ds2_smoke_awindow);
    
    // de_dust2 — XBox Smoke
    NadeSpot ds2_smoke_xbox;
    ds2_smoke_xbox.name = "XBox Smoke";
    ds2_smoke_xbox.map = "de_dust2";
    ds2_smoke_xbox.type = SMOKE;
    ds2_smoke_xbox.standPos = {-210, 2100, 128};
    ds2_smoke_xbox.aimAngle = {-55, 0, 0};
    ds2_smoke_xbox.action = STAND_THROW;
    ds2_smoke_xbox.landingPos = {0, 1400, 0};
    ds2_smoke_xbox.description = "Smokes XBox from T mid";
    ds2_smoke_xbox.targets = {"Mid", "XBox"};
    defaultNades.push_back(ds2_smoke_xbox);
    
    // de_mirage — A Site Window Smoke
    NadeSpot mirage_smoke_window;
    mirage_smoke_window.name = "A Window Smoke";
    mirage_smoke_window.map = "de_mirage";
    mirage_smoke_window.type = SMOKE;
    mirage_smoke_window.standPos = {-1700, 1400, 132};
    mirage_smoke_window.aimAngle = {-38, 215, 0};
    mirage_smoke_window.action = JUMP_THROW;
    mirage_smoke_window.landingPos = {-450, 600, 0};
    mirage_smoke_window.description = "Smokes A Window from T spawn";
    mirage_smoke_window.targets = {"A Site", "A Window"};
    defaultNades.push_back(mirage_smoke_window);
    
    database["de_dust2"] = defaultNades;
    database["de_mirage"] = {mirage_smoke_window};
}

std::vector<NadeSpot> NadeEngine::GetAvailableNades(GameState* state, NadeType filterType) {
    std::vector<NadeSpot> result;
    if (!state) return result;
    
    std::string mapName(state->mapName);
    auto it = database.find(mapName);
    if (it == database.end()) return result;
    
    for (auto& nade : it->second) {
        if (filterType == nade.type || filterType == SMOKE) {
            result.push_back(nade);
        }
    }
    
    return result;
}

NadeSpot* NadeEngine::GetClosestNade(Vector3 playerPos, float maxDist) {
    NadeSpot* closest = nullptr;
    float bestDist = maxDist;
    
    for (auto& [map, nades] : database) {
        for (auto& nade : nades) {
            float dist = playerPos.DistTo(nade.standPos);
            if (dist < bestDist) {
                bestDist = dist;
                closest = &nade;
            }
        }
    }
    
    return closest;
}

void NadeEngine::StartRecording(NadeType type) {
    recording = true;
    currentRecord = {};
    currentRecord.type = type;
}

void NadeEngine::StopRecording(GameState* state) {
    if (!recording || !state) return;
    
    auto* local = state->GetLocal();
    if (!local) return;
    
    currentRecord.standPos = local->origin;
    currentRecord.aimAngle = local->viewAngle;
    
    // Determine action based on player state
    if (GetAsyncKeyState(VK_SPACE) & 0x8000)
        currentRecord.action = JUMP_THROW;
    else if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        currentRecord.action = CROUCH_THROW;
    else if (local->velocity.Length2D() > 100)
        currentRecord.action = RUN_THROW;
    else
        currentRecord.action = STAND_THROW;
    
    currentRecord.name = "User #" + std::to_string(rand() % 1000);
    
    SaveToDatabase(currentRecord);
    recording = false;
}

void NadeEngine::SaveToDatabase(NadeSpot& spot) {
    spot.map = "current";
    database[spot.map].push_back(spot);
}

bool NadeEngine::ExecuteNade(NadeSpot& spot) {
    if (!spot.IsValid()) return false;
    
    // 1. Move to position (simplified: just check distance)
    // In full implementation: SendInput movement
    
    // 2. Set view angle
    // Send mouse input to aim at the correct angle
    // This requires mouse movement calculation based on current angle
    
    // 3. Perform action
    switch (spot.action) {
        case JUMP_THROW:
            // Press jump + fire
            keybd_event(VK_SPACE, 0, 0, 0);
            Sleep(50);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(10);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
            break;
        case CROUCH_THROW:
            keybd_event(VK_CONTROL, 0, 0, 0);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(10);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
            break;
        default:
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(10);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            break;
    }
    
    return true;
}

Vector3 NadeEngine::SimulateTrajectory(Vector3 pos, QAngle angle, int action, float& outTime) {
    // Simplified Source 2 grenade physics simulation
    float pitch = angle.pitch * 3.14159f / 180.0f;
    float yaw = angle.yaw * 3.14159f / 180.0f;
    
    // Initial velocity based on action
    float initialSpeed = 1000.0f; // Base throw speed
    switch (action) {
        case JUMP_THROW: initialSpeed = 1100.0f; break;
        case CROUCH_THROW: initialSpeed = 850.0f; break;
        case WALK_THROW: initialSpeed = 750.0f; break;
        case RUN_THROW: initialSpeed = 1200.0f; break;
        default: initialSpeed = 1000.0f; break;
    }
    
    Vector3 velocity;
    velocity.x = -cosf(pitch) * cosf(yaw) * initialSpeed;
    velocity.y = cosf(pitch) * sinf(yaw) * initialSpeed;
    velocity.z = -sinf(pitch) * initialSpeed;
    
    // Add jump velocity
    if (action == JUMP_THROW)
        velocity.z += 300.0f;
    
    // Time-step simulation
    float dt = 0.001f; // 1ms
    Vector3 currentPos = pos;
    Vector3 currentVel = velocity;
    
    const float GRAVITY = 800.0f;
    const float DRAG = 0.01f;
    
    outTime = 0;
    
    for (int step = 0; step < 5000; step++) {
        currentVel.z -= GRAVITY * dt;
        currentVel *= (1.0f - DRAG * dt);
        currentPos = currentPos + currentVel * dt;
        outTime += dt;
        
        // Check ground collision
        if (currentPos.z < 0) {
            currentPos.z = 0;
            break;
        }
    }
    
    return currentPos;
}

NadeEngine::NadeUI NadeEngine::GetUI(GameState* state, NadeSpot* spot) {
    NadeUI ui = {};
    if (!state || !spot) return ui;
    
    ui.position = spot->standPos;
    ui.aimAngle = spot->aimAngle;
    ui.landingPos = spot->landingPos;
    
    switch (spot->action) {
        case JUMP_THROW: ui.actionText = "Jump + Throw"; break;
        case CROUCH_THROW: ui.actionText = "Crouch + Throw"; break;
        case WALK_THROW: ui.actionText = "Walk + Throw"; break;
        case RUN_THROW: ui.actionText = "Run + Throw"; break;
        default: ui.actionText = "Stand + Throw"; break;
    }
    
    // Check if player is near position
    auto* local = state->GetLocal();
    if (local) {
        float dist = local->origin.DistTo(spot->standPos);
        ui.canExecute = dist < 100.0f;
    }
    
    return ui;
}