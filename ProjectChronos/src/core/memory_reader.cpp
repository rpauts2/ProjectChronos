#include "memory_reader.h"
#include "safety/obfuscation.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <random>
#include <algorithm>
#include <thread>

#pragma comment(lib, "advapi32.lib")

MemoryReader::MemoryReader() {}

MemoryReader::~MemoryReader() {
    Unload();
}

bool MemoryReader::ReadRPM(uintptr_t addr, void* buffer, size_t size) {
    if (!hProcess) return false;
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(hProcess, (LPCVOID)addr, buffer, size, &bytesRead) != 0;
}

bool MemoryReader::Initialize() {
    pid = GetTargetProcessId();
    if (!pid) return false;

    // Try driver first
    if (Load()) {
        useRPM = false;
    } else {
        // Fall back to ReadProcessMemory
        hProcess = OpenProcess(PROCESS_VM_READ, FALSE, pid);
        if (!hProcess) return false;
        useRPM = true;
        loaded = true;
    }

    clientBase = GetModuleBase("client.dll");
    engineBase = GetModuleBase("engine2.dll");
    serverBase = GetModuleBase("server.dll");
    inputBase = GetModuleBase("inputsystem.dll");

    loaded = (clientBase > 0);
    if (!loaded) Unload();
    return loaded;
}

bool MemoryReader::Load() {
    // Seed random once
    static bool seeded = false;
    if (!seeded) { srand((unsigned int)time(nullptr) ^ (unsigned int)GetCurrentProcessId()); seeded = true; }

    // Get exe directory for driver lookup
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t exeDir[MAX_PATH];
    wcsncpy_s(exeDir, exePath, MAX_PATH);
    wchar_t* sep = wcsrchr(exeDir, L'\\');
    if (sep) *sep = 0;

    char drvName[16];
    NameGen::RandomString(drvName, 12);

    wchar_t wname[16];
    for (int i = 0; i < 12; i++) wname[i] = (unsigned char)drvName[i];
    wname[12] = 0;

    // Use custom path instead of temp (avoid stale lock from unseeded rand)
    wchar_t drvDir[] = L"C:\\chronos_drv\\";
    CreateDirectoryW(drvDir, nullptr);
    wchar_t sysPath[MAX_PATH];
    swprintf_s(sysPath, L"%s%s.sys", drvDir, wname);
    DeleteFileW(sysPath);

    // Try loading from disk — manual file copy bypassing CopyFileW
    wchar_t srcPath[MAX_PATH];
    bool extracted = false;

    // Locations to try (in order)
    const wchar_t* locations[] = {
        L"chronos_drv.sys",
        L"..\\build\\chronos_drv.sys",
    };
    wchar_t exeLoc[MAX_PATH];
    swprintf_s(exeLoc, L"%s\\chronos_drv.sys", exeDir);
    const wchar_t* locations2[] = { exeLoc };

    // Try manual file copy (ReadFile + WriteFile) instead of CopyFileW
    for (auto* loc : locations) {
        HANDLE hSrc = CreateFileW(loc, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hSrc == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
            if (f) { fprintf(f, "CreateFile READ failed for %S err=%lu\n", loc, err); fclose(f); }
            continue;
        }

        HANDLE hDst = CreateFileW(sysPath, GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDst == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
            if (f) { fprintf(f, "CreateFile WRITE failed err=%lu\n", err); fclose(f); }
            CloseHandle(hSrc); continue;
        }

        BYTE buf[8192];
        DWORD read = 0, written = 0;
        while (ReadFile(hSrc, buf, sizeof(buf), &read, nullptr) && read > 0) {
            WriteFile(hDst, buf, read, &written, nullptr);
        }
        CloseHandle(hDst);
        CloseHandle(hSrc);
        extracted = true;
        break;
    }

    if (!extracted) {
        for (auto* loc : locations2) {
            HANDLE hSrc = CreateFileW(loc, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hSrc == INVALID_HANDLE_VALUE) {
                DWORD err = GetLastError();
                FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
                if (f) { fprintf(f, "CreateFile READ2 failed for %S err=%lu\n", loc, err); fclose(f); }
                continue;
            }

            HANDLE hDst = CreateFileW(sysPath, GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hDst == INVALID_HANDLE_VALUE) { CloseHandle(hSrc); continue; }

            BYTE buf[8192];
            DWORD read = 0, written = 0;
            while (ReadFile(hSrc, buf, sizeof(buf), &read, nullptr) && read > 0) {
                WriteFile(hDst, buf, read, &written, nullptr);
            }
            CloseHandle(hDst);
            CloseHandle(hSrc);
            extracted = true;
            break;
        }
    }

    if (!extracted) {
        FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
        if (f) { fprintf(f, "Disk copy failed, trying embedded resource\n"); fclose(f); }

        // Fallback: extract embedded chronos_drv.sys
        HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(100), L"SYS");
        if (hRes) {
            HGLOBAL hMem = LoadResource(nullptr, hRes);
            if (hMem) {
                BYTE* data = (BYTE*)LockResource(hMem);
                DWORD size = SizeofResource(nullptr, hRes);
                if (data && size > 0) {
                    HANDLE hFile = CreateFileW(sysPath, GENERIC_WRITE, 0, nullptr,
                                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD written = 0;
                        WriteFile(hFile, data, size, &written, nullptr);
                        CloseHandle(hFile);
                        extracted = true;
                    }
                }
                FreeResource(hMem);
            }
        }
    }

    if (!extracted) {
        FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
        if (f) { fprintf(f, "Failed to extract driver\n"); fclose(f); }
        return false;
    }

    // Randomize timestamps
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ft.dwLowDateTime -= (DWORD)(rand() * 1000);
    HANDLE hF = CreateFileW(sysPath, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    if (hF != INVALID_HANDLE_VALUE) {
        SetFileTime(hF, &ft, &ft, &ft);
        CloseHandle(hF);
    }

    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        DWORD scmErr = GetLastError();
        if (scmErr == ERROR_ACCESS_DENIED) {
            hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!hSCM) {
                FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
                if (f) { fprintf(f, "OpenSCManager CONNECT failed: %lu\n", GetLastError()); fclose(f); }
                DeleteFileW(sysPath); return false;
            }
        } else {
            FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
            if (f) { fprintf(f, "OpenSCManager failed: %lu\n", scmErr); fclose(f); }
            DeleteFileW(sysPath); return false;
        }
    }

    std::wstring svcNameW(wname);
    SC_HANDLE hService = CreateServiceW(hSCM, svcNameW.c_str(), svcNameW.c_str(),
        SERVICE_START | SERVICE_STOP | DELETE,
        SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
        SERVICE_ERROR_IGNORE, sysPath,
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!hService) {
        DWORD err = GetLastError();
        hService = OpenServiceW(hSCM, svcNameW.c_str(), SERVICE_QUERY_STATUS | SERVICE_START);
        if (!hService) {
            hService = OpenServiceW(hSCM, svcNameW.c_str(), SERVICE_QUERY_STATUS);
        }
        if (!hService) {
            DWORD err2 = GetLastError();
            FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
            if (f) { fprintf(f, "OpenServiceW failed: %lu, CreateServiceW gave %lu\n", err2, err); fclose(f); }
            CloseServiceHandle(hSCM); DeleteFileW(sysPath); return false;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5 + rand() % 8));

    bool started = StartService(hService, 0, nullptr);
    if (!started) {
        DWORD ssErr = GetLastError();
        if (ssErr != ERROR_SERVICE_ALREADY_RUNNING) {
            // Service might not be startable (non-admin), check status
            SERVICE_STATUS svcStatus = {};
            if (!QueryServiceStatus(hService, &svcStatus) || svcStatus.dwCurrentState != SERVICE_RUNNING) {
                DWORD qsErr = GetLastError();
                FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
                if (f) { fprintf(f, "StartService failed: %lu, QueryServiceStatus: %lu/%lu\n", ssErr, qsErr, svcStatus.dwCurrentState); fclose(f); }
                DeleteService(hService); CloseServiceHandle(hService);
                CloseServiceHandle(hSCM); DeleteFileW(sysPath);
                return false;
            }
        }
    }

    // Self-delete .sys — driver stays in kernel
    HANDLE hDel = CreateFileW(sysPath, DELETE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hDel != INVALID_HANDLE_VALUE) CloseHandle(hDel);

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);

    // Open device via Win32 namespace \\.\ChronosXXXX
    for (int suffix = 0; suffix < 0x100000; suffix++) {
        wchar_t path[64];
        swprintf_s(path, L"\\\\.\\Chronos%04X", suffix);
        hDriver = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDriver != INVALID_HANDLE_VALUE)
            return true;
    }

    FILE* f = nullptr; _wfopen_s(&f, L"chronos_dbg.txt", L"a");
    if (f) { fprintf(f, "Device not found after scanning 0x100000 suffixes\n"); fclose(f); }
    return false;
}

void MemoryReader::Unload() {
    if (hDriver != INVALID_HANDLE_VALUE) {
        CloseHandle(hDriver);
        hDriver = INVALID_HANDLE_VALUE;
    }
    if (hProcess) {
        CloseHandle(hProcess);
        hProcess = NULL;
    }
    loaded = false;
}

bool MemoryReader::OpenDevice() {
    return Load(); // Re-entrant: Load always re-scans
}

void MemoryReader::CleanupAll() {
    Unload();
    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return;

    DWORD bytesNeeded = 0, servicesReturned = 0;
    EnumServicesStatusW(hSCM, SERVICE_DRIVER, SERVICE_STATE_ALL,
                        nullptr, 0, &bytesNeeded, &servicesReturned, nullptr);
    if (bytesNeeded > 0) {
        std::vector<BYTE> buf(bytesNeeded);
        if (EnumServicesStatusW(hSCM, SERVICE_DRIVER, SERVICE_STATE_ALL,
                                (LPENUM_SERVICE_STATUSW)buf.data(), bytesNeeded,
                                &bytesNeeded, &servicesReturned, nullptr)) {
            LPENUM_SERVICE_STATUSW services = (LPENUM_SERVICE_STATUSW)buf.data();
            for (DWORD i = 0; i < servicesReturned; i++) {
                std::wstring name(services[i].lpServiceName);
                if (name.length() == 12) {
                    SC_HANDLE hSvc = OpenServiceW(hSCM, name.c_str(), SERVICE_STOP | DELETE);
                    if (hSvc) {
                        ControlService(hSvc, SERVICE_CONTROL_STOP, nullptr);
                        DeleteService(hSvc);
                        CloseServiceHandle(hSvc);
                    }
                }
            }
        }
    }
    CloseServiceHandle(hSCM);
}

bool MemoryReader::WriteBuffer(uintptr_t addr, void* buffer, size_t size) {
    if (!loaded || !buffer || !size) return false;

    // Try driver IOCTL first
    CHRONOS_WRITE_REQUEST req = { (uint32_t)pid, (uint64_t)addr, size };
    BYTE* respBuf = (BYTE*)malloc(sizeof(CHRONOS_READ_RESPONSE));
    DWORD returned = 0;
    bool ok = DeviceIoControl(hDriver, CHRONOS_IOCTL_WRITE,
        &req, sizeof(req), respBuf, (DWORD)sizeof(CHRONOS_READ_RESPONSE),
        &returned, nullptr) != 0;
    free(respBuf);
    if (ok) return true;

    // Fallback to WriteProcessMemory
    HANDLE hProcess = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!hProcess) return false;

    SIZE_T bytesWritten = 0;
    ok = WriteProcessMemory(hProcess, (LPVOID)addr, buffer, size, &bytesWritten) != 0;
    CloseHandle(hProcess);
    return ok && bytesWritten == size;
}

bool MemoryReader::ReadBuffer(uintptr_t addr, void* buffer, size_t size) {
    if (!loaded || !buffer || !size) return false;

    if (useRPM) return ReadRPM(addr, buffer, size);

    CHRONOS_READ_REQUEST req = { (uint32_t)pid, (uint64_t)addr, size };
    BYTE* respBuf = (BYTE*)malloc(sizeof(CHRONOS_READ_RESPONSE) + size);
    if (!respBuf) return false;

    DWORD returned = 0;
    bool ok = DeviceIoControl(hDriver, CHRONOS_IOCTL_READ,
        &req, sizeof(req), respBuf, (DWORD)(sizeof(CHRONOS_READ_RESPONSE) + size),
        &returned, nullptr) != 0;

    if (ok && returned >= sizeof(CHRONOS_READ_RESPONSE)) {
        CHRONOS_READ_RESPONSE* resp = (CHRONOS_READ_RESPONSE*)respBuf;
        size_t toCopy = (std::min)(resp->bytesRead, size);
        memcpy(buffer, respBuf + sizeof(CHRONOS_READ_RESPONSE), toCopy);
    }

    free(respBuf);
    return ok;
}

DWORD MemoryReader::GetTargetProcessId() {
    DWORD pid = 0;
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
    return pid;
}

uintptr_t MemoryReader::GetModuleBase(const std::string& moduleName) {
    if (moduleName.empty()) return 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid ? pid : GetTargetProcessId());
    if (snap == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W me = { sizeof(MODULEENTRY32W) };
    uintptr_t base = 0;
    std::wstring wname(moduleName.begin(), moduleName.end());

    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, wname.c_str()) == 0) {
                base = (uintptr_t)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return base;
}
