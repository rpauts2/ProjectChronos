#pragma once
#include "core/types.h"
#include <string>

class HWIDSpoofer {
    bool spoofed = false;
public:
    HWIDSpoofer() {}
    ~HWIDSpoofer();

    void SpoofAll();
    void RestoreAll();

    static void SpoofRegistryVolumeGUID();
    static void SpoofRegistryMachineGUID();
    static void SpoofMACAddress();
    static void SpoofComputerName();
    static void SpoofWinProductID();

    static void SaveRegistryKey(const std::string& path, const std::string& value, const std::string& backupSuffix);
    static void RestoreRegistryKey(const std::string& path, const std::string& backupSuffix);
};
