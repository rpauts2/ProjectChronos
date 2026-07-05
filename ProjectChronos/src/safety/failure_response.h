#pragma once
#include "core/types.h"
#include <atomic>
#include <thread>
#include <mutex>

// ==================== FAILURE RESPONSE ====================
// Monitors for VAC scanning activity
// Automatically disables cheats when VAC is detected

class FailureResponse {
    std::atomic<bool> vacScanning{false};
    std::atomic<bool> allDisabled{false};
    
    // Detection metrics
    float baseFPS = 0;
    DWORD lastScanTime = 0;
    int scanCount = 0;
    
    // Thread safety for emergency resume
    std::thread resumeThread;
    std::mutex resumeMutex;
    
public:
    FailureResponse() {}
    ~FailureResponse() {
        std::lock_guard<std::mutex> lock(resumeMutex);
        if (resumeThread.joinable()) {
            resumeThread.join();
        }
    }
    
    // Non-copyable
    FailureResponse(const FailureResponse&) = delete;
    FailureResponse& operator=(const FailureResponse&) = delete;
    
    void Update(const GameState& state);
    bool IsVACScanning() const { return vacScanning; }
    bool IsDisabled() const { return allDisabled; }
    
    void EmergencyShutdown();
    void SafeResume();
    
private:
    bool DetectMemoryScan();
    bool DetectFPSSpike();
    bool DetectProcessScan();
};