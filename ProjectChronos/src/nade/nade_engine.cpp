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
    LoadTricks(); // Also load movement tricks
    std::vector<NadeSpot> dust2, mirage, inferno, nuke, overpass, anubis, ancient;

    auto mk = [](const char* n, const char* m, NadeType t, Vector3 p, QAngle a, int act, Vector3 l, const char* d) {
        NadeSpot s; s.name=n; s.map=m; s.type=t; s.standPos=p; s.aimAngle=a; s.action=act; s.landingPos=l; s.description=d; return s;
    };

    // ═══════════════════════ de_dust2 ═══════════════════════
    dust2.push_back(mk("A Window Smoke","de_dust2",SMOKE,{-1350,2350,130},{-42,135,0},JUMP_THROW,{-550,1250,0},"Smokes CT from T spawn"));
    dust2.push_back(mk("XBox Smoke","de_dust2",SMOKE,{-210,2100,128},{-55,0,0},STAND_THROW,{0,1400,0},"Smokes XBox from T mid"));
    dust2.push_back(mk("A Short Smoke","de_dust2",SMOKE,{-1200,2500,130},{-48,160,0},STAND_THROW,{-300,1800,0},"Smokes A Short"));
    dust2.push_back(mk("A Cross Smoke","de_dust2",SMOKE,{-800,2100,128},{-35,145,0},JUMP_THROW,{100,1900,0},"Smokes cross to A"));
    dust2.push_back(mk("A Site Flash","de_dust2",FLASH,{-1200,2400,130},{-60,150,0},JUMP_THROW,{-200,1500,0},"Pop flash A site"));
    dust2.push_back(mk("A Site HE","de_dust2",HE,{-1100,2300,130},{-50,140,0},STAND_THROW,{-100,1600,0},"HE A site default"));
    dust2.push_back(mk("A Car Molotov","de_dust2",MOLOTOV,{-1300,2350,130},{-44,132,0},STAND_THROW,{-400,1350,0},"Molotov A car"));
    dust2.push_back(mk("B Door Smoke","de_dust2",SMOKE,{800,2300,130},{-38,-45,0},JUMP_THROW,{1200,1400,0},"Smokes B doors"));
    dust2.push_back(mk("B Window Smoke","de_dust2",SMOKE,{600,2200,130},{-42,-35,0},STAND_THROW,{1100,1300,0},"Smokes B window"));
    dust2.push_back(mk("B Site Flash","de_dust2",FLASH,{700,2300,130},{-55,-40,0},JUMP_THROW,{1300,1500,0},"Pop flash B site"));
    dust2.push_back(mk("B Platform Molotov","de_dust2",MOLOTOV,{800,2200,130},{-40,-30,0},STAND_THROW,{1400,1600,0},"Molotov B platform"));
    dust2.push_back(mk("Mid Doors Smoke","de_dust2",SMOKE,{-600,2000,128},{-30,20,0},STAND_THROW,{200,400,0},"Smokes mid doors"));
    dust2.push_back(mk("Lower Tunnels Smoke","de_dust2",SMOKE,{-1100,1700,128},{-25,60,0},STAND_THROW,{-200,800,0},"Smokes lower tunnels"));
    database["de_dust2"] = dust2;

    // ═══════════════════════ de_mirage ═══════════════════════
    mirage.push_back(mk("A Window Smoke","de_mirage",SMOKE,{-1700,1400,132},{-38,215,0},JUMP_THROW,{-450,600,0},"Smokes A window from T spawn"));
    mirage.push_back(mk("A Jungle Smoke","de_mirage",SMOKE,{-1500,1300,132},{-35,200,0},STAND_THROW,{-300,500,0},"Smokes A jungle"));
    mirage.push_back(mk("A Stairs Smoke","de_mirage",SMOKE,{-1400,1500,132},{-40,190,0},STAND_THROW,{-200,700,0},"Smokes A stairs"));
    mirage.push_back(mk("A CT Smoke","de_mirage",SMOKE,{-1600,1200,132},{-42,225,0},JUMP_THROW,{-600,400,0},"Smokes A CT"));
    mirage.push_back(mk("A Palace Flash","de_mirage",FLASH,{-1200,1600,132},{-55,175,0},JUMP_THROW,{-100,800,0},"Pop flash A palace"));
    mirage.push_back(mk("A Default Molotov","de_mirage",MOLOTOV,{-1300,1400,132},{-45,195,0},STAND_THROW,{-100,500,0},"Molotov A default"));
    mirage.push_back(mk("B Short Smoke","de_mirage",SMOKE,{400,2000,132},{-30,-170,0},STAND_THROW,{600,1000,0},"Smokes B short"));
    mirage.push_back(mk("B Apps Flash","de_mirage",FLASH,{300,2100,132},{-50,-160,0},JUMP_THROW,{500,1200,0},"Pop flash B apps"));
    mirage.push_back(mk("Mid Window Smoke","de_mirage",SMOKE,{-800,1800,132},{-25,10,0},JUMP_THROW,{-200,400,0},"Smokes mid window"));
    database["de_mirage"] = mirage;

    // ═══════════════════════ de_inferno ═══════════════════════
    inferno.push_back(mk("A Site Smoke","de_inferno",SMOKE,{-1600,2700,130},{-45,120,0},JUMP_THROW,{-500,1800,0},"Smokes A site from ramp"));
    inferno.push_back(mk("A CT Smoke","de_inferno",SMOKE,{-1500,2600,130},{-42,135,0},STAND_THROW,{-700,2000,0},"Smokes A CT"));
    inferno.push_back(mk("A Moto Flash","de_inferno",FLASH,{-1400,2500,130},{-60,110,0},JUMP_THROW,{-300,1700,0},"Pop flash A moto"));
    inferno.push_back(mk("B CT Smoke","de_inferno",SMOKE,{-2400,1200,130},{-35,-10,0},JUMP_THROW,{-1500,2000,0},"Smokes B CT"));
    inferno.push_back(mk("B Coffin Flash","de_inferno",FLASH,{-2300,1300,130},{-55,-20,0},JUMP_THROW,{-1600,2100,0},"Pop flash B coffins"));
    inferno.push_back(mk("Banana Molotov","de_inferno",MOLOTOV,{-2200,1400,130},{-30,-5,0},STAND_THROW,{-1400,2200,0},"Molotov banana"));
    inferno.push_back(mk("Mid Garage Smoke","de_inferno",SMOKE,{-1800,2200,130},{-28,50,0},STAND_THROW,{-1000,1500,0},"Smokes mid garage"));
    database["de_inferno"] = inferno;

    // ═══════════════════════ de_nuke ═══════════════════════
    nuke.push_back(mk("A Site Smoke","de_nuke",SMOKE,{-800,-1200,-400},{-35,45,0},JUMP_THROW,{-200,-500,-450},"Smokes A site from outside"));
    nuke.push_back(mk("Ramp Smoke","de_nuke",SMOKE,{-600,-1500,-400},{-30,30,0},STAND_THROW,{0,-800,-450},"Smokes ramp room"));
    nuke.push_back(mk("B Site Flash","de_nuke",FLASH,{-200,-400,-700},{-60,0,0},JUMP_THROW,{0,0,-750},"Pop flash B site"));
    nuke.push_back(mk("Yard Smoke","de_nuke",SMOKE,{-900,-1100,-400},{-38,50,0},JUMP_THROW,{-400,-600,-450},"Smokes yard from T"));
    database["de_nuke"] = nuke;

    // ═══════════════════════ de_overpass ═══════════════════════
    overpass.push_back(mk("A Site Smoke","de_overpass",SMOKE,{-1000,300,130},{-40,-120,0},JUMP_THROW,{-200,-300,0},"Smokes A site from long"));
    overpass.push_back(mk("A Bank Smoke","de_overpass",SMOKE,{-900,200,130},{-35,-110,0},STAND_THROW,{-100,-200,0},"Smokes A bank"));
    overpass.push_back(mk("B Short Smoke","de_overpass",SMOKE,{300,1500,130},{-30,45,0},JUMP_THROW,{-200,800,0},"Smokes B short"));
    overpass.push_back(mk("B Site Flash","de_overpass",FLASH,{200,1600,130},{-55,50,0},JUMP_THROW,{-100,900,0},"Pop flash B site"));
    overpass.push_back(mk("Monster Molotov","de_overpass",MOLOTOV,{400,1400,130},{-25,30,0},STAND_THROW,{0,700,0},"Molotov monster"));
    database["de_overpass"] = overpass;

    // ═══════════════════════ de_anubis ═══════════════════════
    anubis.push_back(mk("A Main Smoke","de_anubis",SMOKE,{-1300,1000,130},{-42,170,0},JUMP_THROW,{-400,500,0},"Smokes A main"));
    anubis.push_back(mk("B Site Smoke","de_anubis",SMOKE,{400,1200,130},{-35,-50,0},JUMP_THROW,{0,500,0},"Smokes B site"));
    anubis.push_back(mk("Mid Flash","de_anubis",FLASH,{-500,800,130},{-55,100,0},JUMP_THROW,{0,300,0},"Pop flash mid"));
    anubis.push_back(mk("A Connector Molotov","de_anubis",MOLOTOV,{-1100,900,130},{-38,160,0},STAND_THROW,{-300,400,0},"Molotov A connector"));
    database["de_anubis"] = anubis;

    // ═══════════════════════ de_ancient ═══════════════════════
    ancient.push_back(mk("A Site Smoke","de_ancient",SMOKE,{-1200,1800,130},{-40,150,0},JUMP_THROW,{-300,1000,0},"Smokes A site"));
    ancient.push_back(mk("B Site Smoke","de_ancient",SMOKE,{800,2000,130},{-35,-60,0},JUMP_THROW,{300,1200,0},"Smokes B site"));
    ancient.push_back(mk("Mid Flash","de_ancient",FLASH,{-200,1500,130},{-55,90,0},JUMP_THROW,{0,800,0},"Pop flash mid"));
    database["de_ancient"] = ancient;
}

std::vector<NadeSpot> NadeEngine::GetAvailableNades(GameState* state, NadeType filterType) {
    std::vector<NadeSpot> result;
    if (!state) return result;
    
    std::string mapName(state->mapName);
    if (mapName.empty()) return result;
    auto it = database.find(mapName);
    if (it == database.end()) return result;
    
    for (auto& nade : it->second) {
        if (filterType == -1 || filterType == nade.type) {
            result.push_back(nade);
        }
    }
    
    return result;
}

NadeSpot* NadeEngine::GetClosestNade(Vector3 playerPos, float maxDist) {
    NadeSpot* closest = nullptr;
    float bestDist = maxDist;
    
    for (auto& [map, nades] : database) {
        if (!currentMap.empty() && map != currentMap) continue;
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
    if (spot.map.empty() || spot.map == "current") {
        spot.map = "unknown";
    }
    database[spot.map].push_back(spot);
}

bool NadeEngine::StartThrowSequence(NadeSpot& spot) {
    if (!spot.IsValid()) return false;
    pendingThrow = &spot;
    throwSequenceActive = true;
    throwSequenceTimer = 0;
    throwSequenceStep = 0;
    autoAimActive = true;
    activeNade = &spot;
    return true;
}

void NadeEngine::UpdateThrowSequence(GameState* state, float deltaTime) {
    if (!throwSequenceActive || !pendingThrow || !state) return;
    
    auto* local = state->GetLocal();
    if (!local) return;
    
    throwSequenceTimer += deltaTime;
    NadeSpot& spot = *pendingThrow;
    
    // Step 0: Aim at target angle (smooth aim via mouse_event)
    if (throwSequenceStep == 0) {
        AimAtAngle(state, spot.aimAngle, nadeHelperAimSpeed);
        
        // Check if aim is close enough
        QAngle delta = spot.aimAngle - local->viewAngle;
        delta.Clamp();
        float dist = sqrtf(delta.pitch * delta.pitch + delta.yaw * delta.yaw);
        
        if (dist < 1.0f || throwSequenceTimer > 3.0f) {
            // Aim reached (or timeout), proceed to throw
            throwSequenceStep = 1;
            throwSequenceTimer = 0;
        }
        return;
    }
    
    // Step 1: Perform the throw action
    if (throwSequenceStep == 1) {
        switch (spot.action) {
            case JUMP_THROW:
                // Jump first
                if (throwSequenceTimer < 0.01f) {
                    PressKey(VK_SPACE);
                } else if (throwSequenceTimer > 0.20f && throwSequenceTimer < 0.22f) {
                    // At jump apex, throw
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                } else if (throwSequenceTimer > 0.25f) {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    ReleaseKey(VK_SPACE);
                    throwSequenceStep = 2;
                }
                break;
                
            case CROUCH_THROW:
                if (throwSequenceTimer < 0.01f) {
                    PressKey(VK_CONTROL);
                } else if (throwSequenceTimer > 0.15f && throwSequenceTimer < 0.17f) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                } else if (throwSequenceTimer > 0.20f) {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    ReleaseKey(VK_CONTROL);
                    throwSequenceStep = 2;
                }
                break;
                
            case WALK_THROW:
                if (throwSequenceTimer < 0.01f) {
                    PressKey('W');
                } else if (throwSequenceTimer > 0.30f && throwSequenceTimer < 0.32f) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                } else if (throwSequenceTimer > 0.35f) {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    ReleaseKey('W');
                    throwSequenceStep = 2;
                }
                break;
                
            case RUN_THROW:
                if (throwSequenceTimer < 0.01f) {
                    PressKey('W');
                } else if (throwSequenceTimer > 0.50f && throwSequenceTimer < 0.52f) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                } else if (throwSequenceTimer > 0.55f) {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    ReleaseKey('W');
                    throwSequenceStep = 2;
                }
                break;
                
            default: // STAND_THROW
                if (throwSequenceTimer < 0.10f) {
                    // Brief delay for aim settle
                } else if (throwSequenceTimer > 0.10f && throwSequenceTimer < 0.12f) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                } else if (throwSequenceTimer > 0.15f) {
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    throwSequenceStep = 2;
                }
                break;
        }
    }
    
    // Step 2: Done
    if (throwSequenceStep == 2) {
        throwSequenceActive = false;
        autoAimActive = false;
        activeNade = nullptr;
        pendingThrow = nullptr;
    }
}

void NadeEngine::AimAtAngle(GameState* state, QAngle targetAngle, float smoothFactor) {
    if (!state) return;
    auto* local = state->GetLocal();
    if (!local) return;
    
    QAngle currentAngle = local->viewAngle;
    QAngle delta = targetAngle - currentAngle;
    delta.Clamp();
    
    // Convert to mouse movement
    float horizFov = 90.0f;
    float pixelsPerDegree = 1920.0f / horizFov;
    
    float dx = delta.yaw * pixelsPerDegree * smoothFactor;
    float dy = delta.pitch * pixelsPerDegree * smoothFactor;
    
    // Clamp movement
    float maxPixels = 50.0f;
    float mag = sqrtf(dx * dx + dy * dy);
    if (mag > maxPixels) {
        dx = dx / mag * maxPixels;
        dy = dy / mag * maxPixels;
    }
    
    if (fabsf(dx) > 0.5f || fabsf(dy) > 0.5f) {
        mouse_event(MOUSEEVENTF_MOVE, (int)dx, (int)dy, 0, 0);
    }
}

void NadeEngine::PressKey(int key) {
    keybd_event((BYTE)key, 0, 0, 0);
}

void NadeEngine::ReleaseKey(int key) {
    keybd_event((BYTE)key, 0, KEYEVENTF_KEYUP, 0);
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
    ui.typeName = "Unknown";
    
    switch (spot->type) {
        case SMOKE: ui.typeName = "Smoke"; break;
        case FLASH: ui.typeName = "Flash"; break;
        case HE: ui.typeName = "HE Grenade"; break;
        case MOLOTOV: ui.typeName = "Molotov"; break;
        case DECOY: ui.typeName = "Decoy"; break;
    }
    
    switch (spot->action) {
        case JUMP_THROW: ui.actionText = "Jump + Throw"; break;
        case CROUCH_THROW: ui.actionText = "Crouch + Throw"; break;
        case WALK_THROW: ui.actionText = "Walk + Throw"; break;
        case RUN_THROW: ui.actionText = "Run + Throw"; break;
        default: ui.actionText = "Stand + Throw"; break;
    }
    
    auto* local = state->GetLocal();
    if (local) {
        ui.distance = local->origin.DistTo(spot->standPos);
        ui.canExecute = ui.distance < 100.0f;
    }
    
    return ui;
}

float NadeEngine::GetThrowSpeed(int action, int /*nadeType*/) {
    float baseSpeed = 1000.0f;
    switch (action) {
        case JUMP_THROW: return baseSpeed * 1.1f;
        case CROUCH_THROW: return baseSpeed * 0.85f;
        case WALK_THROW: return baseSpeed * 0.75f;
        case RUN_THROW: return baseSpeed * 1.2f;
        default: return baseSpeed;
    }
}

std::vector<Vector3> NadeEngine::GetTrajectoryPath(Vector3 pos, QAngle angle, int action, int maxBounces) {
    std::vector<Vector3> points;
    float pitch = angle.pitch * 3.14159f / 180.0f;
    float yaw = angle.yaw * 3.14159f / 180.0f;
    
    float speed = GetThrowSpeed(action, 0);
    Vector3 velocity;
    velocity.x = -cosf(pitch) * cosf(yaw) * speed;
    velocity.y = cosf(pitch) * sinf(yaw) * speed;
    velocity.z = -sinf(pitch) * speed;
    if (action == JUMP_THROW) velocity.z += 300.0f;
    
    float dt = 0.016f;
    Vector3 curPos = pos;
    Vector3 curVel = velocity;
    const float GRAVITY = 800.0f;
    const float BOUNCE_DAMPING = 0.6f;
    int bounces = 0;
    
    points.push_back(curPos);
    
    for (int step = 0; step < 1000; step++) {
        curVel.z -= GRAVITY * dt;
        curPos = curPos + curVel * dt;
        
        if (curPos.z <= 0 && bounces < maxBounces) {
            curPos.z = 0;
            curVel.z = -curVel.z * BOUNCE_DAMPING;
            curVel.x *= 0.8f;
            curVel.y *= 0.8f;
            bounces++;
        }
        
        if (step % 3 == 0)
            points.push_back(curPos);
        
        if (curPos.z < -100 || curVel.Length() < 10) break;
    }
    
    points.push_back(curPos);
    return points;
}

void NadeEngine::UpdateTrajectoryCache(Vector3 pos, QAngle angle, int action) {
    trajectoryCache.points = GetTrajectoryPath(pos, angle, action);
    trajectoryCache.landingPos = trajectoryCache.points.empty() ? pos : trajectoryCache.points.back();
    trajectoryCache.flightTime = trajectoryCache.points.size() * 0.016f * 3;
    trajectoryCache.valid = !trajectoryCache.points.empty();
}

void NadeEngine::UpdateAutoAim(GameState* state) {
    // Update throw sequence if active
    if (throwSequenceActive) {
        // UpdateThrowSequence is called from main loop with deltaTime
        return;
    }
    
    // Update trick sequence if active
    if (trickSequenceActive) {
        // UpdateTrickSequence is called from main loop with deltaTime
        return;
    }
    
    if (!autoAimActive || !activeNade || !state) return;
    auto* local = state->GetLocal();
    if (!local) return;
    
    QAngle currentAngle = local->viewAngle;
    QAngle targetAngle = activeNade->aimAngle;
    
    float diffPitch = targetAngle.pitch - currentAngle.pitch;
    float diffYaw = targetAngle.yaw - currentAngle.yaw;
    while (diffYaw > 180) diffYaw -= 360;
    while (diffYaw < -180) diffYaw += 360;
    
    float smooth = nadeHelperAimSpeed;
    QAngle smoothed;
    smoothed.pitch = currentAngle.pitch + diffPitch * smooth;
    smoothed.yaw = currentAngle.yaw + diffYaw * smooth;
    smoothed.Clamp();
    
    float dist = sqrtf(diffPitch * diffPitch + diffYaw * diffYaw);
    if (dist < 0.5f) {
        smoothed = targetAngle;
    }
    
    local->viewAngle = smoothed;
}

// ==================== AUTO-TRICKS ====================

void NadeEngine::LoadTricks() {
    std::vector<MovementTrick> mirage, dust2, nuke, inferno, overpass, anubis, ancient;
    
    auto mkTrick = [](const char* n, const char* m, Vector3 pos, float radius, QAngle face, std::vector<TrickAction> seq, const char* desc) {
        MovementTrick t; t.name=n; t.map=m; t.triggerPos=pos; t.triggerRadius=radius; t.faceDirection=face; t.sequence=seq; t.description=desc; return t;
    };
    
    // ═══════════════ de_mirage tricks ═══════════════
    
    // Window jump from short
    mirage.push_back(mkTrick("Short → Window", "de_mirage", {-580, 380, 132}, 120.0f, {-15, -90, 0}, {
        {VK_CONTROL, 0.0f, 0.0f},   // crouch
        {'D', 0.15f, 0.0f},          // strafe right
        {VK_SPACE, 0.0f, 0.15f},     // jump
        {'D', 0.3f, 0.15f},          // continue strafe
        {VK_CONTROL, 0.0f, 0.45f},   // uncrouch mid-air
    }, "Jump from catwalk to window (crouch-jump)"));
    
    // Underpass jump
    mirage.push_back(mkTrick("Underpass Jump", "de_mirage", {-1100, 200, 132}, 100.0f, {-20, 45, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'A', 0.3f, 0.1f},
    }, "Jump into underpass from mid"));
    
    // B apps → Van jump
    mirage.push_back(mkTrick("Apps → Van", "de_mirage", {400, 2200, 132}, 100.0f, {-10, -170, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.15f},
        {'W', 0.4f, 0.0f},
    }, "Jump from apps to van (run-jump)"));
    
    database["de_mirage"] = std::vector<NadeSpot>();  // keep existing nades
    trickDatabase["de_mirage"] = mirage;
    
    // ═══════════════ de_dust2 tricks ═══════════════
    
    // Xbox jump from lower tunnels
    dust2.push_back(mkTrick("Tunnels → Xbox", "de_dust2", {-680, 1400, 128}, 100.0f, {-15, 90, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'W', 0.35f, 0.0f},
    }, "Jump from lower tunnels to xbox"));
    
    // Short to A site
    dust2.push_back(mkTrick("Short → A Site", "de_dust2", {-350, 1500, 200}, 100.0f, {-25, 135, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.15f},
        {'D', 0.3f, 0.0f},
    }, "Jump from short to A site"));
    
    trickDatabase["de_dust2"] = dust2;
    
    // ═══════════════ de_nuke tricks ═══════════════
    
    // Heaven to rafters
    nuke.push_back(mkTrick("Heaven → Rafters", "de_nuke", {-200, -200, -350}, 100.0f, {-30, 0, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'W', 0.5f, 0.0f},
    }, "Drop from heaven to rafters"));
    
    // Ramp room jump
    nuke.push_back(mkTrick("Ramp Jump", "de_nuke", {0, -800, -400}, 100.0f, {-20, 90, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'A', 0.3f, 0.0f},
    }, "Jump across ramp room"));
    
    trickDatabase["de_nuke"] = nuke;
    
    // ═══════════════ de_inferno tricks ═══════════════
    
    // Apartments jump
    inferno.push_back(mkTrick("Apps → Balcony", "de_inferno", {-1200, 2200, 130}, 100.0f, {-15, -45, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.15f},
        {'W', 0.4f, 0.0f},
    }, "Jump from apartments to balcony"));
    
    // Banana boost
    inferno.push_back(mkTrick("Banana Boost", "de_inferno", {-2000, 1600, 130}, 100.0f, {-10, -10, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
    }, "Jump boost on banana"));
    
    trickDatabase["de_inferno"] = inferno;
    
    // ═══════════════ de_overpass tricks ═══════════════
    
    overpass.push_back(mkTrick("Short → Water", "de_overpass", {200, 1300, 130}, 100.0f, {-20, 30, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'D', 0.3f, 0.0f},
    }, "Jump from short to water"));
    
    trickDatabase["de_overpass"] = overpass;
    
    // ═══════════════ de_anubis tricks ═══════════════
    
    anubis.push_back(mkTrick("Water Jump", "de_anubis", {-400, 600, 130}, 100.0f, {-15, 100, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'W', 0.3f, 0.0f},
    }, "Jump across water"));
    
    trickDatabase["de_anubis"] = anubis;
    
    // ═══════════════ de_ancient tricks ═══════════════
    
    ancient.push_back(mkTrick("Cave Jump", "de_ancient", {-500, 1200, 130}, 100.0f, {-20, 60, 0}, {
        {'W', 0.0f, 0.0f},
        {VK_SPACE, 0.0f, 0.1f},
        {'A', 0.3f, 0.0f},
    }, "Jump through cave"));
    
    trickDatabase["de_ancient"] = ancient;
}

MovementTrick* NadeEngine::GetClosestTrick(Vector3 playerPos, float maxDist) {
    MovementTrick* closest = nullptr;
    float bestDist = maxDist;
    
    for (auto& [map, tricks] : trickDatabase) {
        if (!currentMap.empty() && map != currentMap) continue;
        for (auto& trick : tricks) {
            float dist = playerPos.DistTo(trick.triggerPos);
            if (dist < bestDist) {
                bestDist = dist;
                closest = &trick;
            }
        }
    }
    
    return closest;
}

bool NadeEngine::StartTrick(MovementTrick& trick) {
    if (trick.sequence.empty()) return false;
    
    pendingTrick = &trick;
    trickSequenceActive = true;
    trickSequenceTimer = 0;
    trickSequenceStep = 0;
    
    // Calculate total time
    trickTotalTime = 0;
    for (auto& action : trick.sequence) {
        trickTotalTime = (std::max)(trickTotalTime, action.delayBefore + action.holdTime);
    }
    
    return true;
}

void NadeEngine::UpdateTrickSequence(GameState* state, float deltaTime) {
    if (!trickSequenceActive || !pendingTrick || !state) return;
    
    auto* local = state->GetLocal();
    if (!local) return;
    
    trickSequenceTimer += deltaTime;
    MovementTrick& trick = *pendingTrick;
    
    // First: aim at face direction
    if (trickSequenceStep == 0) {
        AimAtAngle(state, trick.faceDirection, 0.3f);
        
        QAngle delta = trick.faceDirection - local->viewAngle;
        delta.Clamp();
        float dist = sqrtf(delta.pitch * delta.pitch + delta.yaw * delta.yaw);
        
        if (dist < 2.0f || trickSequenceTimer > 1.0f) {
            trickSequenceStep = 1;
            trickSequenceTimer = 0;
        }
        return;
    }
    
    // Execute key sequence
    if (trickSequenceStep == 1) {
        float elapsed = trickSequenceTimer;
        
        for (size_t i = 0; i < trick.sequence.size(); i++) {
            auto& action = trick.sequence[i];
            float actionStart = action.delayBefore;
            float actionEnd = actionStart + action.holdTime;
            
            if (elapsed >= actionStart && elapsed < actionEnd) {
                // Press key if not already pressed
                PressKey(action.key);
            } else if (elapsed >= actionEnd) {
                // Release key
                ReleaseKey(action.key);
            }
        }
        
        // Check if all actions are done
        if (elapsed > trickTotalTime + 0.1f) {
            // Release all keys
            for (auto& action : trick.sequence) {
                ReleaseKey(action.key);
            }
            trickSequenceStep = 2;
        }
    }
    
    // Done
    if (trickSequenceStep == 2) {
        trickSequenceActive = false;
        pendingTrick = nullptr;
    }
}

NadeSpot* NadeEngine::GetNadeByName(const std::string& name) {
    for (auto& [map, nades] : database) {
        for (auto& nade : nades) {
            if (nade.name == name) return &nade;
        }
    }
    return nullptr;
}