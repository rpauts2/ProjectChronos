#pragma once

/* Shared IOCTL protocol between user-mode chronos.exe and kernel-mode chronos_drv.sys */
/* NOTE: In kernel mode (WDK), ULONG/SIZE_T/PVOID come from ntddk.h                     */
/*       In user mode (Win32), they come from windows.h                                   */

#define CHRONOS_IOCTL_READ 0x80000000

typedef struct _CHRONOS_READ_REQUEST {
    ULONG  pid;
    ULONG64 address;
    SIZE_T size;
} CHRONOS_READ_REQUEST, *PCHRONOS_READ_REQUEST;

typedef struct _CHRONOS_READ_RESPONSE {
    SIZE_T bytesRead;
} CHRONOS_READ_RESPONSE, *PCHRONOS_READ_RESPONSE;
