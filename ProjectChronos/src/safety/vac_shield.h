#pragma once
#include "core/types.h"
#include <atomic>
#include <thread>

class VACShield {
    std::atomic<bool> vacDetected{false};
    std::atomic<bool> emergencyShutdown{false};
    std::thread monitorThread;

    // Detection counters
    int scanWarnings = 0;
    DWORD lastScanTick = 0;
    DWORD processCheckTick = 0;

    // Hidden process state
    bool processHidden = false;

public:
    VACShield();
    ~VACShield();

    void Start();
    void Stop();
    bool IsVACDetected() const { return vacDetected; }
    bool IsEmergencyShutdown() const { return emergencyShutdown; }

private:
    void MonitorLoop();

    // Detection methods
    bool CheckVACProcesses();
    bool CheckMemoryScanPatterns();
    bool CheckHandleEnumeration();
    bool CheckThreadCreation();

    // Countermeasures
    void HideProcess();
    void SuspendCheats();
    void EmergencyCleanup();
    void ResumeAfterCooldown();
};