/*
 * PROJECT:         LakoOS SpacemiT K1 Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Driver entry and PortCls adapter registration
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag)
{
    PVOID Allocation = ExAllocatePoolWithTag(PoolType, Size, Tag);
    if (Allocation)
        RtlZeroMemory(Allocation, Size);
    return Allocation;
}

void
__cdecl
operator delete(PVOID Allocation)
{
    if (Allocation)
        ExFreePool(Allocation);
}

void
__cdecl
operator delete(PVOID Allocation, UINT_PTR)
{
    if (Allocation)
        ExFreePool(Allocation);
}

static NTSTATUS
InstallSubdevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PCWSTR Name,
    REFGUID PortClassId,
    PUNKNOWN Miniport,
    CK1xAudioAdapter *Adapter,
    PRESOURCELIST ResourceList,
    PUNKNOWN *PortUnknown)
{
    PPORT Port;
    NTSTATUS Status;

    Status = PcNewPort(&Port, PortClassId);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Port->Init(DeviceObject, Irp, Miniport, static_cast<PUNKNOWN>(Adapter), ResourceList);
    if (NT_SUCCESS(Status))
        Status = PcRegisterSubdevice(DeviceObject, const_cast<PWSTR>(Name), Port);
    if (NT_SUCCESS(Status) && PortUnknown)
        Status = Port->QueryInterface(IID_IUnknown, reinterpret_cast<PVOID *>(PortUnknown));

    Port->Release();
    return Status;
}

static NTSTATUS
NTAPI
K1xAudioStartDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp, PRESOURCELIST ResourceList)
{
    PK1XAUDIO_DEVICE_EXTENSION DeviceExtension;
    CK1xAudioAdapter *Adapter;
    PUNKNOWN TopologyMiniport = NULL;
    PUNKNOWN WaveMiniport = NULL;
    PUNKNOWN TopologyPort = NULL;
    PUNKNOWN WavePort = NULL;
    NTSTATUS Status;

    DeviceExtension = static_cast<PK1XAUDIO_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
    Adapter = new (NonPagedPool, TAG_K1XAUDIO) CK1xAudioAdapter();
    if (!Adapter)
        return STATUS_INSUFFICIENT_RESOURCES;
    Adapter->AddRef();

    Status = Adapter->Initialize(DeviceObject, ResourceList);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = K1xAudioCreateTopology(&TopologyMiniport, Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = InstallSubdevice(
        DeviceObject,
        Irp,
        L"Topology",
        CLSID_PortTopology,
        TopologyMiniport,
        Adapter,
        ResourceList,
        &TopologyPort);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = K1xAudioCreateWave(&WaveMiniport, Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = InstallSubdevice(
        DeviceObject,
        Irp,
        L"Wave",
        CLSID_PortWaveRT,
        WaveMiniport,
        Adapter,
        ResourceList,
        &WavePort);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = PcRegisterPhysicalConnection(DeviceObject, WavePort, 1, TopologyPort, 0);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = PcRegisterPhysicalConnection(DeviceObject, TopologyPort, 3, WavePort, 3);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    DeviceExtension->Adapter = Adapter;

Cleanup:
    if (WavePort)
        WavePort->Release();
    if (TopologyPort)
        TopologyPort->Release();
    if (WaveMiniport)
        WaveMiniport->Release();
    if (TopologyMiniport)
        TopologyMiniport->Release();
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("K1XAUDIO: start failed 0x%08lx\n", Status);
        Adapter->Shutdown();
        Adapter->Release();
    }
    return Status;
}

static NTSTATUS
NTAPI
K1xAudioAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
    if (!DriverObject || !PhysicalDeviceObject)
        return STATUS_INVALID_PARAMETER;

    return PcAddAdapterDevice(
        DriverObject,
        PhysicalDeviceObject,
        K1xAudioStartDevice,
        2,
        sizeof(K1XAUDIO_DEVICE_EXTENSION));
}

static NTSTATUS
NTAPI
K1xAudioPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

    if (Stack->MinorFunction == IRP_MN_STOP_DEVICE ||
        Stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL ||
        Stack->MinorFunction == IRP_MN_REMOVE_DEVICE)
    {
        PK1XAUDIO_DEVICE_EXTENSION DeviceExtension =
            static_cast<PK1XAUDIO_DEVICE_EXTENSION>(DeviceObject->DeviceExtension);
        CK1xAudioAdapter *Adapter = DeviceExtension->Adapter;
        if (Adapter)
        {
            DeviceExtension->Adapter = NULL;
            Adapter->Shutdown();
            Adapter->Release();
        }
    }

    return PcDispatchIrp(DeviceObject, Irp);
}

static VOID
NTAPI
K1xAudioUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

extern "C" DRIVER_INITIALIZE DriverEntry;

extern "C"
NTSTATUS
NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status = PcInitializeAdapterDriver(DriverObject, RegistryPath, K1xAudioAddDevice);
    if (NT_SUCCESS(Status))
    {
        DriverObject->DriverUnload = K1xAudioUnload;
        DriverObject->MajorFunction[IRP_MJ_PNP] = K1xAudioPnp;
    }
    return Status;
}
