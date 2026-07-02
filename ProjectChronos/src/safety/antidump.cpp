#include "antidump.h"
#include "obfuscation.h"
#include <psapi.h>
#include <algorithm>

static void LogDbg(const char* msg) {
    FILE* f = nullptr; _wfopen_s(&f, L"C:\\chronos_antidump.txt", L"a");
    if (f) { fprintf(f, "%s\n", msg); fflush(f); fclose(f); }
}

AntiDump::~AntiDump() {
    Unprotect();
}

void AntiDump::Protect() {
    if (active) return;
    LogDbg("Protect: entering");
    // RandomizeImportOrder disabled — crashes on modern Windows (CFG/read-only .rdata)
    // LogDbg("Protect: calling RandomizeImportOrder");
    // RandomizeImportOrder();
    // LogDbg("Protect: RandomizeImportOrder OK");
    LogDbg("Protect: calling ErasePEHeaders");
    ErasePEHeaders();
    LogDbg("Protect: ErasePEHeaders OK");
    active = true;
    LogDbg("Protect: done");
}

void AntiDump::Unprotect() {
    active = false;
}

void AntiDump::ErasePEHeaders() {
    // DISABLED — breaks CRT on modern Windows
}

void AntiDump::SelfDelete() {
    // Self-delete the .exe on close
    // Uses FILE_FLAG_DELETE_ON_CLOSE — file is deleted when process exits
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HANDLE hFile = CreateFileW(exePath, DELETE, 0, nullptr,
                                OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
}

void AntiDump::NullOutSection(const char* sectionName) {
    // DISABLED
}

void AntiDump::RandomizeImportOrder() {
    // DISABLED
}
