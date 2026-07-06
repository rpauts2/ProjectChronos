#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>

#include "core/memory_reader.h"
#include "core/types.h"
#include "core/state_engine.h"
#include "core/offset_manager.h"
#include "core/config.h"
#include "overlay/overlay.h"
#include "overlay/imgui_setup.h"
#include "exploits/exploit_selector.h"
#include "exploits/executor.h"
#include "exploits/packet_engine.h"
#include "safety/vac_shield.h"
#include "safety/failure_response.h"
#include "utils/logging.h"
#include "aimbot/quantum_aim.h"
#include "aimbot/aim_controller.h"
#include "nade/nade_engine.h"
#include "aimbot/resolver.h"
#include "aimbot/autowall.h"
#include "exploits/skinchanger.h"

// Vectored Exception Handler — logs crash info before death
static LONG WINAPI VehHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void* addr = ep->ExceptionRecord->ExceptionAddress;
    char buf[256];
    snprintf(buf, sizeof(buf),
        "[CRASH] EXCEPTION 0x%08X at addr %p (menu likely caused this)",
        code, addr);
    LogMessage(buf);
    LogMessage("[CRASH] Dump written. Re-throwing to OS.");
    return EXCEPTION_CONTINUE_SEARCH;
}

int main() {
    InitLogging();
    LogMessage("=== Chronos v9 Starting ===");

    // Install VEH to catch crashes
    AddVectoredExceptionHandler(1, VehHandler);

    // Wait for CS2 process
    LogMessage("Waiting for CS2...");
    MemoryReader reader;
    int pid = 0;
    for (int i = 0; i < 300; i++) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
            if (Process32FirstW(snap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
                        pid = pe.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
        if (pid) break;
        Sleep(1000);
    }
    if (!pid) {
        LogMessage("CS2 process not found after 300s");
        return 1;
    }
    LogMessage("CS2 found, PID=" + std::to_string(pid) + ", initializing...");
    if (!reader.Initialize()) {
        LogMessage("MemoryReader init FAILED");
        return 1;
    }
    LogMessage("MemoryReader ready, PID=" + std::to_string(reader.GetPID()));

    LogMessage("Loading offsets (auto-download from cs2-dumper)...");
    OffsetManager offsetMgr(&reader);
    bool offsetsOK = offsetMgr.Update();
    OffsetDatabase offsets = offsetMgr.GetOffsets();
    LogMessage("Offsets: " + offsetMgr.GetStatus());
    LogMessage("  VM=0x" + std::to_string(offsets.dwViewMatrix) +
               " EL=0x" + std::to_string(offsets.dwEntityList) +
               " LP=0x" + std::to_string(offsets.dwLocalPlayerPawn));
    
    if (!offsetsOK || offsets.dwViewMatrix == 0 || offsets.dwEntityList == 0) {
        LogMessage("[FATAL] Critical offsets missing — cannot continue safely");
        LogMessage("[FATAL] Check internet connection or try again later");
        return 1;
    }

    LogMessage("Initializing StateEngine...");
    StateEngine stateEngine(&reader);
    stateEngine.SetOffsets(offsets);
    LogMessage("StateEngine OK");

    LogMessage("Creating overlay...");
    Overlay overlay;
    if (!overlay.Create()) {
        LogMessage("Overlay creation FAILED");
        reader.Unload();
        return 1;
    }
    LogMessage("Overlay created");

    // ---- Ultimate Aim System: Exploit Layer ----
    LogMessage("Initializing exploit layer...");

    InputHistoryExploit inputHistory(&reader);
    inputHistory.Initialize();

    PacketEngine packetEngine;
    bool packetEngineOK = packetEngine.Initialize();
    if (packetEngineOK) {
        packetEngine.StartCapture();
        LogMessage("PacketEngine: WinDivert capturing");
    } else {
        LogMessage("PacketEngine: WinDivert unavailable (run as admin)");
    }

    Executor executor(&reader);
    executor.SetInputHistory(&inputHistory);
    executor.SetPacketEngine(&packetEngine);
    executor.SetOffsets(offsets);
    inputHistory.SetOffsets(offsets);

    ExploitSelector exploitSelector;
    exploitSelector.SetReader(&reader);

    // ---- Grenade Helper ----
    NadeEngine* nadeEngine = new NadeEngine();
    nadeEngine->LoadDefaultDatabase();

    // ---- Ultimate Aim System: Aimbot Layer (Clean) ----
    LogMessage("Initializing AimController...");
    Resolver resolver;
    Autowall autowall;
    AimController* aimController = new AimController(&reader, &resolver, &autowall);
    aimController->SetOffsets(offsets);
    aimController->settings.enabled = true;
    aimController->settings.aimbot = true;
    aimController->settings.rcs = true;
    aimController->settings.predictive = true;
    aimController->settings.smoothEnabled = true;
    aimController->settings.backtrackEnabled = true;
    aimController->settings.screenWidth = 1920;
    aimController->settings.screenHeight = 1080;

    VACShield vacShield;

    SkinChanger skinChanger(&reader);
    skinChanger.SetOffsets(offsets);
    skinChanger.LoadDefaultSkins();

    Config config;
    auto loadConfig = [&]() {
        if (config.Load("default")) {
            overlay.settings.showBox = config.GetBool("showBox", true);
            overlay.settings.showName = config.GetBool("showName", true);
            overlay.settings.showHealth = config.GetBool("showHealth", true);
            overlay.settings.showWeapon = config.GetBool("showWeapon", true);
            overlay.settings.showSkeleton = config.GetBool("showSkeleton", false);
            overlay.settings.showArmor = config.GetBool("showArmor", false);
            overlay.settings.showFlags = config.GetBool("showFlags", true);
            overlay.settings.showDistance = config.GetBool("showDistance", false);
            overlay.settings.showSnaplines = config.GetBool("showSnaplines", false);
            overlay.settings.showRadar = config.GetBool("showRadar", true);
            overlay.settings.showGlow = config.GetBool("showGlow", false);
            overlay.settings.showInfoPanel = config.GetBool("showInfoPanel", true);
            overlay.settings.showHeadDot = config.GetBool("showHeadDot", true);
            overlay.settings.showChams = config.GetBool("showChams", false);
            overlay.settings.showAimline = config.GetBool("showAimline", true);
            overlay.settings.showOOVIndicators = config.GetBool("showOOVIndicators", true);
            overlay.settings.showMoney = config.GetBool("showMoney", true);
            overlay.settings.showBombInfo = config.GetBool("showBombInfo", true);
            overlay.settings.showDeadSkulls = config.GetBool("showDeadSkulls", true);
            overlay.settings.showHitMarker = config.GetBool("showHitMarker", false);
            overlay.settings.showSpectators = config.GetBool("showSpectators", false);
            overlay.settings.showVelocity = config.GetBool("showVelocity", false);
            overlay.settings.showRecoilCrosshair = config.GetBool("showRecoilCrosshair", false);
            overlay.settings.showScopeOverlay = config.GetBool("showScopeOverlay", false);
            overlay.settings.showDroppedWeapons = config.GetBool("showDroppedWeapons", false);
            overlay.settings.showSoundESP = config.GetBool("showSoundESP", false);
            overlay.settings.showCrosshair = config.GetBool("showCrosshair", true);
            overlay.settings.showTrajectory = config.GetBool("showTrajectory", true);
            overlay.settings.showAmmo = config.GetBool("showAmmo", true);
            overlay.settings.showWatermark = config.GetBool("showWatermark", true);
            overlay.settings.useWeaponIcons = config.GetBool("useWeaponIcons", true);
            overlay.settings.healthBasedColor = config.GetBool("healthBasedColor", true);
            overlay.settings.healthStyle = config.GetInt("healthStyle", 0);
            overlay.settings.boxStyle = config.GetInt("boxStyle", 1);
            overlay.settings.boxWidthRatio = config.GetFloat("boxWidthRatio", 0.40f);
            overlay.settings.boxThickness = config.GetFloat("boxThickness", 1.5f);
            overlay.settings.snaplineThickness = config.GetFloat("snaplineThickness", 1.0f);
            overlay.settings.radarSize = config.GetInt("radarSize", 200);
            overlay.settings.radarScale = config.GetFloat("radarScale", 0.5f);
            overlay.settings.radarRotate = config.GetBool("radarRotate", true);
            overlay.settings.radarShowTeam = config.GetBool("radarShowTeam", false);
            overlay.settings.nadeHelperEnabled = config.GetBool("nadeHelperEnabled", true);
            overlay.settings.autoTrickEnabled = config.GetBool("autoTrickEnabled", true);
            overlay.settings.knifeBotEnabled = config.GetBool("knifeBotEnabled", false);
            overlay.settings.quickStopEnabled = config.GetBool("quickStopEnabled", false);
            overlay.settings.quickSwitchEnabled = config.GetBool("quickSwitchEnabled", false);
            overlay.settings.autoDefuseEnabled = config.GetBool("autoDefuseEnabled", false);
            overlay.settings.fakeDuckEnabled = config.GetBool("fakeDuckEnabled", false);
            overlay.settings.bhopEnabled = config.GetBool("bhopEnabled", false);
            overlay.settings.bhopHitchance = config.GetInt("bhopHitchance", 80);
            overlay.settings.bhopAutoStrafe = config.GetBool("bhopAutoStrafe", false);
            overlay.settings.antiAimEnabled = config.GetBool("antiAimEnabled", false);
            overlay.settings.antiAimPitchEnabled = config.GetBool("antiAimPitchEnabled", false);
            overlay.settings.antiAimPitch = config.GetInt("antiAimPitch", 0);
            overlay.settings.antiAimYaw = config.GetInt("antiAimYaw", 0);
            overlay.settings.fakeLatencyEnabled = config.GetBool("fakeLatencyEnabled", false);
            overlay.settings.fakeLatencyAmount = config.GetFloat("fakeLatencyAmount", 100.0f);
            overlay.settings.knifeBotRange = config.GetFloat("knifeBotRange", 80.0f);
            overlay.settings.clanTagEnabled = config.GetBool("clanTagEnabled", false);
            overlay.settings.skinChangerEnabled = config.GetBool("skinChangerEnabled", true);
            overlay.settings.thirdPerson = config.GetBool("thirdPerson", false);
            overlay.settings.nadeHelperKey = config.GetInt("nadeHelperKey", 0x47);
            overlay.settings.nadeHelperThrowKey = config.GetInt("nadeHelperThrowKey", 'V');
            overlay.settings.nadeHelperRadius = config.GetFloat("nadeHelperRadius", 500.0f);
            overlay.settings.nadeHelperAimSpeed = config.GetFloat("nadeHelperAimSpeed", 0.3f);
            overlay.settings.autoTrickKey = config.GetInt("autoTrickKey", 0x54);
            overlay.settings.autoTrickRadius = config.GetFloat("autoTrickRadius", 150.0f);
            for (int c = 0; c < 4; c++) {
                overlay.settings.boxColor[c] = config.GetFloat("boxColor" + std::to_string(c), overlay.settings.boxColor[c]);
                overlay.settings.nameColor[c] = config.GetFloat("nameColor" + std::to_string(c), overlay.settings.nameColor[c]);
                overlay.settings.weaponColor[c] = config.GetFloat("weaponColor" + std::to_string(c), overlay.settings.weaponColor[c]);
                overlay.settings.skeletonColor[c] = config.GetFloat("skeletonColor" + std::to_string(c), overlay.settings.skeletonColor[c]);
                overlay.settings.glowColor[c] = config.GetFloat("glowColor" + std::to_string(c), overlay.settings.glowColor[c]);
                overlay.settings.snaplineColor[c] = config.GetFloat("snaplineColor" + std::to_string(c), overlay.settings.snaplineColor[c]);
                overlay.settings.crosshairColor[c] = config.GetFloat("crosshairColor" + std::to_string(c), overlay.settings.crosshairColor[c]);
                overlay.settings.droppedWeaponColor[c] = config.GetFloat("droppedWeaponColor" + std::to_string(c), overlay.settings.droppedWeaponColor[c]);
            }
            aimController->settings.enabled = config.GetBool("aimEnabled", true);
            aimController->settings.aimbot = config.GetBool("aimbot", true);
            aimController->settings.rcs = config.GetBool("aimRcs", true);
            aimController->settings.predictive = config.GetBool("aimPredictive", true);
            aimController->settings.smoothEnabled = config.GetBool("aimSmooth", true);
            aimController->settings.backtrackEnabled = config.GetBool("aimBacktrack", true);
            aimController->settings.fov = config.GetFloat("aimFov", 40.0f);
            aimController->settings.smoothSpeed = config.GetFloat("aimSmoothSpeed", 25.0f);
            aimController->settings.mouseSensitivity = config.GetFloat("aimSensitivity", 1.0f);
            aimController->settings.autoFire = config.GetBool("aimAutoFire", true);
            aimController->settings.triggerEnabled = config.GetBool("aimTrigger", false);
            aimController->settings.rageMode = config.GetBool("aimRage", false);
            aimController->settings.fireMode = config.GetInt("aimFireMode", 0);
            LogMessage("[CONFIG] Loaded default config");
        } else {
            LogMessage("[CONFIG] No default config found, using defaults");
        }
    };
    auto saveConfig = [&]() {
        config.SetBool("showBox", overlay.settings.showBox);
        config.SetBool("showName", overlay.settings.showName);
        config.SetBool("showHealth", overlay.settings.showHealth);
        config.SetBool("showWeapon", overlay.settings.showWeapon);
        config.SetBool("showSkeleton", overlay.settings.showSkeleton);
        config.SetBool("showArmor", overlay.settings.showArmor);
        config.SetBool("showFlags", overlay.settings.showFlags);
        config.SetBool("showDistance", overlay.settings.showDistance);
        config.SetBool("showSnaplines", overlay.settings.showSnaplines);
        config.SetBool("showRadar", overlay.settings.showRadar);
        config.SetBool("showGlow", overlay.settings.showGlow);
        config.SetBool("showInfoPanel", overlay.settings.showInfoPanel);
        config.SetBool("showHeadDot", overlay.settings.showHeadDot);
        config.SetBool("showChams", overlay.settings.showChams);
        config.SetBool("showAimline", overlay.settings.showAimline);
        config.SetBool("showOOVIndicators", overlay.settings.showOOVIndicators);
        config.SetBool("showMoney", overlay.settings.showMoney);
        config.SetBool("showBombInfo", overlay.settings.showBombInfo);
        config.SetBool("showDeadSkulls", overlay.settings.showDeadSkulls);
        config.SetBool("showHitMarker", overlay.settings.showHitMarker);
        config.SetBool("showSpectators", overlay.settings.showSpectators);
        config.SetBool("showVelocity", overlay.settings.showVelocity);
        config.SetBool("showRecoilCrosshair", overlay.settings.showRecoilCrosshair);
        config.SetBool("showScopeOverlay", overlay.settings.showScopeOverlay);
        config.SetBool("showDroppedWeapons", overlay.settings.showDroppedWeapons);
        config.SetBool("showSoundESP", overlay.settings.showSoundESP);
        config.SetBool("showCrosshair", overlay.settings.showCrosshair);
        config.SetBool("showTrajectory", overlay.settings.showTrajectory);
        config.SetBool("showAmmo", overlay.settings.showAmmo);
        config.SetBool("showWatermark", overlay.settings.showWatermark);
        config.SetBool("useWeaponIcons", overlay.settings.useWeaponIcons);
        config.SetBool("healthBasedColor", overlay.settings.healthBasedColor);
        config.SetInt("healthStyle", overlay.settings.healthStyle);
        config.SetInt("boxStyle", overlay.settings.boxStyle);
        config.SetFloat("boxWidthRatio", overlay.settings.boxWidthRatio);
        config.SetFloat("boxThickness", overlay.settings.boxThickness);
        config.SetFloat("snaplineThickness", overlay.settings.snaplineThickness);
        config.SetInt("radarSize", overlay.settings.radarSize);
        config.SetFloat("radarScale", overlay.settings.radarScale);
        config.SetBool("radarRotate", overlay.settings.radarRotate);
        config.SetBool("radarShowTeam", overlay.settings.radarShowTeam);
        config.SetBool("nadeHelperEnabled", overlay.settings.nadeHelperEnabled);
        config.SetBool("autoTrickEnabled", overlay.settings.autoTrickEnabled);
        config.SetBool("knifeBotEnabled", overlay.settings.knifeBotEnabled);
        config.SetBool("quickStopEnabled", overlay.settings.quickStopEnabled);
        config.SetBool("quickSwitchEnabled", overlay.settings.quickSwitchEnabled);
        config.SetBool("autoDefuseEnabled", overlay.settings.autoDefuseEnabled);
        config.SetBool("fakeDuckEnabled", overlay.settings.fakeDuckEnabled);
        config.SetBool("bhopEnabled", overlay.settings.bhopEnabled);
        config.SetInt("bhopHitchance", overlay.settings.bhopHitchance);
        config.SetBool("bhopAutoStrafe", overlay.settings.bhopAutoStrafe);
        config.SetBool("antiAimEnabled", overlay.settings.antiAimEnabled);
        config.SetBool("antiAimPitchEnabled", overlay.settings.antiAimPitchEnabled);
        config.SetInt("antiAimPitch", overlay.settings.antiAimPitch);
        config.SetInt("antiAimYaw", overlay.settings.antiAimYaw);
        config.SetBool("fakeLatencyEnabled", overlay.settings.fakeLatencyEnabled);
        config.SetFloat("fakeLatencyAmount", overlay.settings.fakeLatencyAmount);
        config.SetFloat("knifeBotRange", overlay.settings.knifeBotRange);
        config.SetBool("clanTagEnabled", overlay.settings.clanTagEnabled);
        config.SetBool("skinChangerEnabled", overlay.settings.skinChangerEnabled);
        config.SetBool("thirdPerson", overlay.settings.thirdPerson);
        config.SetInt("nadeHelperKey", overlay.settings.nadeHelperKey);
        config.SetInt("nadeHelperThrowKey", overlay.settings.nadeHelperThrowKey);
        config.SetFloat("nadeHelperRadius", overlay.settings.nadeHelperRadius);
        config.SetFloat("nadeHelperAimSpeed", overlay.settings.nadeHelperAimSpeed);
        config.SetInt("autoTrickKey", overlay.settings.autoTrickKey);
        config.SetFloat("autoTrickRadius", overlay.settings.autoTrickRadius);
        for (int c = 0; c < 4; c++) {
            config.SetFloat("boxColor" + std::to_string(c), overlay.settings.boxColor[c]);
            config.SetFloat("nameColor" + std::to_string(c), overlay.settings.nameColor[c]);
            config.SetFloat("weaponColor" + std::to_string(c), overlay.settings.weaponColor[c]);
            config.SetFloat("skeletonColor" + std::to_string(c), overlay.settings.skeletonColor[c]);
            config.SetFloat("glowColor" + std::to_string(c), overlay.settings.glowColor[c]);
            config.SetFloat("snaplineColor" + std::to_string(c), overlay.settings.snaplineColor[c]);
            config.SetFloat("crosshairColor" + std::to_string(c), overlay.settings.crosshairColor[c]);
            config.SetFloat("droppedWeaponColor" + std::to_string(c), overlay.settings.droppedWeaponColor[c]);
        }
        config.SetBool("aimEnabled", aimController->settings.enabled);
        config.SetBool("aimbot", aimController->settings.aimbot);
        config.SetBool("aimRcs", aimController->settings.rcs);
        config.SetBool("aimPredictive", aimController->settings.predictive);
        config.SetBool("aimSmooth", aimController->settings.smoothEnabled);
        config.SetBool("aimBacktrack", aimController->settings.backtrackEnabled);
        config.SetFloat("aimFov", aimController->settings.fov);
        config.SetFloat("aimSmoothSpeed", aimController->settings.smoothSpeed);
        config.SetFloat("aimSensitivity", aimController->settings.mouseSensitivity);
        config.SetBool("aimAutoFire", aimController->settings.autoFire);
        config.SetBool("aimTrigger", aimController->settings.triggerEnabled);
        config.SetBool("aimRage", aimController->settings.rageMode);
        config.SetInt("aimFireMode", aimController->settings.fireMode);
        if (config.Save("default")) {
            LogMessage("[CONFIG] Saved default config");
        }
    };

    loadConfig();

    // Frame timing
    float frameTime = 0.016f;
    static LARGE_INTEGER perfFreq = {};
    static bool perfInited = false;
    if (!perfInited) { QueryPerformanceFrequency(&perfFreq); perfInited = true; }
    LARGE_INTEGER perfNow = {};
    QueryPerformanceCounter(&perfNow);
    double lastPerfTime = (double)perfNow.QuadPart / (double)perfFreq.QuadPart;
    int frameCount = 0;

    LogMessage("Entering main loop");

    MSG msg = {};
    while (true) {
        // Process pending window style changes BEFORE message pump
        overlay.ApplyPendingStyle();

        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) goto cleanup;
        }

        if (GetAsyncKeyState(VK_INSERT) & 1) {
            static DWORD lastInsert = 0;
            DWORD now = GetTickCount();
            if (now - lastInsert > 200) {
                LogMessage("[MAIN] INSERT pressed, toggling menu");
                overlay.ToggleMenu();
                lastInsert = now;
            }
        }

        if (GetAsyncKeyState(VK_END) & 1)
            break;

        if (GetAsyncKeyState(VK_DELETE) & 1) {
            static DWORD lastDelete = 0;
            DWORD now = GetTickCount();
            if (now - lastDelete > 500) {
                LogMessage("[MAIN] DELETE pressed, saving config");
                saveConfig();
                lastDelete = now;
            }
        }

        // 1. Update frame timing
        LARGE_INTEGER perfFrame = {};
        QueryPerformanceCounter(&perfFrame);
        double nowPerf = (double)perfFrame.QuadPart / (double)perfFreq.QuadPart;
        frameTime = (float)(nowPerf - lastPerfTime);
        if (frameTime < 0.001f) frameTime = 0.016f;
        lastPerfTime = nowPerf;

        // 2. Update game state
        stateEngine.Update();
        GameState* state = stateEngine.GetState();
        state->nadeEngine = nadeEngine;
        nadeEngine->SetCurrentMap(state->mapName);

        // 3. Update resolver tracking for all players
        for (int i = 0; i < state->playerCount; i++) {
            resolver.UpdatePlayerData(&state->players[i], i);
        }

        // 4. Record history for sub-tick exploits
        exploitSelector.GetHistory()->Record(*state, GetTickCount());

        // 5. Run the Clean AimController
        aimController->Update(state, frameTime);

        // 5a. Anti-Aim: offset view angles to make hitscan harder
        if (overlay.settings.antiAimEnabled) {
            auto* localAA = state ? state->GetLocal() : nullptr;
            if (localAA && localAA->health > 0 && !aimController->HasTarget()) {
                QAngle antiAngles = localAA->viewAngle;
                // Pitch manipulation
                if (overlay.settings.antiAimPitchEnabled) {
                    switch (overlay.settings.antiAimPitch) {
                        case 1: antiAngles.pitch = 89.0f; break;   // Up
                        case 2: antiAngles.pitch = -89.0f; break;  // Down
                        case 3: antiAngles.pitch = (float)((rand() % 178) - 89); break; // Random
                    }
                }
                // Yaw manipulation
                switch (overlay.settings.antiAimYaw) {
                    case 1: antiAngles.yaw -= 90.0f; break;   // Left
                    case 2: antiAngles.yaw += 90.0f; break;   // Right
                    case 3: antiAngles.yaw += 180.0f; break;  // Back
                    case 4: antiAngles.yaw += (float)(rand() % 360); break; // Random
                }
                antiAngles.Clamp();
            }
        }

        // 5b. Update nade engine auto-aim and trajectory
        nadeEngine->UpdateAutoAim(state);
        overlay.nadeEnginePtr = nadeEngine;

        // 5c. Update throw sequence if active
        if (nadeEngine->IsThrowing()) {
            nadeEngine->UpdateThrowSequence(state, frameTime);
        }

        // 5d. Update trick sequence if active
        if (nadeEngine->IsTricking()) {
            nadeEngine->UpdateTrickSequence(state, frameTime);
        }

        // 5e. Nade helper keybind: toggle nearby spots display
        if (overlay.settings.nadeHelperEnabled) {
            auto* localP = state ? state->GetLocal() : nullptr;
            if (localP && localP->health > 0) {
                if (GetAsyncKeyState(overlay.settings.nadeHelperKey) & 1) {
                    // Toggle nade helper display (already shown via RenderNadeUI)
                }
                // Throw key: start throw sequence for closest spot
                if (GetAsyncKeyState(overlay.settings.nadeHelperThrowKey) & 1) {
                    if (!nadeEngine->IsThrowing()) {
                        NadeSpot* closest = nadeEngine->GetClosestNade(localP->origin, overlay.settings.nadeHelperRadius);
                        if (closest) {
                            nadeEngine->nadeHelperAimSpeed = overlay.settings.nadeHelperAimSpeed;
                            nadeEngine->StartThrowSequence(*closest);
                        }
                    }
                }
            }
        }

        // 5f. Auto-trick keybind: execute movement trick
        if (overlay.settings.autoTrickEnabled) {
            auto* localP2 = state ? state->GetLocal() : nullptr;
            if (localP2 && localP2->health > 0) {
                if (GetAsyncKeyState(overlay.settings.autoTrickKey) & 1) {
                    if (!nadeEngine->IsTricking()) {
                        MovementTrick* trick = nadeEngine->GetClosestTrick(localP2->origin, overlay.settings.autoTrickRadius);
                        if (trick) {
                            nadeEngine->StartTrick(*trick);
                        }
                    }
                }
            }
        }

        // 5g. Quick Stop: release movement keys when shooting for accuracy
        if (overlay.settings.quickStopEnabled) {
            auto* localQS = state ? state->GetLocal() : nullptr;
            if (localQS && localQS->health > 0 && aimController->HasTarget()) {
                if (aimController->WasShotFired()) {
                    keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
                    keybd_event('S', 0, KEYEVENTF_KEYUP, 0);
                    keybd_event('A', 0, KEYEVENTF_KEYUP, 0);
                    keybd_event('D', 0, KEYEVENTF_KEYUP, 0);
                }
            }
        }

        // 5h. Quick Switch: press Q after shooting
        if (overlay.settings.quickSwitchEnabled) {
            auto* localQS2 = state ? state->GetLocal() : nullptr;
            if (localQS2 && localQS2->health > 0 && aimController->WasShotFired()) {
                static DWORD lastQSTime = 0;
                DWORD nowQS = GetTickCount();
                if (nowQS - lastQSTime > 50) {
                    keybd_event('Q', 0, 0, 0);
                    keybd_event('Q', 0, KEYEVENTF_KEYUP, 0);
                    lastQSTime = nowQS;
                }
            }
        }

        aimController->ResetShotFlag();

        // 5i. Fake Duck: rapid crouch toggle
        if (overlay.settings.fakeDuckEnabled) {
            auto* localFD = state ? state->GetLocal() : nullptr;
            if (localFD && localFD->health > 0) {
                static bool duckState = false;
                static DWORD lastDuckTime = 0;
                DWORD nowDuck = GetTickCount();
                if (nowDuck - lastDuckTime > 16) {
                    if (duckState) {
                        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                    } else {
                        keybd_event(VK_CONTROL, 0, 0, 0);
                    }
                    duckState = !duckState;
                    lastDuckTime = nowDuck;
                }
            }
        }

        // 5i-b. BunnyHop: auto jump when space held and on ground
        if (overlay.settings.bhopEnabled) {
            auto* localBH = state ? state->GetLocal() : nullptr;
            if (localBH && localBH->health > 0 && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                int flags = localBH->flags;
                if (flags & 1) {
                    int roll = rand() % 100;
                    if (roll < overlay.settings.bhopHitchance) {
                        keybd_event(VK_SPACE, 0, 0, 0);
                        keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
                    }
                }
            }
        }

        // 5j. Knife Bot: auto-stab when enemy is within knife range
        if (overlay.settings.knifeBotEnabled) {
            auto* localKB = state ? state->GetLocal() : nullptr;
            if (localKB && localKB->health > 0) {
                static DWORD lastKnifeTime = 0;
                DWORD nowKnife = GetTickCount();
                if (nowKnife - lastKnifeTime >= 400) {
                    bool isKnife = (localKB->weaponId == 42 || localKB->weaponId == 59);
                    if (isKnife) {
                        for (int i = 0; i < 64; i++) {
                            auto& p = state->players[i];
                            if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;
                            float dist = p.origin.DistTo(localKB->origin);
                            if (dist < overlay.settings.knifeBotRange) {
                                if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                                    lastKnifeTime = nowKnife;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }

        // 5k. Fake Latency: shift CUserCmd tick count back to simulate lag
        if (overlay.settings.fakeLatencyEnabled) {
            auto* localFL = state ? state->GetLocal() : nullptr;
            if (localFL && localFL->health > 0) {
                uintptr_t client = reader.GetClient();
                if (client) {
                    uintptr_t inputSystem = reader.Read<uintptr_t>(client + offsets.dwInputSystem);
                    if (inputSystem) {
                        uintptr_t commandsPtr = reader.Read<uintptr_t>(inputSystem + offsets.m_pCommands);
                        int cmdNum = reader.Read<int>(inputSystem + offsets.m_nCmdCount);
                        if (commandsPtr && cmdNum > 0) {
                            int tickShift = (int)(overlay.settings.fakeLatencyAmount / 15.0f);
                            if (tickShift < 1) tickShift = 1;
                            if (tickShift > 16) tickShift = 16;
                            int idx = cmdNum % 150;
                            uintptr_t cmdAddr = commandsPtr + idx * offsets.m_cmdSize;
                            int curTick = reader.Read<int>(cmdAddr + offsets.m_nTickCount);
                            reader.Write<int>(cmdAddr + offsets.m_nTickCount, curTick - tickShift);
                        }
                    }
                }
            }
        }

        // 5l. Clan Tag: animated tag via writing to controller's m_szClan
        if (overlay.settings.clanTagEnabled) {
            static DWORD lastClanTagTime = 0;
            DWORD nowCT = GetTickCount();
            if (nowCT - lastClanTagTime >= 1500) {
                auto* localCT = state ? state->GetLocal() : nullptr;
                if (localCT && localCT->health > 0) {
                    static int tagPhase = 0;
                    const char* tags[] = {
                        "  C H R O N O S  ",
                        " C H R O N O S  ",
                        "CHRONOS ",
                        " HRONOS  ",
                        "RONOS   ",
                        "ONOS    ",
                        "NOS     ",
                        "OS      ",
                        "S       ",
                        "        ",
                        "S       ",
                        "OS      ",
                        "NOS     ",
                        "ONOS    ",
                        "RONOS   ",
                        " HRONOS  ",
                    };
                    int tagCount = sizeof(tags) / sizeof(tags[0]);
                    const char* tag = tags[tagPhase % tagCount];
                    // Write clan tag string to controller via CUtlString
                    // m_szClan is at offset in controller, it's a pointer to string
                    uintptr_t clanStrPtr = reader.Read<uintptr_t>(localCT->controllerAddr + offsets.m_szClan);
                    if (clanStrPtr && clanStrPtr > 0x10000) {
                        // Write directly - CUtlString stores a char* internally
                        char buf[32] = {};
                        strncpy(buf, tag, 31);
                        reader.WriteBuffer(clanStrPtr, buf, strlen(tag) + 1);
                    }
                    tagPhase++;
                    lastClanTagTime = nowCT;
                }
            }
        }

        // 5m. Auto Defuse: press USE when near planted bomb
        if (overlay.settings.autoDefuseEnabled) {
            auto* localAD = state ? state->GetLocal() : nullptr;
            if (localAD && localAD->health > 0 && state->bombPlanted) {
                static DWORD lastDefuseTime = 0;
                DWORD nowDefuse = GetTickCount();
                if (nowDefuse - lastDefuseTime >= 200) {
                    float distToBomb = localAD->origin.DistTo(state->bombPos);
                    if (distToBomb < 120.0f) {
                        bool enemiesNearby = false;
                        for (int i = 0; i < 64; i++) {
                            auto& p = state->players[i];
                            if (!p.IsValid() || !p.IsEnemy(state->localTeam)) continue;
                            if (p.origin.DistTo(state->bombPos) < 500.0f) {
                                enemiesNearby = true;
                                break;
                            }
                        }
                        if (!enemiesNearby) {
                            if (!(GetAsyncKeyState('E') & 0x8000)) {
                                keybd_event('E', 0, 0, 0);
                                keybd_event('E', 0, KEYEVENTF_KEYUP, 0);
                                lastDefuseTime = nowDefuse;
                            }
                        }
                    }
                }
            }
        }

        // 5n. Third Person: toggle camera via writing to camera services
        if (overlay.settings.thirdPerson) {
            auto* localTP = state ? state->GetLocal() : nullptr;
            if (localTP && localTP->health > 0) {
                uintptr_t camServices = reader.Read<uintptr_t>(localTP->pawnAddr + offsets.m_pCameraServices);
                if (camServices && camServices > 0x10000) {
                    // Check if already in third person, if not enable it
                    float curDist = reader.Read<float>(camServices + offsets.m_vecThirdPersonViewOffset);
                    if (curDist < 1.0f) {
                        // Set third person distance
                        reader.Write<float>(camServices + offsets.m_vecThirdPersonViewOffset, 150.0f);
                    }
                }
            }
        } else {
            auto* localTP = state ? state->GetLocal() : nullptr;
            if (localTP && localTP->health > 0) {
                uintptr_t camServices = reader.Read<uintptr_t>(localTP->pawnAddr + offsets.m_pCameraServices);
                if (camServices && camServices > 0x10000) {
                    float curDist = reader.Read<float>(camServices + offsets.m_vecThirdPersonViewOffset);
                    if (curDist > 1.0f) {
                        reader.Write<float>(camServices + offsets.m_vecThirdPersonViewOffset, 0.0f);
                    }
                }
            }
        }

        // 5o. Skin Changer: apply custom skins to weapons
        skinChanger.SetEnabled(overlay.settings.skinChangerEnabled);
        if (skinChanger.IsEnabled()) {
            skinChanger.ApplySkins(state);
        }

        // 6. Get fake lag decision and forward to packet engine
        // (AimController handles this internally)

        // 7. Get time dilation ping and forward to exploit
        // (AimController handles this internally)

        // 8. Build situation context for exploit analysis
        SituationContext sit = exploitSelector.BuildContext(state);
        ExploitSolution solution = exploitSelector.Analyze(sit);

        // 9. AimController handles shot execution directly in Update()
        // (writes angle + IN_ATTACK to CUserCmd atomically)

        // 10. Check shot canceller
        if (exploitSelector.ShouldBlockShot(sit, solution)) {
            solution.blockShot = true;
            if (solution.blockShot) {
                executor.Execute(solution, state);
            }
        }

        // 12. Render
        overlay.Render(state, &exploitSelector, aimController);

        // Log every 500th frame to track liveness
        frameCount++;
        if (frameCount % 500 == 0) {
            LogMessage("[LOOP] frame " + std::to_string(frameCount) + " ok, menuOpen=" + std::to_string(overlay.IsMenuOpen()));
        }

        Sleep(1);
    }

cleanup:
    LogMessage("Shutting down...");
    packetEngine.Shutdown();
    overlay.Cleanup();
    delete aimController;
    delete nadeEngine;
    reader.Unload();
    CloseLogging();
    return 0;
}
