#include <windows.h>
#include <string>
#include <fstream>
#include <chrono>
#include <ctime>
#include <sstream>

static std::ofstream logFile;
static bool loggingInitialized = false;

void InitLogging() {
    if (loggingInitialized) return;
    
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    std::string logPath = std::string(path) + "chronos_log.txt";
    
    logFile.open(logPath, std::ios::out | std::ios::trunc);
    loggingInitialized = true;
    
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        logFile << "=== Project Chronos Log ===" << std::endl;
        logFile << "Started: " << std::ctime(&t);
    }
}

void LogMessage(const std::string& msg) {
    if (!loggingInitialized) InitLogging();
    if (!logFile.is_open()) return;
    
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &t);
    
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    
    logFile << "[" << buf << "." << ms.count() << "] " << msg << std::endl;
    logFile.flush();
}

void CloseLogging() {
    if (logFile.is_open()) {
        LogMessage("=== Shutdown ===");
        logFile.close();
    }
    loggingInitialized = false;
}