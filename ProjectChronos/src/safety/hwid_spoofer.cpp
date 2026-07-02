#include "hwid_spoofer.h"
#include "obfuscation.h"
#include <random>

HWIDSpoofer::~HWIDSpoofer() {
    RestoreAll();
}

void HWIDSpoofer::SpoofAll() {
    SpoofRegistryVolumeGUID();
    SpoofRegistryMachineGUID();
    SpoofMACAddress();
    SpoofComputerName();
    SpoofWinProductID();
    spoofed = true;
}

void HWIDSpoofer::RestoreAll() {
    if (!spoofed) return;
    // Restore by deleting backup keys — actual restore depends on what was backed up
    spoofed = false;
}

static std::string RandomHexString(int len) {
    static const char hex[] = "0123456789abcdef";
    std::string s;
    s.reserve(len);
    for (int i = 0; i < len; i++)
        s += hex[rand() % 16];
    return s;
}

void HWIDSpoofer::SpoofRegistryVolumeGUID() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\MountedDevices", 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return;

    DWORD idx = 0;
    char valueName[256];
    DWORD valueNameSize = sizeof(valueName);
    BYTE data[512];
    DWORD dataSize = sizeof(data);
    DWORD type;

    while (RegEnumValueA(hKey, idx, valueName, &valueNameSize, nullptr,
                          &type, data, &dataSize) == ERROR_SUCCESS) {
        std::string vn(valueName);
        if (vn.find("Volume") != std::string::npos && vn.find("{") != std::string::npos) {
            // Found a volume GUID — replace it
            std::string newGuid = OBFUSCATE("{")
                + RandomHexString(8) + OBFUSCATE("-")
                + RandomHexString(4) + OBFUSCATE("-")
                + RandomHexString(4) + OBFUSCATE("-")
                + RandomHexString(4) + OBFUSCATE("-")
                + RandomHexString(12) + OBFUSCATE("}");

            // Replace GUID in the value name
            size_t guidStart = vn.find(OBFUSCATE("{"));
            if (guidStart != std::string::npos) {
                std::string newName = vn.substr(0, guidStart) + newGuid;
                RegDeleteValueA(hKey, valueName);
                RegSetValueExA(hKey, newName.c_str(), 0, type, data, dataSize);
            }
        }
        valueNameSize = sizeof(valueName);
        dataSize = sizeof(data);
        idx++;
    }
    RegCloseKey(hKey);
}

void HWIDSpoofer::SpoofRegistryMachineGUID() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return;

    char oldGuid[64] = {};
    DWORD size = sizeof(oldGuid);
    RegQueryValueExA(hKey, "MachineGuid", nullptr, nullptr, (BYTE*)oldGuid, &size);

    if (oldGuid[0]) {
        // Save backup
        RegSetValueExA(hKey, "MachineGuid_Backup", 0, REG_SZ, (BYTE*)oldGuid, (DWORD)strlen(oldGuid) + 1);

        // Generate new random GUID
        std::string newGuid = RandomHexString(8) + "-" + RandomHexString(4) + "-"
            + RandomHexString(4) + "-" + RandomHexString(4) + "-" + RandomHexString(12);

        RegSetValueExA(hKey, "MachineGuid", 0, REG_SZ, (BYTE*)newGuid.c_str(), (DWORD)newGuid.length() + 1);
    }
    RegCloseKey(hKey);
}

void HWIDSpoofer::SpoofMACAddress() {
    // User-mode MAC spoofing via registry
    // Full MAC spoof requires kernel (NDIS) or ADMIN + registry
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}",
        0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return;

    DWORD idx = 0;
    char subKey[256];
    DWORD subKeySize = sizeof(subKey);

    while (RegEnumKeyExA(hKey, idx, subKey, &subKeySize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY hAdapter;
        if (RegOpenKeyExA(hKey, subKey, 0, KEY_READ | KEY_WRITE, &hAdapter) == ERROR_SUCCESS) {
            char origMac[32] = {};
            DWORD macSize = sizeof(origMac);
            if (RegQueryValueExA(hAdapter, "NetworkAddress", nullptr, nullptr, (BYTE*)origMac, &macSize) == ERROR_SUCCESS) {
                // Generate random MAC
                char newMac[18];
                sprintf_s(newMac, "%02X%02X%02X%02X%02X%02X",
                    rand() % 256, rand() % 256, rand() % 256,
                    rand() % 256, rand() % 256, rand() % 256);
                RegSetValueExA(hAdapter, "NetworkAddress_Backup", 0, REG_SZ, (BYTE*)origMac, macSize);
                RegSetValueExA(hAdapter, "NetworkAddress", 0, REG_SZ, (BYTE*)newMac, (DWORD)strlen(newMac) + 1);
            }
            RegCloseKey(hAdapter);
        }
        subKeySize = sizeof(subKey);
        idx++;
    }
    RegCloseKey(hKey);
}

void HWIDSpoofer::SpoofComputerName() {
    char oldName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(oldName);
    if (!GetComputerNameA(oldName, &size)) return;

    // Generate random computer name
    char newName[16];
    NameGen::RandomString(newName, 12);

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
        0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "ComputerName_Backup", 0, REG_SZ, (BYTE*)oldName, (DWORD)strlen(oldName) + 1);
        RegSetValueExA(hKey, "ComputerName", 0, REG_SZ, (BYTE*)newName, (DWORD)strlen(newName) + 1);
        RegCloseKey(hKey);
    }

    // Also update ActiveComputerName
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName",
        0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "ComputerName_Backup", 0, REG_SZ, (BYTE*)oldName, (DWORD)strlen(oldName) + 1);
        RegSetValueExA(hKey, "ComputerName", 0, REG_SZ, (BYTE*)newName, (DWORD)strlen(newName) + 1);
        RegCloseKey(hKey);
    }
}

void HWIDSpoofer::SpoofWinProductID() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
        0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return;

    char oldPID[64] = {};
    DWORD size = sizeof(oldPID);
    if (RegQueryValueExA(hKey, "ProductId", nullptr, nullptr, (BYTE*)oldPID, &size) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "ProductId_Backup", 0, REG_SZ, (BYTE*)oldPID, size);

        std::string newPID = RandomHexString(5) + "-" + RandomHexString(5) + "-"
            + RandomHexString(5) + "-" + RandomHexString(5);
        RegSetValueExA(hKey, "ProductId", 0, REG_SZ, (BYTE*)newPID.c_str(), (DWORD)newPID.length() + 1);
    }
    RegCloseKey(hKey);
}

void HWIDSpoofer::SaveRegistryKey(const std::string& path, const std::string& value, const std::string& backupSuffix) {
    // Generic save — placeholder
    UNREFERENCED_PARAMETER(path); UNREFERENCED_PARAMETER(value); UNREFERENCED_PARAMETER(backupSuffix);
}

void HWIDSpoofer::RestoreRegistryKey(const std::string& path, const std::string& backupSuffix) {
    // Generic restore — placeholder
    UNREFERENCED_PARAMETER(path); UNREFERENCED_PARAMETER(backupSuffix);
}
