#pragma once
#include "types.h"
#include <string>
#include <vector>
#include <cstdint>

// Simple IOCTL code — no complex macro needed
#define CHRONOS_IOCTL_READ 0x80000000
#define CHRONOS_IOCTL_WRITE 0x80000001

struct CHRONOS_READ_REQUEST {
    uint32_t  pid;
    uint64_t  address;
    size_t    size;
};

struct CHRONOS_READ_RESPONSE {
    size_t bytesRead;
};

struct CHRONOS_WRITE_REQUEST {
    uint32_t  pid;
    uint64_t  address;
    size_t    size;
};

class MemoryReader {
    HANDLE hDriver = INVALID_HANDLE_VALUE;
    HANDLE hProcess = NULL;
    bool loaded = false;
    bool useRPM = false; // fallback to ReadProcessMemory when driver unavailable
    DWORD pid = 0;
    uintptr_t clientBase = 0;
    uintptr_t engineBase = 0;
    uintptr_t serverBase = 0;
    uintptr_t inputBase = 0;

    bool ReadRPM(uintptr_t addr, void* buffer, size_t size);

public:
    MemoryReader();
    ~MemoryReader();

    bool Initialize();
    bool Load();
    void Unload();

    template<typename T>
    T Read(uintptr_t addr) {
        T value = {};
        if (!loaded) return value;
        if (useRPM) {
            ReadRPM(addr, &value, sizeof(T));
            return value;
        }
        CHRONOS_READ_REQUEST req = { (uint32_t)pid, (uint64_t)addr, sizeof(T) };
        BYTE buf[sizeof(CHRONOS_READ_RESPONSE) + sizeof(T)] = {};
        DWORD returned = 0;
        if (DeviceIoControl(hDriver, CHRONOS_IOCTL_READ,
            &req, sizeof(req), buf, sizeof(buf), &returned, nullptr)) {
            memcpy(&value, buf + sizeof(CHRONOS_READ_RESPONSE), sizeof(T));
        }
        return value;
    }

    template<typename T> T ReadOffset(uintptr_t offset) { return Read<T>(clientBase + offset); }
    template<typename T> T ReadPtr(uintptr_t base, uintptr_t offset) { return Read<T>(base + offset); }

    // Write via driver or fallback to WriteProcessMemory
    template<typename T> bool Write(uintptr_t addr, T value) {
        return WriteBuffer(addr, &value, sizeof(T));
    }

    bool ReadBuffer(uintptr_t addr, void* buffer, size_t size);
    bool WriteBuffer(uintptr_t addr, void* buffer, size_t size);
    uintptr_t GetModuleBase(const std::string& moduleName);
    uintptr_t GetClient() const { return clientBase; }
    uintptr_t GetEngine() const { return engineBase; }
    uintptr_t GetServer() const { return serverBase; }
    uintptr_t GetInputSystem() const { return inputBase; }
    DWORD GetPID() const { return pid; }
    DWORD GetTargetProcessId();
    bool IsLoaded() const { return loaded; }
    HANDLE GetDeviceHandle() const { return hDriver; }

private:
    bool OpenDevice();
    void CleanupAll();
};
