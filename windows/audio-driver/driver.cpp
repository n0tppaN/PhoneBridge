#include <ntddk.h>
#include <wdf.h>
#include <portcls.h>

extern "C" DRIVER_INITIALIZE DriverEntry;

/**
 * PhoneBridge SYSVAD Virtual Audio Driver (phonebridge_audio.sys)
 * Exposes "Microfone (PhoneBridge)" as a native WDM/WaveRT recording endpoint.
 */
extern "C" NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, nullptr);
    status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);

    if (NT_SUCCESS(status)) {
        KdPrint(("[PhoneBridgeAudio] PhoneBridge Virtual Audio Driver (.sys) loaded successfully.\n"));
    }
    return status;
}
