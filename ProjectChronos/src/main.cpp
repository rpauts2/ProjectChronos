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
#include "aimbot/resolver.h"
#include "aimbot/autowall.h"

int main() {
    InitLogging();
    LogMessage("=== Chronos v9 Starting ===");

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

    LogMessage("Loading offsets...");
    OffsetManager offsetMgr(&reader);
    offsetMgr.Update();
    OffsetDatabase offsets = offsetMgr.GetOffsets();
    LogMessage("Offsets loaded, VM=0x" + std::to_string(offsets.dwViewMatrix) + " EL=0x" + std::to_string(offsets.dwEntityList));

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

    ExploitSelector exploitSelector;
    exploitSelector.SetReader(&reader);

    // ---- Ultimate Aim System: Aimbot Layer (19-in-1) ----
    LogMessage("Initializing QuantumAim (19-in-1)...");
    Resolver resolver;
    Autowall autowall;
    QuantumAim quantumAim(&reader, &resolver, &autowall);
    quantumAim.settings.enabled = true;
    quantumAim.settings.aimbot = true;
    quantumAim.settings.rcs = true;
    quantumAim.settings.predictive = true;
    quantumAim.settings.luckEngine = true;
    quantumAim.settings.humanError = true;
    quantumAim.settings.momentumShot = true;
    quantumAim.settings.velocityEngine = true;
    quantumAim.settings.recoilFlow = true;
    quantumAim.settings.decisionEngine = true;

    VACShield vacShield;

    // Frame timing
    float frameTime = 0.016f;
    DWORD lastFrameTime = GetTickCount();

    LogMessage("Entering main loop");

    MSG msg = {};
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) goto cleanup;
        }

        if (GetAsyncKeyState(VK_INSERT) & 1)
            overlay.ToggleMenu();

        if (GetAsyncKeyState(VK_END) & 1)
            break;

        // 1. Update frame timing
        DWORD now = GetTickCount();
        frameTime = (now - lastFrameTime) / 1000.0f;
        if (frameTime < 0.001f) frameTime = 0.016f;
        lastFrameTime = now;

        // 2. Update game state
        stateEngine.Update();
        GameState* state = stateEngine.GetState();

        // 3. Update resolver tracking for all players
        for (int i = 0; i < state->playerCount; i++) {
            resolver.UpdatePlayerData(&state->players[i], i);
        }

        // 4. Record history for sub-tick exploits
        exploitSelector.GetHistory()->Record(*state, GetTickCount());

        // 5. Run the Ultimate Quantum Aim (19 subsystems in 1)
        quantumAim.Update(state, frameTime);

        // 6. Get fake lag decision and forward to packet engine
        if (quantumAim.GetFakeLag()->ShouldSkipPacket()) {
            // Skip this packet for fake lag/desync
        }

        // 7. Get time dilation ping and forward to exploit
        int fakePing = quantumAim.GetTimeDilation()->GetFakePingMs();
        if (fakePing > 20) {
            packetEngine.SetPacketDelay("enemy_move", fakePing - 20);
        }

        // 8. Build situation context for exploit analysis
        SituationContext sit = exploitSelector.BuildContext(state);
        ExploitSolution solution = exploitSelector.Analyze(sit);

        // 9. Merge QuantumAim with exploit selection
        if (quantumAim.HasTarget() && quantumAim.ShouldFire()) {
            solution.shouldShoot = true;
            solution.overrideAngle = true;
            solution.angleOverride = quantumAim.GetAimAngle();
            solution.confidence = quantumAim.GetCurrentHitchance() / 100.0f;
            solution.description = "Quantum: " + std::to_string((int)quantumAim.GetCurrentHitchance()) + "%";
        }

        // 10. Check shot canceller
        if (exploitSelector.ShouldBlockShot(sit, solution)) {
            solution.shouldShoot = false;
            solution.blockShot = true;
        }

        // 11. EXECUTE the exploit + quantum shot
        if (solution.type != EXPLOIT_NONE || solution.shouldShoot || solution.blockShot) {
            executor.Execute(solution, state);
        }

        // 12. Render
        overlay.Render(state, &exploitSelector, nullptr);

        Sleep(1);
    }

cleanup:
    LogMessage("Shutting down...");
    packetEngine.Shutdown();
    overlay.Cleanup();
    reader.Unload();
    CloseLogging();
    return 0;
}
