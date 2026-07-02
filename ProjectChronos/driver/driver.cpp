/* chronos_drv.sys - Custom kernel-mode memory reader (read-only) */
/* Single IOCTL: MmCopyVirtualMemory via METHOD_BUFFERED           */

#include <ntifs.h>
#include <ntstatus.h>
#include <ntstrsafe.h>
#include "driver.h"

extern "C" {
    NTSTATUS MmCopyVirtualMemory(
        PEPROCESS SourceProcess,  PVOID  SourceAddress,
        PEPROCESS TargetProcess,  PVOID  TargetAddress,
        SIZE_T   BufferSize,      KPROCESSOR_MODE PreviousMode,
        PSIZE_T  ReturnSize
    );
}

typedef struct _LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinksList;
    LIST_ENTRY InMemoryOrderLinksList;
    LIST_ENTRY InInitializationOrderLinksList;
    PVOID      DllBase;
    PVOID      EntryPoint;
    ULONG      SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

static PDEVICE_OBJECT g_DeviceObject = NULL;
static UNICODE_STRING g_DeviceName;
static UNICODE_STRING g_DosDeviceName;
static KTIMER g_HideTimer;
static KDPC g_HideDpc;

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD      DriverUnload;
DRIVER_DISPATCH    ChronosCreateClose;
DRIVER_DISPATCH    ChronosDeviceControl;

static VOID HideDpcRoutine(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
    UNREFERENCED_PARAMETER(Dpc); UNREFERENCED_PARAMETER(SystemArgument1); UNREFERENCED_PARAMETER(SystemArgument2);
    PDRIVER_OBJECT DriverObject = (PDRIVER_OBJECT)DeferredContext;
    PLDR_DATA_TABLE_ENTRY entry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;
    if (!entry) return;

    KIRQL irql = KeRaiseIrqlToDpcLevel();

    if (entry->InLoadOrderLinksList.Flink) {
        RemoveEntryList(&entry->InLoadOrderLinksList);
        entry->InLoadOrderLinksList.Flink = NULL;
        entry->InLoadOrderLinksList.Blink = NULL;
    }
    if (entry->InMemoryOrderLinksList.Flink) {
        RemoveEntryList(&entry->InMemoryOrderLinksList);
        entry->InMemoryOrderLinksList.Flink = NULL;
        entry->InMemoryOrderLinksList.Blink = NULL;
    }
    if (entry->InInitializationOrderLinksList.Flink) {
        RemoveEntryList(&entry->InInitializationOrderLinksList);
        entry->InInitializationOrderLinksList.Flink = NULL;
        entry->InInitializationOrderLinksList.Blink = NULL;
    }

    KeLowerIrql(irql);
}

NTSTATUS ChronosCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS ChronosDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    ULONG_PTR info = 0;
    PVOID buf = Irp->AssociatedIrp.SystemBuffer;
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inSize = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outSize = stack->Parameters.DeviceIoControl.OutputBufferLength;

    if (code == CHRONOS_IOCTL_READ && inSize >= sizeof(CHRONOS_READ_REQUEST) && buf) {
        PCHRONOS_READ_REQUEST req = (PCHRONOS_READ_REQUEST)buf;

        /* Output: response header + data fits in output buffer */
        SIZE_T maxData = outSize - sizeof(CHRONOS_READ_RESPONSE);
        SIZE_T toRead = req->size;
        if (toRead > maxData) toRead = maxData;

        if (toRead > 0) {
            PEPROCESS targetProcess = NULL;
            status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req->pid, &targetProcess);
            if (NT_SUCCESS(status) && targetProcess) {
                PCHRONOS_READ_RESPONSE resp = (PCHRONOS_READ_RESPONSE)buf;
                PVOID targetBuf = ((PUCHAR)buf) + sizeof(CHRONOS_READ_RESPONSE);
                SIZE_T bytesCopied = 0;

                __try {
                    status = MmCopyVirtualMemory(
                        targetProcess,
                        (PVOID)(ULONG_PTR)req->address,
                        PsGetCurrentProcess(),
                        targetBuf,
                        toRead,
                        KernelMode,
                        &bytesCopied
                    );
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    status = GetExceptionCode();
                }

                ObDereferenceObject(targetProcess);

                if (NT_SUCCESS(status)) {
                    resp->bytesRead = bytesCopied;
                    info = sizeof(CHRONOS_READ_RESPONSE) + bytesCopied;
                }
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
        } else {
            status = STATUS_SUCCESS;
            info = sizeof(CHRONOS_READ_RESPONSE);
        }
    } else {
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    if (g_DosDeviceName.Buffer)
        IoDeleteSymbolicLink(&g_DosDeviceName);
    if (g_DeviceObject)
        IoDeleteDevice(g_DeviceObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);

    ULONG randSuffix = (ULONG)(__rdtsc() & 0xFFFFF);

    WCHAR devName[64], dosName[64];
    RtlStringCbPrintfW(devName, sizeof(devName), L"\\Device\\Chronos%04X", randSuffix);
    RtlStringCbPrintfW(dosName, sizeof(dosName), L"\\DosDevices\\Chronos%04X", randSuffix);

    RtlInitUnicodeString(&g_DeviceName, devName);
    RtlInitUnicodeString(&g_DosDeviceName, dosName);

    NTSTATUS status = IoCreateDevice(DriverObject, 0, &g_DeviceName,
                                      FILE_DEVICE_UNKNOWN, 0, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;

    status = IoCreateSymbolicLink(&g_DosDeviceName, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = ChronosCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = ChronosCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ChronosDeviceControl;

    // HideDriver disabled — causes BSoD on Windows 11 even via timer DPC.
    // TODO: use alternative hiding (DKOM via worker thread, or pool tag manipulation)
    // KeInitializeTimer(&g_HideTimer);
    // KeInitializeDpc(&g_HideDpc, HideDpcRoutine, DriverObject);
    // LARGE_INTEGER dueTime;
    // dueTime.QuadPart = -10000000;
    // KeSetTimer(&g_HideTimer, dueTime, &g_HideDpc);

    return STATUS_SUCCESS;
}
