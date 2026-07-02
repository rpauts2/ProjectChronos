#include "vac_shield.h"
#include "safety/obfuscation.h"
#include <tlhelp32.h>
#include <algorithm>
#include <random>

VACShield::VACShield() {}

VACShield::~VACShield() {
    Stop();
}

void VACShield::Start() {
    monitorThread = std::thread(&VACShield::MonitorLoop, this);
}

void VACShield::Stop() {
    if (monitorThread.joinable())
        monitorThread.join();
}

void VACShield::MonitorLoop() {
    // Randomize initial sleep to avoid boot-time detection
    RandomSleep();

    while (!emergencyShutdown) {
        bool threat = false;

        // Check 1: VAC processes running
        if (CheckVACProcesses()) {
            threat = true;
            scanWarnings += 3;
        }

        // Check 2: Unusual handle enumeration
        if (CheckHandleEnumeration()) {
            threat = true;
            scanWarnings++;
        }

        // Check 3: Debugger presence (anti-analysis)
        if (AntiDebug::IsDebuggerPresent() || AntiDebug::CheckNtGlobalFlag()) {
            threat = true;
            scanWarnings += 5; // Immediate shutdown
        }

        if (threat) {
            lastScanTick = GetTickCount();

            if (scanWarnings >= 5) {
                EmergencyCleanup();
                ResumeAfterCooldown();
            } else {
                SuspendCheats();
            }
        } else {
            // Decay warnings over time
            if (GetTickCount() - lastScanTick > 60000) // 1 minute clean
                scanWarnings = (std::max)(0, scanWarnings - 1);
        }

        // Random interval between checks (2-5 seconds)
        int interval = 2000 + rand() % 3000;
        Sleep(interval);
    }
}

bool VACShield::CheckVACProcesses() {
    // Check for known VAC processes
    const char* targets[] = {
        "steam.exe",
        "steamservice.exe",
        "gameoverlayui.exe",
    };

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    bool found = false;

    if (Process32FirstW(snap, &pe)) {
        do {
            // Convert to lowercase for comparison
            wchar_t lower[256];
            for (int i = 0; pe.szExeFile[i] && i < 255; i++)
                lower[i] = towlower(pe.szExeFile[i]);
            lower[255] = 0;

            for (auto* t : targets) {
                wchar_t wtarget[64];
                for (int i = 0; t[i]; i++)
                    wtarget[i] = t[i];
                wtarget[strlen(t)] = 0;

                if (wcsstr(lower, wtarget)) {
                    found = true;
                    break;
                }
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return found;
}

bool VACShield::CheckHandleEnumeration() {
    // Check if someone opened a handle to our process
    // Simplified: check NtQuerySystemInformation pattern
    // In full version: monitor handle count changes
    return false;
}

bool VACShield::CheckMemoryScanPatterns() {
    // TODO: Check for VAC signature scanning
    return false;
}

bool VACShield::CheckThreadCreation() {
    // Monitor for unexpected thread creation in cs2.exe
    return false;
}

void VACShield::HideProcess() {
    // Toggle process visibility
    // In full version: unhide from toolhelp snapshots
    processHidden = true;
}

void VACShield::SuspendCheats() {
    vacDetected = true;
    HideProcess();
}

void VACShield::EmergencyCleanup() {
    emergencyShutdown = true;
    vacDetected = true;

    // Wipe sensitive memory
    volatile char wipe[4096];
    for (int i = 0; i < 4096; i++)
        wipe[i] = rand() & 0xFF;

    // Signal all threads to stop
    // In full version: send event to main loop
}

void VACShield::ResumeAfterCooldown() {
    // Wait 5-10 minutes before resuming
    int cooldown = 300000 + rand() % 300000; // 5-10 min
    Sleep(cooldown);

    emergencyShutdown = false;
    vacDetected = false;
    scanWarnings = 0;
}