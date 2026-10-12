/*
 * PROJECT:     LakoOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB function controller KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "classlibrary.h"
#include <usbspec.h>
#include <usbfnbase.h>
#include <usbfnattach.h>
#include <usbfnioctl.h>
#include <ufxproprietarycharger.h>
#include <ufxclient.h>
#include <aux_klib.h>

#define UFX_TAG 'xfFU'
#define UFX_MAX_INTERFACES 8
#define UFX_MAX_ENDPOINTS 30
#define UFX_MAX_EVENTS 64
#define UFX_MAX_NOTIFICATIONS 32
#define UFX_MAX_PENDING_IRPS 4
#define UFX_DEFAULT_PACKET 64
#define UFX_CONFIG_ATTRIBUTES 0xE0
#define UFX_CONFIG_POWER 0x32
#define UFX_LANGUAGE 0x0409
#define UFX_STRING_MANUFACTURER 1
#define UFX_STRING_PRODUCT 2
#define UFX_STRING_SERIAL 3
#define UFX_PIPE_IN 0x10

typedef enum _UFX_EVENT_TYPE
{
    UfxEventHardwareReady,
    UfxEventAttach,
    UfxEventDetach,
    UfxEventSuspend,
    UfxEventResume,
    UfxEventReset,
    UfxEventSetup,
    UfxEventActivate,
    UfxEventDeactivate,
    UfxEventFailure,
    UfxEventRemoteWakeup
} UFX_EVENT_TYPE;

typedef struct _UFX_EVENT
{
    UFX_EVENT_TYPE Type;
    ULONG Value;
    USB_DEFAULT_PIPE_SETUP_PACKET Setup;
    WDFREQUEST Request;
} UFX_EVENT, *PUFX_EVENT;

#define UFX_MAX_FAILURE_CONTEXT 256

typedef struct _UFX_INTERFACE
{
    WCHAR Name[MAX_INTERFACE_NAME_LENGTH];
    WCHAR Guid[MAX_INTERFACE_GUID_LENGTH];
    BOOLEAN HasGuid;
    UCHAR Number;
    ULONG DescriptorLength;
    UCHAR Descriptor[256];
    WDFDEVICE Pdo;
    USBFN_NOTIFICATION Notifications[UFX_MAX_NOTIFICATIONS];
    ULONG NotificationHead;
    ULONG NotificationCount;
} UFX_INTERFACE, *PUFX_INTERFACE;

typedef struct _UFX_ENDPOINT_ENTRY
{
    UCHAR Interface;
    USB_ENDPOINT_DESCRIPTOR Descriptor;
    UFXENDPOINT Endpoint;
} UFX_ENDPOINT_ENTRY, *PUFX_ENDPOINT_ENTRY;

typedef struct _UFX_PENDING_IRP
{
    PIRP Irp;
    WDFQUEUE Queue;
} UFX_PENDING_IRP, *PUFX_PENDING_IRP;

typedef struct _UFX_DEVICE_CONTEXT
{
    UFXDEVICE Handle;
    WDFDEVICE Device;
    UFX_DEVICE_CALLBACKS Callbacks;
    UFX_DEVICE_CAPABILITIES Capabilities;
    KSPIN_LOCK Lock;
    UFX_EVENT Events[UFX_MAX_EVENTS];
    ULONG EventHead;
    ULONG EventCount;
    KEVENT WorkEvent;
    KEVENT EventDone;
    NTSTATUS EventStatus;
    PKTHREAD Thread;
    BOOLEAN Stopping;
    UFX_PENDING_IRP PendingIrps[UFX_MAX_PENDING_IRPS];
    USBFN_PORT_TYPE PortType;
    USBFN_DEVICE_STATE State;
    BOOLEAN HardwareReady;
    BOOLEAN Attached;
    BOOLEAN PortReported;
    BOOLEAN Active;
    BOOLEAN Connected;
    BOOLEAN ChildrenCreated;
    BOOLEAN RemoteWake;
    UCHAR Address;
    UCHAR Configuration;
    UCHAR AlternateSetting;
    USB_DEVICE_SPEED Speed;
    UFXENDPOINT DefaultEndpoint;
    ULONG EndpointCount;
    UFX_ENDPOINT_ENTRY Endpoints[UFX_MAX_ENDPOINTS];
    ULONG InterfaceCount;
    UFX_INTERFACE Interfaces[UFX_MAX_INTERFACES];
    ULONG VendorId;
    ULONG ProductId;
    ULONG SerialNumber;
    WCHAR Manufacturer[MAX_USB_STRING_LENGTH];
    WCHAR Product[MAX_USB_STRING_LENGTH];
    WCHAR Serial[MAX_USB_STRING_LENGTH];
    UCHAR Buffer[512];
    BOOLEAN FailurePresent;
    BOOLEAN Failed;
    UCHAR Failure[UFX_MAX_FAILURE_CONTEXT];
} UFX_DEVICE_CONTEXT, *PUFX_DEVICE_CONTEXT;

typedef struct _UFX_FDO_CONTEXT
{
    UFXDEVICE UfxDevice;
} UFX_FDO_CONTEXT, *PUFX_FDO_CONTEXT;

typedef struct _UFX_ENDPOINT_CONTEXT
{
    UFXDEVICE UfxDevice;
    USB_ENDPOINT_DESCRIPTOR Descriptor;
    UFX_ENDPOINT_CALLBACKS Callbacks;
    WDFQUEUE TransferQueue;
    WDFQUEUE CommandQueue;
    USBFNPIPEID PipeId;
    BOOLEAN Halted;
} UFX_ENDPOINT_CONTEXT, *PUFX_ENDPOINT_CONTEXT;

typedef struct _UFX_PDO_CONTEXT
{
    UFXDEVICE UfxDevice;
    ULONG Interface;
    WDFQUEUE NotificationQueue;
} UFX_PDO_CONTEXT, *PUFX_PDO_CONTEXT;

typedef struct _UFXENDPOINT_INIT
{
    UFXDEVICE UfxDevice;
    USB_ENDPOINT_DESCRIPTOR Descriptor;
    UFX_ENDPOINT_CALLBACKS Callbacks;
} UFXENDPOINT_INIT_DATA, *PUFXENDPOINT_INIT_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UFX_DEVICE_CONTEXT, UfxGetDeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UFX_FDO_CONTEXT, UfxGetFdoContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UFX_ENDPOINT_CONTEXT, UfxGetEndpointContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(UFX_PDO_CONTEXT, UfxGetPdoContext)

static VOID UfxPost(_Inout_ PUFX_DEVICE_CONTEXT Context, _In_ UFX_EVENT_TYPE Type, _In_ ULONG Value,
                    _In_opt_ PUSB_DEFAULT_PIPE_SETUP_PACKET Setup, _In_opt_ WDFREQUEST Request);

static
NTSTATUS
UfxQueryValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ ULONG Type,
    _Out_writes_bytes_(Size) PVOID Data,
    _In_ ULONG Size,
    _Out_opt_ PULONG Length)
{
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 512];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PVOID)Buffer;
    UNICODE_STRING ValueName;
    ULONG Result;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information, sizeof(Buffer), &Result);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Information->Type != Type || Information->DataLength > Size)
        return STATUS_OBJECT_TYPE_MISMATCH;

    RtlCopyMemory(Data, Information->Data, Information->DataLength);
    if (Length != NULL)
        *Length = Information->DataLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UfxOpenKey(
    _Out_ PHANDLE Key,
    _In_opt_ HANDLE Root,
    _In_ PCWSTR Path)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Root, NULL);
    return ZwOpenKey(Key, KEY_READ, &Attributes);
}

static
VOID
UfxReadString(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _Out_writes_(Count) PWCHAR Text,
    _In_ ULONG Count)
{
    ULONG Length = 0;

    RtlZeroMemory(Text, Count * sizeof(WCHAR));
    if (!NT_SUCCESS(UfxQueryValue(Key, Name, REG_SZ, Text, Count * sizeof(WCHAR), &Length)))
        Text[0] = UNICODE_NULL;
    Text[Count - 1] = UNICODE_NULL;
}

static
VOID
UfxReadConfiguration(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    WCHAR Current[MAX_CONFIGURATION_NAME_LENGTH];
    WCHAR List[UFX_MAX_INTERFACES * MAX_INTERFACE_NAME_LENGTH + 1];
    WCHAR Path[128];
    HANDLE Root, Key;
    ULONG Length, Value;
    PWCHAR Name;

    Context->SerialNumber = 1;
    RtlStringCbCopyW(Path, sizeof(Path), KREGUSBFNENUMPATH);
    Length = (ULONG)wcslen(Path);
    if (Length != 0 && Path[Length - 1] == L'\\')
        Path[Length - 1] = UNICODE_NULL;
    if (!NT_SUCCESS(UfxOpenKey(&Root, NULL, Path)))
        return;

    if (NT_SUCCESS(UfxQueryValue(Root, L"idVendor", REG_DWORD, &Value, sizeof(Value), NULL)))
        Context->VendorId = Value;
    if (NT_SUCCESS(UfxQueryValue(Root, L"idProduct", REG_DWORD, &Value, sizeof(Value), NULL)))
        Context->ProductId = Value;
    if (NT_SUCCESS(UfxQueryValue(Root, L"iSerialNumber", REG_DWORD, &Value, sizeof(Value), NULL)))
        Context->SerialNumber = Value;
    UfxReadString(Root, L"SerialNumberString", Context->Serial, RTL_NUMBER_OF(Context->Serial));
    UfxReadString(Root, L"ManufacturerString", Context->Manufacturer, RTL_NUMBER_OF(Context->Manufacturer));
    UfxReadString(Root, L"ProductString", Context->Product, RTL_NUMBER_OF(Context->Product));
    UfxReadString(Root, L"CurrentConfiguration", Current, RTL_NUMBER_OF(Current));

    RtlStringCbPrintfW(Path, sizeof(Path), L"Configurations\\%ws", Current);
    RtlZeroMemory(List, sizeof(List));
    if (Current[0] != UNICODE_NULL && NT_SUCCESS(UfxOpenKey(&Key, Root, Path)))
    {
        if (!NT_SUCCESS(UfxQueryValue(Key, L"InterfaceList", REG_MULTI_SZ, List, sizeof(List) - 2 * sizeof(WCHAR), &Length)))
            List[0] = UNICODE_NULL;
        ZwClose(Key);
    }

    for (Name = List; *Name != UNICODE_NULL && Context->InterfaceCount < UFX_MAX_INTERFACES; Name += wcslen(Name) + 1)
    {
        PUFX_INTERFACE Interface = &Context->Interfaces[Context->InterfaceCount];

        RtlStringCbPrintfW(Path, sizeof(Path), L"Interfaces\\%ws", Name);
        if (!NT_SUCCESS(UfxOpenKey(&Key, Root, Path)))
            continue;

        if (NT_SUCCESS(UfxQueryValue(Key, L"InterfaceDescriptor", REG_BINARY, Interface->Descriptor,
                                     sizeof(Interface->Descriptor), &Interface->DescriptorLength)) &&
            Interface->DescriptorLength >= sizeof(USB_INTERFACE_DESCRIPTOR))
        {
            RtlStringCbCopyW(Interface->Name, sizeof(Interface->Name), Name);
            UfxReadString(Key, L"InterfaceGUID", Interface->Guid, RTL_NUMBER_OF(Interface->Guid));
            Interface->HasGuid = Interface->Guid[0] != UNICODE_NULL;
            Interface->Number = (UCHAR)Context->InterfaceCount;
            Interface->Descriptor[2] = Interface->Number;
            Interface->Descriptor[8] = 0;
            Context->InterfaceCount++;
        }

        ZwClose(Key);
    }

    ZwClose(Root);
}

static
VOID
UfxReadSmbiosUuid(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    UCHAR Uuid[16];
    PUCHAR Table, Entry, End;
    ULONG Length = 0, Index;
    NTSTATUS Status;

    RtlZeroMemory(Uuid, sizeof(Uuid));
    if (NT_SUCCESS(AuxKlibInitialize()))
    {
        Status = AuxKlibGetSystemFirmwareTable('RSMB', 0, NULL, 0, &Length);
        if ((Status == STATUS_BUFFER_TOO_SMALL || NT_SUCCESS(Status)) && Length > 8)
        {
            Table = ExAllocatePoolZero(PagedPool, Length, UFX_TAG);
            if (Table != NULL)
            {
                if (NT_SUCCESS(AuxKlibGetSystemFirmwareTable('RSMB', 0, Table, Length, &Length)) && Length > 8)
                {
                    Entry = Table + 8;
                    End = Table + min(Length, 8 + *(PULONG)(Table + 4));
                    while (Entry + 4 <= End && Entry[1] >= 4 && Entry[0] != 127)
                    {
                        if (Entry[0] == 1 && Entry[1] >= 0x19 && Entry + 0x18 <= End)
                        {
                            RtlCopyMemory(Uuid, Entry + 8, sizeof(Uuid));
                            break;
                        }

                        Entry += Entry[1];
                        while (Entry + 1 < End && (Entry[0] != 0 || Entry[1] != 0))
                            Entry++;
                        Entry += 2;
                    }
                }

                ExFreePoolWithTag(Table, UFX_TAG);
            }
        }
    }

    for (Index = 0; Index < sizeof(Uuid); Index++)
        RtlStringCbPrintfW(Context->Serial + Index * 2, 3 * sizeof(WCHAR), L"%02X", Uuid[Index]);
}

static
USHORT
UfxPacketSize(
    _In_ const USB_ENDPOINT_DESCRIPTOR *Descriptor,
    _In_ USB_DEVICE_SPEED Speed)
{
    USHORT Size = Descriptor->wMaxPacketSize;

    switch (Descriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK)
    {
        case USB_ENDPOINT_TYPE_BULK:
            return Speed == UsbSuperSpeed ? 1024 : Speed == UsbHighSpeed ? 512 : 64;
        case USB_ENDPOINT_TYPE_CONTROL:
            return Speed == UsbSuperSpeed ? 512 : 64;
        default:
            return Speed == UsbFullSpeed ? min(Size, 64) : Size;
    }
}

static
VOID
UfxCollectEndpoints(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    ULONG Index, Offset;

    Context->EndpointCount = 0;
    for (Index = 0; Index < Context->InterfaceCount; Index++)
    {
        PUFX_INTERFACE Interface = &Context->Interfaces[Index];

        for (Offset = Interface->Descriptor[0]; Offset + 2 <= Interface->DescriptorLength && Interface->Descriptor[Offset] != 0;
             Offset += Interface->Descriptor[Offset])
        {
            if (Interface->Descriptor[Offset + 1] == USB_ENDPOINT_DESCRIPTOR_TYPE &&
                Offset + sizeof(USB_ENDPOINT_DESCRIPTOR) <= Interface->DescriptorLength &&
                Context->EndpointCount < UFX_MAX_ENDPOINTS)
            {
                PUFX_ENDPOINT_ENTRY Entry = &Context->Endpoints[Context->EndpointCount++];

                RtlCopyMemory(&Entry->Descriptor, &Interface->Descriptor[Offset], sizeof(Entry->Descriptor));
                Entry->Interface = (UCHAR)Index;
                Entry->Endpoint = NULL;
            }
        }
    }
}

static
USBFNPIPEID
UfxPipeId(
    _In_ UCHAR Address)
{
    return (USBFNPIPEID)((Address & 0x0F) | ((Address & USB_ENDPOINT_DIRECTION_MASK) ? UFX_PIPE_IN : 0));
}

static
PUFX_ENDPOINT_CONTEXT
UfxFindPipe(
    _In_ PUFX_DEVICE_CONTEXT Context,
    _In_ USBFNPIPEID PipeId)
{
    ULONG Index;

    if (PipeId == 0)
        return Context->DefaultEndpoint != NULL ? UfxGetEndpointContext(Context->DefaultEndpoint) : NULL;

    for (Index = 0; Index < Context->EndpointCount; Index++)
    {
        if (Context->Endpoints[Index].Endpoint != NULL && UfxPipeId(Context->Endpoints[Index].Descriptor.bEndpointAddress) == PipeId)
            return UfxGetEndpointContext(Context->Endpoints[Index].Endpoint);
    }

    return NULL;
}

static
ULONG
UfxBuildConfiguration(
    _In_ PUFX_DEVICE_CONTEXT Context,
    _In_ USB_DEVICE_SPEED Speed,
    _In_ UCHAR Type,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size)
{
    ULONG Length = sizeof(USB_CONFIGURATION_DESCRIPTOR), Index, Offset;
    PUSB_CONFIGURATION_DESCRIPTOR Configuration = (PVOID)Buffer;
    PUSB_ENDPOINT_DESCRIPTOR Endpoint;

    if (Size < Length)
        return 0;

    RtlZeroMemory(Configuration, sizeof(*Configuration));
    Configuration->bLength = sizeof(*Configuration);
    Configuration->bDescriptorType = Type;
    Configuration->bNumInterfaces = (UCHAR)Context->InterfaceCount;
    Configuration->bConfigurationValue = 1;
    Configuration->bmAttributes = UFX_CONFIG_ATTRIBUTES;
    Configuration->MaxPower = UFX_CONFIG_POWER;
    for (Index = 0; Index < Context->InterfaceCount; Index++)
    {
        PUFX_INTERFACE Interface = &Context->Interfaces[Index];

        if (Length + Interface->DescriptorLength > Size)
            break;

        RtlCopyMemory(Buffer + Length, Interface->Descriptor, Interface->DescriptorLength);
        for (Offset = Interface->Descriptor[0]; Offset + 2 <= Interface->DescriptorLength && Interface->Descriptor[Offset] != 0;
             Offset += Interface->Descriptor[Offset])
        {
            if (Interface->Descriptor[Offset + 1] == USB_ENDPOINT_DESCRIPTOR_TYPE &&
                Offset + sizeof(USB_ENDPOINT_DESCRIPTOR) <= Interface->DescriptorLength)
            {
                Endpoint = (PVOID)(Buffer + Length + Offset);
                Endpoint->wMaxPacketSize = UfxPacketSize(Endpoint, Speed);
            }
        }

        Length += Interface->DescriptorLength;
    }

    Configuration->wTotalLength = (USHORT)Length;
    return Length;
}

static
ULONG
UfxBuildString(
    _In_ PCWSTR Text,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size)
{
    ULONG Length = (ULONG)min(wcslen(Text) * sizeof(WCHAR) + 2, min(Size, 254));

    Buffer[0] = (UCHAR)Length;
    Buffer[1] = USB_STRING_DESCRIPTOR_TYPE;
    RtlCopyMemory(Buffer + 2, Text, Length - 2);
    return Length;
}

static
ULONG
UfxBuildDescriptor(
    _In_ PUFX_DEVICE_CONTEXT Context,
    _In_ USHORT Value,
    _In_ USHORT Language,
    _Out_writes_bytes_(Size) PUCHAR Buffer,
    _In_ ULONG Size)
{
    PUSB_DEVICE_DESCRIPTOR Device = (PVOID)Buffer;
    PUSB_DEVICE_QUALIFIER_DESCRIPTOR Qualifier = (PVOID)Buffer;
    UCHAR Index = (UCHAR)Value;

    switch (Value >> 8)
    {
        case USB_DEVICE_DESCRIPTOR_TYPE:
            RtlZeroMemory(Device, sizeof(*Device));
            Device->bLength = sizeof(*Device);
            Device->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
            Device->bcdUSB = 0x0200;
            Device->bMaxPacketSize0 = UFX_DEFAULT_PACKET;
            Device->idVendor = (USHORT)Context->VendorId;
            Device->idProduct = (USHORT)Context->ProductId;
            Device->iManufacturer = UFX_STRING_MANUFACTURER;
            Device->iProduct = UFX_STRING_PRODUCT;
            Device->iSerialNumber = Context->SerialNumber != 0 ? UFX_STRING_SERIAL : 0;
            Device->bNumConfigurations = 1;
            return sizeof(*Device);

        case USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE:
            RtlZeroMemory(Qualifier, sizeof(*Qualifier));
            Qualifier->bLength = sizeof(*Qualifier);
            Qualifier->bDescriptorType = USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE;
            Qualifier->bcdUSB = 0x0200;
            Qualifier->bMaxPacketSize0 = UFX_DEFAULT_PACKET;
            Qualifier->bNumConfigurations = 1;
            return sizeof(*Qualifier);

        case USB_CONFIGURATION_DESCRIPTOR_TYPE:
            return Index == 0 ? UfxBuildConfiguration(Context, Context->Speed, USB_CONFIGURATION_DESCRIPTOR_TYPE, Buffer, Size) : 0;

        case USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE:
            return Index == 0 ? UfxBuildConfiguration(Context, Context->Speed == UsbHighSpeed ? UsbFullSpeed : UsbHighSpeed,
                                                      USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE, Buffer, Size) : 0;

        case USB_STRING_DESCRIPTOR_TYPE:
            if (Index != 0 && Language != UFX_LANGUAGE)
                return 0;

            switch (Index)
            {
                case 0:
                    Buffer[0] = 4;
                    Buffer[1] = USB_STRING_DESCRIPTOR_TYPE;
                    Buffer[2] = (UCHAR)UFX_LANGUAGE;
                    Buffer[3] = (UCHAR)(UFX_LANGUAGE >> 8);
                    return 4;
                case UFX_STRING_MANUFACTURER:
                    return UfxBuildString(Context->Manufacturer, Buffer, Size);
                case UFX_STRING_PRODUCT:
                    return UfxBuildString(Context->Product, Buffer, Size);
                case UFX_STRING_SERIAL:
                    return Context->SerialNumber != 0 ? UfxBuildString(Context->Serial, Buffer, Size) : 0;
                default:
                    return 0;
            }

        default:
            return 0;
    }
}

static
NTSTATUS
NTAPI
UfxEvtWdmIrpDispatch(
    _In_ WDFDEVICE Device,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG Code,
    _In_ WDFCONTEXT DriverContext,
    _Inout_ PIRP Irp,
    _In_ WDFCONTEXT DispatchContext)
{
    PUFX_FDO_CONTEXT Fdo = UfxGetFdoContext(Device);
    PUFX_DEVICE_CONTEXT Context;
    WDFQUEUE Queue = NULL;
    KIRQL Irql;
    ULONG Index;

    UNREFERENCED_PARAMETER(MajorFunction);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(Code);
    UNREFERENCED_PARAMETER(DriverContext);

    if (Fdo != NULL && Fdo->UfxDevice != NULL)
    {
        Context = UfxGetDeviceContext(Fdo->UfxDevice);
        KeAcquireSpinLock(&Context->Lock, &Irql);
        for (Index = 0; Index < UFX_MAX_PENDING_IRPS; Index++)
        {
            if (Context->PendingIrps[Index].Irp == Irp)
            {
                Queue = Context->PendingIrps[Index].Queue;
                Context->PendingIrps[Index].Irp = NULL;
                break;
            }
        }
        KeReleaseSpinLock(&Context->Lock, Irql);
        if (Queue != NULL)
            return WdfDeviceWdmDispatchIrpToIoQueue(Device, Irp, Queue, WDF_DISPATCH_IRP_TO_IO_QUEUE_NO_FLAGS);
    }

    return WdfDeviceWdmDispatchIrp(Device, Irp, DispatchContext);
}

static
NTSTATUS
UfxSendToQueue(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ WDFQUEUE Queue,
    _In_ ULONG Code,
    _In_ USBFNPIPEID PipeId,
    _Inout_updates_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG_PTR Information)
{
    PDEVICE_OBJECT DeviceObject = WdfDeviceWdmGetDeviceObject(Context->Device);
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    KEVENT Event;
    KIRQL Irql;
    ULONG Index;
    PIRP Irp;

    if (Information != NULL)
        *Information = 0;
    if (Queue == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(Code, DeviceObject, &PipeId, sizeof(PipeId), Length != 0 ? Buffer : NULL, Length, TRUE,
                                        &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeAcquireSpinLock(&Context->Lock, &Irql);
    for (Index = 0; Index < UFX_MAX_PENDING_IRPS - 1 && Context->PendingIrps[Index].Irp != NULL; Index++)
        ;
    Context->PendingIrps[Index].Irp = Irp;
    Context->PendingIrps[Index].Queue = Queue;
    KeReleaseSpinLock(&Context->Lock, Irql);

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (Information != NULL)
        *Information = IoStatus.Information;
    return Status;
}

static
NTSTATUS
UfxSendToEndpoint(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ USBFNPIPEID PipeId,
    _In_ BOOLEAN Command,
    _In_ ULONG Code,
    _Inout_updates_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PUFX_ENDPOINT_CONTEXT Endpoint = UfxFindPipe(Context, PipeId);

    if (Endpoint == NULL)
        return STATUS_INVALID_PARAMETER;

    return UfxSendToQueue(Context, Command ? Endpoint->CommandQueue : Endpoint->TransferQueue, Code, PipeId, Buffer, Length, NULL);
}

static
NTSTATUS
UfxCall(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    KeWaitForSingleObject(&Context->EventDone, Executive, KernelMode, FALSE, NULL);
    return Context->EventStatus;
}

static
VOID
UfxStateChange(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ USBFN_DEVICE_STATE State)
{
    Context->State = State;
    if (Context->Callbacks.EvtDeviceUsbStateChange != NULL)
    {
        KeClearEvent(&Context->EventDone);
        Context->Callbacks.EvtDeviceUsbStateChange(Context->Handle, State);
        UfxCall(Context);
    }
}

static
VOID
UfxPortChange(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ USBFN_PORT_TYPE Port)
{
    if (Context->Callbacks.EvtDevicePortChange != NULL)
    {
        KeClearEvent(&Context->EventDone);
        Context->Callbacks.EvtDevicePortChange(Context->Handle, Port);
        UfxCall(Context);
    }
}

static
VOID
UfxDeliverNotifications(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ ULONG Index)
{
    PUFX_INTERFACE Interface = &Context->Interfaces[Index];
    USBFN_NOTIFICATION Notification;
    PUFX_PDO_CONTEXT Pdo;
    WDFREQUEST Request;
    PVOID Buffer;
    KIRQL Irql;

    if (Index >= Context->InterfaceCount || Interface->Pdo == NULL)
        return;

    Pdo = UfxGetPdoContext(Interface->Pdo);
    for (;;)
    {
        KeAcquireSpinLock(&Context->Lock, &Irql);
        if (Interface->NotificationCount == 0)
        {
            KeReleaseSpinLock(&Context->Lock, Irql);
            return;
        }
        KeReleaseSpinLock(&Context->Lock, Irql);

        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Pdo->NotificationQueue, &Request)))
            return;

        KeAcquireSpinLock(&Context->Lock, &Irql);
        Notification = Interface->Notifications[Interface->NotificationHead];
        Interface->NotificationHead = (Interface->NotificationHead + 1) % UFX_MAX_NOTIFICATIONS;
        Interface->NotificationCount--;
        KeReleaseSpinLock(&Context->Lock, Irql);

        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(Notification), &Buffer, NULL)))
        {
            RtlCopyMemory(Buffer, &Notification, sizeof(Notification));
            WdfRequestComplete(Request, STATUS_SUCCESS);
        }
        else
        {
            WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        }
    }
}

static
VOID
UfxCancelNotifications(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    WDFREQUEST Request;
    ULONG Index;

    for (Index = 0; Index < Context->InterfaceCount; Index++)
    {
        if (Context->Interfaces[Index].Pdo == NULL)
            continue;

        while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(UfxGetPdoContext(Context->Interfaces[Index].Pdo)->NotificationQueue,
                                                        &Request)))
        {
            WdfRequestComplete(Request, STATUS_CANCELLED);
        }
    }
}

static
VOID
UfxNotifyInterface(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ ULONG Index,
    _In_ PUSBFN_NOTIFICATION Source)
{
    PUFX_INTERFACE Interface = &Context->Interfaces[Index];
    KIRQL Irql;

    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (Interface->NotificationCount < UFX_MAX_NOTIFICATIONS)
    {
        Interface->Notifications[(Interface->NotificationHead + Interface->NotificationCount) % UFX_MAX_NOTIFICATIONS] = *Source;
        Interface->NotificationCount++;
    }
    KeReleaseSpinLock(&Context->Lock, Irql);
    UfxDeliverNotifications(Context, Index);
}

static
VOID
UfxNotify(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ USBFN_EVENT Event,
    _In_ ULONG Value)
{
    USBFN_NOTIFICATION Notification;
    ULONG Index;

    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.Event = Event;
    switch (Event)
    {
        case UsbfnEventReset:
            Notification.u.BusSpeed = (USBFN_BUS_SPEED)Value;
            break;
        case UsbfnEventPortType:
            Notification.u.PortType = (USBFN_PORT_TYPE)Value;
            break;
        case UsbfnEventSetInterface:
            Notification.u.AlternateInterface.InterfaceNumber = (USHORT)(Value & 0xFFFF);
            Notification.u.AlternateInterface.AlternateInterfaceNumber = (USHORT)(Value >> 16);
            break;
        default:
            Notification.u.ConfigurationValue = (USHORT)Value;
            break;
    }

    for (Index = 0; Index < Context->InterfaceCount; Index++)
        UfxNotifyInterface(Context, Index, &Notification);
}

static
VOID
UfxNotifySetup(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ ULONG Index,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Setup)
{
    USBFN_NOTIFICATION Notification;

    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.Event = UsbfnEventSetupPacket;
    Notification.u.SetupPacket = *Setup;
    UfxNotifyInterface(Context, Index, &Notification);
}

static
VOID
UfxStall(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ USBFNPIPEID PipeId)
{
    USHORT Stall = TRUE;

    UfxSendToEndpoint(Context, PipeId, TRUE, IOCTL_INTERNAL_USBFN_SET_PIPE_STATE, &Stall, sizeof(Stall));
}

static
VOID
UfxControlIn(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Setup,
    _In_ ULONG Length)
{
    Length = min(Length, Setup->wLength);
    if (NT_SUCCESS(UfxSendToEndpoint(Context, 0, FALSE, IOCTL_INTERNAL_USBFN_TRANSFER_IN, Context->Buffer, Length)))
        UfxSendToEndpoint(Context, 0, FALSE, IOCTL_INTERNAL_USBFN_CONTROL_STATUS_HANDSHAKE_OUT, NULL, 0);
}

static
VOID
UfxControlStatus(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    UfxSendToEndpoint(Context, 0, FALSE, IOCTL_INTERNAL_USBFN_CONTROL_STATUS_HANDSHAKE_IN, NULL, 0);
}

static
VOID
UfxUpdateEndpoints(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    USB_ENDPOINT_DESCRIPTOR Descriptor;
    USBFNPIPEID PipeId;
    ULONG Index;

    for (PipeId = 1; PipeId < 2 * UFX_PIPE_IN; PipeId++)
    for (Index = 0; Index < Context->EndpointCount; Index++)
    {
        if (Context->Endpoints[Index].Endpoint == NULL ||
            UfxPipeId(Context->Endpoints[Index].Descriptor.bEndpointAddress) != PipeId)
        {
            continue;
        }

        Descriptor = Context->Endpoints[Index].Descriptor;
        Descriptor.wMaxPacketSize = UfxPacketSize(&Descriptor, Context->Speed);
        UfxSendToEndpoint(Context, UfxPipeId(Descriptor.bEndpointAddress), TRUE, IOCTL_INTERNAL_USBFN_DESCRIPTOR_UPDATE,
                          &Descriptor, sizeof(Descriptor));
    }
}

static VOID UfxAddEndpoints(_Inout_ PUFX_DEVICE_CONTEXT Context);

static
VOID
UfxReplaceEndpoints(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ ULONG Interface)
{
    UFXENDPOINT Endpoint;
    ULONG Index;

    for (Index = 0; Index < Context->EndpointCount; Index++)
    {
        if (Context->Endpoints[Index].Interface != Interface || Context->Endpoints[Index].Endpoint == NULL)
            continue;

        Endpoint = Context->Endpoints[Index].Endpoint;
        Context->Endpoints[Index].Endpoint = NULL;
        WdfObjectDelete(Endpoint);
    }

    UfxAddEndpoints(Context);
}

static
VOID
UfxStandardRequest(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Setup)
{
    UCHAR Recipient = Setup->bmRequestType.B & 0x1F;
    USHORT Status = 0;
    ULONG Length;

    switch (Setup->bRequest)
    {
        case USB_REQUEST_GET_DESCRIPTOR:
            Length = UfxBuildDescriptor(Context, Setup->wValue.W, Setup->wIndex.W, Context->Buffer, sizeof(Context->Buffer));
            if (Length == 0 || Recipient != BMREQUEST_TO_DEVICE)
                UfxStall(Context, 0);
            else
                UfxControlIn(Context, Setup, Length);
            return;

        case USB_REQUEST_SET_ADDRESS:
            if ((Setup->wValue.LowByte & 0x7F) == 0 || Context->Configuration != 0)
            {
                UfxStall(Context, 0);
                return;
            }

            Context->Address = Setup->wValue.LowByte & 0x7F;
            if (Context->Callbacks.EvtDeviceAddressed != NULL)
            {
                KeClearEvent(&Context->EventDone);
                Context->Callbacks.EvtDeviceAddressed(Context->Handle, Context->Address);
                UfxCall(Context);
            }
            UfxControlStatus(Context);
            UfxStateChange(Context, Context->Address != 0 ? UsbfnDeviceStateAddressed : UsbfnDeviceStateDefault);
            return;

        case USB_REQUEST_GET_CONFIGURATION:
            Context->Buffer[0] = Context->Configuration;
            UfxControlIn(Context, Setup, 1);
            return;

        case USB_REQUEST_SET_CONFIGURATION:
            if (Setup->wValue.LowByte > 1)
            {
                UfxStall(Context, 0);
                return;
            }

            Context->Configuration = Setup->wValue.LowByte;
            if (Context->Configuration != 0)
            {
                UfxUpdateEndpoints(Context);
                UfxStateChange(Context, UsbfnDeviceStateConfigured);
                UfxControlStatus(Context);
                UfxNotify(Context, UsbfnEventConfigured, Context->Configuration);
            }
            else
            {
                UfxNotify(Context, UsbfnEventUnConfigured, 0);
                UfxControlStatus(Context);
                UfxStateChange(Context, UsbfnDeviceStateAddressed);
                UfxPortChange(Context, Context->PortType != UsbfnUnknownPort ? Context->PortType : UsbfnStandardDownstreamPort);
            }
            return;

        case USB_REQUEST_GET_STATUS:
            if (Recipient == BMREQUEST_TO_ENDPOINT)
            {
                PUFX_ENDPOINT_CONTEXT Endpoint = UfxFindPipe(Context, UfxPipeId(Setup->wIndex.LowByte));
                USHORT Stalled;

                if (Endpoint == NULL)
                {
                    UfxStall(Context, 0);
                    return;
                }

                Stalled = Endpoint->Halted;
                UfxSendToEndpoint(Context, Endpoint->PipeId, TRUE, IOCTL_INTERNAL_USBFN_GET_PIPE_STATE, &Stalled, sizeof(Stalled));
                Status = (Stalled & 0xFF) != 0 ? 1 : 0;
            }
            else if (Recipient == BMREQUEST_TO_DEVICE)
            {
                Status = Context->RemoteWake ? 2 : 0;
            }
            RtlCopyMemory(Context->Buffer, &Status, sizeof(Status));
            UfxControlIn(Context, Setup, sizeof(Status));
            return;

        case USB_REQUEST_CLEAR_FEATURE:
        case USB_REQUEST_SET_FEATURE:
            if (Recipient == BMREQUEST_TO_ENDPOINT && Setup->wValue.W == USB_FEATURE_ENDPOINT_STALL &&
                UfxFindPipe(Context, UfxPipeId(Setup->wIndex.LowByte)) != NULL)
            {
                PUFX_ENDPOINT_CONTEXT Endpoint = UfxFindPipe(Context, UfxPipeId(Setup->wIndex.LowByte));
                USHORT Stall = Setup->bRequest == USB_REQUEST_SET_FEATURE;

                Endpoint->Halted = (BOOLEAN)Stall;
                UfxSendToEndpoint(Context, Endpoint->PipeId, TRUE, IOCTL_INTERNAL_USBFN_SET_PIPE_STATE, &Stall, sizeof(Stall));
                UfxControlStatus(Context);
            }
            else if (Recipient == BMREQUEST_TO_DEVICE && Setup->wValue.W == USB_FEATURE_REMOTE_WAKEUP)
            {
                Context->RemoteWake = Setup->bRequest == USB_REQUEST_SET_FEATURE;
                UfxControlStatus(Context);
            }
            else if (Recipient == BMREQUEST_TO_DEVICE && Setup->wValue.W == USB_FEATURE_TEST_MODE &&
                     Setup->bRequest == USB_REQUEST_SET_FEATURE)
            {
                UfxControlStatus(Context);
                if (Context->Callbacks.EvtDeviceTestModeSet != NULL)
                {
                    KeClearEvent(&Context->EventDone);
                    Context->Callbacks.EvtDeviceTestModeSet(Context->Handle, Setup->wIndex.HiByte);
                    UfxCall(Context);
                }
            }
            else
            {
                UfxStall(Context, 0);
            }
            return;

        case USB_REQUEST_GET_INTERFACE:
            Context->Buffer[0] = Context->AlternateSetting;
            UfxControlIn(Context, Setup, 1);
            return;

        case USB_REQUEST_SET_INTERFACE:
            if (Setup->wIndex.LowByte >= Context->InterfaceCount || Setup->wValue.LowByte != 0)
            {
                UfxStall(Context, 0);
                return;
            }

            Context->AlternateSetting = Setup->wValue.LowByte;
            UfxReplaceEndpoints(Context, Setup->wIndex.LowByte);
            UfxNotify(Context, UsbfnEventSetInterface, (ULONG)Setup->wIndex.LowByte | ((ULONG)Setup->wValue.LowByte << 16));
            UfxControlStatus(Context);
            return;

        default:
            UfxStall(Context, 0);
            return;
    }
}

static
VOID
UfxSetup(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET Setup)
{
    UCHAR Type = (Setup->bmRequestType.B >> 5) & 3;
    UCHAR Recipient = Setup->bmRequestType.B & 0x1F;

    if (Type == BMREQUEST_STANDARD)
    {
        UfxStandardRequest(Context, Setup);
        return;
    }

    if (Recipient == BMREQUEST_TO_INTERFACE && Type == BMREQUEST_CLASS && Setup->wIndex.LowByte < Context->InterfaceCount)
    {
        UfxNotifySetup(Context, Setup->wIndex.LowByte, Setup);
        return;
    }

    UfxStall(Context, 0);
}

static
VOID
UfxAddDefaultEndpoint(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    UFXENDPOINT_INIT_DATA Init;

    if (Context->Callbacks.EvtDeviceDefaultEndpointAdd == NULL)
        return;

    RtlZeroMemory(&Init, sizeof(Init));
    Init.UfxDevice = Context->Handle;
    Init.Descriptor.bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
    Init.Descriptor.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
    Init.Descriptor.bmAttributes = USB_ENDPOINT_TYPE_CONTROL;
    Init.Descriptor.wMaxPacketSize = UFX_DEFAULT_PACKET;
    KeClearEvent(&Context->EventDone);
    Context->Callbacks.EvtDeviceDefaultEndpointAdd(Context->Handle, UFX_DEFAULT_PACKET, (PUFXENDPOINT_INIT)&Init);
    UfxCall(Context);
}

static
VOID
UfxAddEndpoints(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    UFXENDPOINT_INIT_DATA Init;
    USB_ENDPOINT_DESCRIPTOR Descriptor;
    ULONG Index;

    if (Context->Callbacks.EvtDeviceEndpointAdd == NULL)
        return;

    for (Index = 0; Index < Context->EndpointCount; Index++)
    {
        if (Context->Endpoints[Index].Endpoint != NULL)
            continue;

        RtlZeroMemory(&Init, sizeof(Init));
        Init.UfxDevice = Context->Handle;
        Descriptor = Context->Endpoints[Index].Descriptor;
        Descriptor.wMaxPacketSize = UfxPacketSize(&Descriptor, Context->Capabilities.MaxSpeed);
        Init.Descriptor = Descriptor;
        Context->Callbacks.EvtDeviceEndpointAdd(Context->Handle, &Descriptor, (PUFXENDPOINT_INIT)&Init);
    }
}

static EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL UfxEvtPdoInternalIoctl;

static
NTSTATUS
UfxCreateChild(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ ULONG Interface)
{
    DECLARE_UNICODE_STRING_SIZE(Identifier, 128);
    DECLARE_CONST_UNICODE_STRING(Instance, L"0");
    DECLARE_UNICODE_STRING_SIZE(Text, 128);
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PUFX_PDO_CONTEXT Pdo;
    PWDFDEVICE_INIT Init;
    WDFDEVICE Device;
    NTSTATUS Status;

    Init = WdfPdoInitAllocate(Context->Device);
    if (Init == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlUnicodeStringPrintf(&Identifier, L"USBFN\\%ws", Context->Interfaces[Interface].Name);
    RtlUnicodeStringPrintf(&Text, L"%ws", Context->Interfaces[Interface].Name);
    Status = WdfPdoInitAssignDeviceID(Init, &Identifier);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddHardwareID(Init, &Identifier);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAssignInstanceID(Init, &Instance);
    if (NT_SUCCESS(Status))
        Status = WdfPdoInitAddDeviceText(Init, &Text, &Text, UFX_LANGUAGE);
    if (!NT_SUCCESS(Status))
    {
        WdfDeviceInitFree(Init);
        return Status;
    }

    WdfPdoInitSetDefaultLocale(Init, UFX_LANGUAGE);
    WdfPdoInitAllowForwardingRequestToParent(Init);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, UFX_PDO_CONTEXT);
    Status = WdfDeviceCreate(&Init, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
    {
        if (Init != NULL)
            WdfDeviceInitFree(Init);
        return Status;
    }

    Pdo = UfxGetPdoContext(Device);
    Pdo->UfxDevice = Context->Handle;
    Pdo->Interface = Interface;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.EvtIoInternalDeviceControl = UfxEvtPdoInternalIoctl;
    QueueConfig.PowerManaged = WdfFalse;
    Status = WdfIoQueueCreate(Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if (NT_SUCCESS(Status))
    {
        WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
        QueueConfig.PowerManaged = WdfFalse;
        Status = WdfIoQueueCreate(Device, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Pdo->NotificationQueue);
    }
    if (NT_SUCCESS(Status))
        Status = WdfFdoAddStaticChild(Context->Device, Device);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Device);
        return Status;
    }

    Context->Interfaces[Interface].Pdo = Device;
    return STATUS_SUCCESS;
}

static
VOID
UfxRemoveChildren(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    WDFDEVICE Pdo;
    ULONG Index;

    for (Index = 0; Index < Context->InterfaceCount; Index++)
    {
        Pdo = Context->Interfaces[Index].Pdo;
        if (Pdo == NULL)
            continue;

        Context->Interfaces[Index].Pdo = NULL;
        Context->Interfaces[Index].NotificationCount = 0;
        if (!NT_SUCCESS(WdfPdoMarkMissing(Pdo)))
            Context->Interfaces[Index].Pdo = Pdo;
    }

    Context->ChildrenCreated = FALSE;
    Context->Active = FALSE;
}

static
VOID
UfxDeleteEndpoints(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    UFXENDPOINT Endpoint;
    ULONG Index;

    for (Index = 0; Index < Context->EndpointCount; Index++)
    {
        Endpoint = Context->Endpoints[Index].Endpoint;
        if (Endpoint == NULL)
            continue;

        Context->Endpoints[Index].Endpoint = NULL;
        WdfObjectDelete(Endpoint);
    }
}

static
VOID
UfxConnect(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    if (Context->Connected || !Context->Attached || !Context->Active)
        return;

    Context->Connected = TRUE;
    if (Context->Callbacks.EvtDeviceHostConnect != NULL)
    {
        KeClearEvent(&Context->EventDone);
        Context->Callbacks.EvtDeviceHostConnect(Context->Handle);
        UfxCall(Context);
    }
}

static
VOID
UfxDisconnect(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    if (!Context->Connected)
        return;

    Context->Connected = FALSE;
    if (Context->Callbacks.EvtDeviceHostDisconnect != NULL)
    {
        KeClearEvent(&Context->EventDone);
        Context->Callbacks.EvtDeviceHostDisconnect(Context->Handle);
        UfxCall(Context);
    }
}

static
VOID
UfxAttach(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    ULONG Index;

    if (Context->Attached)
        return;

    Context->Attached = TRUE;
    Context->PortReported = FALSE;
    Context->PortType = UsbfnUnknownPort;
    UfxStateChange(Context, UsbfnDeviceStateAttached);
    UfxNotify(Context, UsbfnEventAttach, 0);
    UfxPortChange(Context, UsbfnUnknownPort);
    if (Context->Callbacks.EvtDevicePortDetect != NULL)
    {
        KeClearEvent(&Context->EventDone);
        Context->Callbacks.EvtDevicePortDetect(Context->Handle);
        UfxCall(Context);
    }

    if (Context->Failed)
        return;

    UfxAddEndpoints(Context);
    if (!Context->ChildrenCreated)
    {
        Context->ChildrenCreated = TRUE;
        for (Index = 0; Index < Context->InterfaceCount; Index++)
            UfxCreateChild(Context, Index);
    }

    UfxConnect(Context);
}

static
VOID
UfxDetach(
    _Inout_ PUFX_DEVICE_CONTEXT Context)
{
    if (!Context->Attached)
        return;

    Context->Attached = FALSE;
    Context->Configuration = 0;
    Context->Address = 0;
    UfxDisconnect(Context);
    UfxStateChange(Context, UsbfnDeviceStateDetached);
    UfxPortChange(Context, UsbfnUnknownPort);
    UfxCancelNotifications(Context);
    UfxRemoveChildren(Context);
    UfxDeleteEndpoints(Context);
}

static
VOID
UfxReset(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ USB_DEVICE_SPEED Speed)
{
    USB_ENDPOINT_DESCRIPTOR Descriptor;

    Context->Speed = Speed;
    Context->Address = 0;
    Context->Configuration = 0;
    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.bLength = sizeof(Descriptor);
    Descriptor.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
    Descriptor.bmAttributes = USB_ENDPOINT_TYPE_CONTROL;
    Descriptor.wMaxPacketSize = Speed == UsbSuperSpeed ? 512 : UFX_DEFAULT_PACKET;
    UfxSendToEndpoint(Context, 0, TRUE, IOCTL_INTERNAL_USBFN_DESCRIPTOR_UPDATE, &Descriptor, sizeof(Descriptor));
    UfxStateChange(Context, UsbfnDeviceStateDefault);
    if (!Context->PortReported)
    {
        Context->PortReported = TRUE;
        UfxPortChange(Context, Context->PortType != UsbfnUnknownPort ? Context->PortType : UsbfnStandardDownstreamPort);
    }
    UfxNotify(Context, UsbfnEventReset, Speed == UsbSuperSpeed ? UsbfnBusSpeedSuper :
                                        Speed == UsbHighSpeed ? UsbfnBusSpeedHigh :
                                        Speed == UsbFullSpeed ? UsbfnBusSpeedFull : UsbfnBusSpeedLow);
}

static
VOID
UfxHandleEvent(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ PUFX_EVENT Event)
{
    switch (Event->Type)
    {
        case UfxEventHardwareReady:
            if (!Context->HardwareReady)
            {
                Context->HardwareReady = TRUE;
                UfxAddDefaultEndpoint(Context);
            }
            break;

        case UfxEventAttach:
            UfxAttach(Context);
            break;

        case UfxEventDetach:
            UfxDetach(Context);
            break;

        case UfxEventSuspend:
            UfxStateChange(Context, UsbfnDeviceStateSuspended);
            UfxNotify(Context, UsbfnEventSuspend, 0);
            break;

        case UfxEventResume:
            UfxStateChange(Context, Context->Configuration != 0 ? UsbfnDeviceStateConfigured :
                                    Context->Address != 0 ? UsbfnDeviceStateAddressed : UsbfnDeviceStateDefault);
            UfxNotify(Context, UsbfnEventResume, 0);
            break;

        case UfxEventReset:
            if (Context->Connected)
                UfxReset(Context, (USB_DEVICE_SPEED)Event->Value);
            break;

        case UfxEventSetup:
            if (Context->Connected)
                UfxSetup(Context, &Event->Setup);
            break;

        case UfxEventActivate:
            Context->Active = TRUE;
            UfxConnect(Context);
            WdfRequestComplete(Event->Request, STATUS_SUCCESS);
            break;

        case UfxEventDeactivate:
            Context->Active = FALSE;
            UfxDisconnect(Context);
            WdfRequestComplete(Event->Request, STATUS_SUCCESS);
            UfxCancelNotifications(Context);
            break;

        case UfxEventFailure:
            UfxNotify(Context, UsbfnEventDetach, 0);
            if (Context->Callbacks.EvtDeviceControllerReset != NULL)
            {
                KeClearEvent(&Context->EventDone);
                Context->Callbacks.EvtDeviceControllerReset(Context->Handle,
                                                            Context->FailurePresent ? (PUFX_HARDWARE_FAILURE_CONTEXT)Context->Failure : NULL);
                UfxCall(Context);
            }
            Context->Attached = FALSE;
            Context->Connected = FALSE;
            Context->Configuration = 0;
            Context->Address = 0;
            Context->Failed = TRUE;
            UfxCancelNotifications(Context);
            UfxRemoveChildren(Context);
            UfxDeleteEndpoints(Context);
            if (Context->DefaultEndpoint != NULL)
            {
                UFXENDPOINT Endpoint = Context->DefaultEndpoint;

                Context->DefaultEndpoint = NULL;
                WdfObjectDelete(Endpoint);
            }
            Context->HardwareReady = FALSE;
            break;

        case UfxEventRemoteWakeup:
            if (Context->State != UsbfnDeviceStateSuspended || !Context->RemoteWake)
            {
                WdfRequestComplete(Event->Request, STATUS_INVALID_DEVICE_STATE);
                break;
            }

            Context->EventStatus = STATUS_SUCCESS;
            if (Context->Callbacks.EvtDeviceRemoteWakeupSignal != NULL)
            {
                KeClearEvent(&Context->EventDone);
                Context->Callbacks.EvtDeviceRemoteWakeupSignal(Context->Handle);
                UfxCall(Context);
            }
            WdfRequestComplete(Event->Request, Context->EventStatus);
            break;
    }
}

static
VOID
NTAPI
UfxWorker(
    _In_ PVOID StartContext)
{
    PUFX_DEVICE_CONTEXT Context = StartContext;
    UFX_EVENT Event;
    KIRQL Irql;

    for (;;)
    {
        KeWaitForSingleObject(&Context->WorkEvent, Executive, KernelMode, FALSE, NULL);
        for (;;)
        {
            KeAcquireSpinLock(&Context->Lock, &Irql);
            if (Context->EventCount == 0)
            {
                KeClearEvent(&Context->WorkEvent);
                KeReleaseSpinLock(&Context->Lock, Irql);
                break;
            }

            Event = Context->Events[Context->EventHead];
            Context->EventHead = (Context->EventHead + 1) % UFX_MAX_EVENTS;
            Context->EventCount--;
            KeReleaseSpinLock(&Context->Lock, Irql);
            UfxHandleEvent(Context, &Event);
        }

        if (Context->Stopping)
            break;
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
VOID
UfxPost(
    _Inout_ PUFX_DEVICE_CONTEXT Context,
    _In_ UFX_EVENT_TYPE Type,
    _In_ ULONG Value,
    _In_opt_ PUSB_DEFAULT_PIPE_SETUP_PACKET Setup,
    _In_opt_ WDFREQUEST Request)
{
    PUFX_EVENT Event;
    BOOLEAN Posted = FALSE;
    KIRQL Irql;

    KeAcquireSpinLock(&Context->Lock, &Irql);
    if (!Context->Stopping && Context->EventCount < UFX_MAX_EVENTS)
    {
        Event = &Context->Events[(Context->EventHead + Context->EventCount) % UFX_MAX_EVENTS];
        RtlZeroMemory(Event, sizeof(*Event));
        Event->Type = Type;
        Event->Value = Value;
        Event->Request = Request;
        if (Setup != NULL)
            Event->Setup = *Setup;
        Context->EventCount++;
        Posted = TRUE;
        KeSetEvent(&Context->WorkEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Context->Lock, Irql);

    if (!Posted && Request != NULL)
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
}

static
VOID
UfxForward(
    _In_ PUFX_DEVICE_CONTEXT Context,
    _In_ WDFREQUEST Request,
    _In_ BOOLEAN Command,
    _In_ BOOLEAN Configured)
{
    WDF_REQUEST_FORWARD_OPTIONS Options;
    PUFX_ENDPOINT_CONTEXT Endpoint;
    USBFNPIPEID *PipeId;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(USBFNPIPEID), (PVOID *)&PipeId, NULL);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    if (!Context->Active)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    if (Configured && Context->Configuration == 0)
    {
        WdfRequestComplete(Request, STATUS_DEVICE_BUSY);
        return;
    }

    Endpoint = UfxFindPipe(Context, *PipeId);
    if (Endpoint == NULL)
    {
        WdfRequestComplete(Request, STATUS_INVALID_PARAMETER);
        return;
    }

    WDF_REQUEST_FORWARD_OPTIONS_INIT(&Options);
    Status = WdfRequestForwardToParentDeviceIoQueue(Request, Command ? Endpoint->CommandQueue : Endpoint->TransferQueue, &Options);
    if (!NT_SUCCESS(Status))
        WdfRequestComplete(Request, Status);
}

static
VOID
UfxClassInterface(
    _In_ PUFX_DEVICE_CONTEXT Context,
    _In_ ULONG Interface,
    _In_ USB_DEVICE_SPEED Speed,
    _Out_ PUSBFN_CLASS_INTERFACE Class)
{
    ULONG Index;

    RtlZeroMemory(Class, sizeof(*Class));
    Class->InterfaceNumber = Context->Interfaces[Interface].Number;
    Class->PipeArr[0].EpDesc.bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
    Class->PipeArr[0].EpDesc.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
    Class->PipeArr[0].EpDesc.wMaxPacketSize = UFX_DEFAULT_PACKET;
    Class->PipeArr[0].PipeId = 0;
    Class->PipeCount = 1;
    for (Index = 0; Index < Context->EndpointCount && Class->PipeCount < MAX_NUM_USBFN_PIPES; Index++)
    {
        if (Context->Endpoints[Index].Interface != Interface)
            continue;

        Class->PipeArr[Class->PipeCount].EpDesc = Context->Endpoints[Index].Descriptor;
        Class->PipeArr[Class->PipeCount].EpDesc.wMaxPacketSize = UfxPacketSize(&Context->Endpoints[Index].Descriptor, Speed);
        Class->PipeArr[Class->PipeCount].PipeId = UfxPipeId(Context->Endpoints[Index].Descriptor.bEndpointAddress);
        Class->PipeCount++;
    }
}

static
VOID
NTAPI
UfxEvtPdoInternalIoctl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    PUFX_PDO_CONTEXT Pdo = UfxGetPdoContext(WdfIoQueueGetDevice(Queue));
    PUFX_DEVICE_CONTEXT Context = UfxGetDeviceContext(Pdo->UfxDevice);
    PUSBFN_CLASS_INFORMATION_PACKET Information;
    PUFX_INTERFACE Interface = &Context->Interfaces[Pdo->Interface];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
        case IOCTL_INTERNAL_USBFN_GET_CLASS_INFO:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*Information), (PVOID *)&Information, NULL);
            if (NT_SUCCESS(Status))
            {
                RtlZeroMemory(Information, sizeof(*Information));
                UfxClassInterface(Context, Pdo->Interface, UsbFullSpeed, &Information->FullSpeedClassInterface);
                UfxClassInterface(Context, Pdo->Interface, UsbHighSpeed, &Information->HighSpeedClassInterface);
                UfxClassInterface(Context, Pdo->Interface, UsbSuperSpeed, &Information->SuperSpeedClassInterface);
                RtlStringCbCopyW(Information->InterfaceName, sizeof(Information->InterfaceName), Interface->Name);
                RtlStringCbCopyW(Information->InterfaceGuid, sizeof(Information->InterfaceGuid), Interface->Guid);
                Information->HasInterfaceGuid = Interface->HasGuid;
            }
            WdfRequestComplete(Request, Status);
            return;

        case IOCTL_INTERNAL_USBFN_BUS_EVENT_NOTIFICATION:
            if (!Context->Active)
            {
                WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
                return;
            }

            Status = WdfRequestForwardToIoQueue(Request, Pdo->NotificationQueue);
            if (!NT_SUCCESS(Status))
                WdfRequestComplete(Request, Status);
            else
                UfxDeliverNotifications(Context, Pdo->Interface);
            return;

        case IOCTL_INTERNAL_USBFN_ACTIVATE_USB_BUS:
            UfxPost(Context, UfxEventActivate, 0, NULL, Request);
            return;

        case IOCTL_INTERNAL_USBFN_DEACTIVATE_USB_BUS:
            UfxPost(Context, UfxEventDeactivate, 0, NULL, Request);
            return;

        case IOCTL_INTERNAL_USBFN_SIGNAL_REMOTE_WAKEUP:
            if (!Context->Active)
                WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
            else
                UfxPost(Context, UfxEventRemoteWakeup, 0, NULL, Request);
            return;

        case IOCTL_INTERNAL_USBFN_GET_PIPE_STATE:
        case IOCTL_INTERNAL_USBFN_SET_PIPE_STATE:
            UfxForward(Context, Request, TRUE, TRUE);
            return;

        case IOCTL_INTERNAL_USBFN_TRANSFER_IN_APPEND_ZERO_PKT:
        {
            PUFX_ENDPOINT_CONTEXT Endpoint;
            USBFNPIPEID *PipeId;

            if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(Request, sizeof(USBFNPIPEID), (PVOID *)&PipeId, NULL)) &&
                (Endpoint = UfxFindPipe(Context, *PipeId)) != NULL &&
                (OutputBufferLength % max(UfxPacketSize(&Endpoint->Descriptor, Context->Speed), 1)) != 0)
            {
                IoGetCurrentIrpStackLocation(WdfRequestWdmGetIrp(Request))->Parameters.DeviceIoControl.IoControlCode =
                    IOCTL_INTERNAL_USBFN_TRANSFER_IN;
            }
            UfxForward(Context, Request, FALSE, FALSE);
            return;
        }

        case IOCTL_INTERNAL_USBFN_TRANSFER_IN:
        case IOCTL_INTERNAL_USBFN_TRANSFER_OUT:
        case IOCTL_INTERNAL_USBFN_CONTROL_STATUS_HANDSHAKE_IN:
        case IOCTL_INTERNAL_USBFN_CONTROL_STATUS_HANDSHAKE_OUT:
            UfxForward(Context, Request, FALSE, FALSE);
            return;

        default:
            WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
            return;
    }
}

static
VOID
NTAPI
UfxEvtDeviceCleanup(
    _In_ WDFOBJECT Object)
{
    PUFX_DEVICE_CONTEXT Context = UfxGetDeviceContext(Object);
    KIRQL Irql;

    if (Context->Thread == NULL)
        return;

    KeAcquireSpinLock(&Context->Lock, &Irql);
    Context->Stopping = TRUE;
    KeSetEvent(&Context->WorkEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Context->Lock, Irql);
    KeWaitForSingleObject(Context->Thread, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(Context->Thread);
    Context->Thread = NULL;
}

static
NTSTATUS
NTAPI
UfxFdoInitialize(
    _In_ PUFX_GLOBALS Globals,
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit,
    _Inout_ PWDF_OBJECT_ATTRIBUTES FdoAttributes)
{
    UNREFERENCED_PARAMETER(Globals);
    UNREFERENCED_PARAMETER(Driver);
    UNREFERENCED_PARAMETER(FdoAttributes);

    if (DeviceInit == NULL)
        return STATUS_INVALID_PARAMETER;

    return WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit) != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
NTAPI
UfxDeviceCreateInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUFX_DEVICE_CALLBACKS Callbacks,
    _In_ PUFX_DEVICE_CAPABILITIES Capabilities,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _Out_ UFXDEVICE *UfxDevice)
{
    WDF_OBJECT_ATTRIBUTES ObjectAttributes, ContextAttributes;
    PUFX_DEVICE_CONTEXT Context;
    PUFX_FDO_CONTEXT Fdo;
    HANDLE Thread;
    WDFOBJECT Object;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Globals);

    if (UfxDevice == NULL)
        return STATUS_INVALID_PARAMETER;
    *UfxDevice = NULL;
    if (WdfDevice == NULL || Callbacks == NULL || Capabilities == NULL || Callbacks->Size < sizeof(ULONG) ||
        Capabilities->Size < FIELD_OFFSET(UFX_DEVICE_CAPABILITIES, InEndpointBitmap))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Attributes != NULL)
        ObjectAttributes = *Attributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
    if (ObjectAttributes.ParentObject == NULL)
        ObjectAttributes.ParentObject = WdfDevice;

    Status = WdfObjectCreate(&ObjectAttributes, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, UFX_DEVICE_CONTEXT);
    ContextAttributes.EvtCleanupCallback = UfxEvtDeviceCleanup;
    Status = WdfObjectAllocateContext(Object, &ContextAttributes, (PVOID *)&Context);
    if (NT_SUCCESS(Status))
    {
        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, UFX_FDO_CONTEXT);
        Status = WdfObjectAllocateContext(WdfDevice, &ContextAttributes, (PVOID *)&Fdo);
    }
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Object);
        return Status;
    }

    Context->Handle = (UFXDEVICE)Object;
    Context->Device = WdfDevice;
    RtlCopyMemory(&Context->Callbacks, Callbacks, min(Callbacks->Size, sizeof(Context->Callbacks)));
    RtlCopyMemory(&Context->Capabilities, Capabilities, min(Capabilities->Size, sizeof(Context->Capabilities)));
    Context->Speed = Context->Capabilities.MaxSpeed;
    Context->State = UsbfnDeviceStateDetached;
    KeInitializeSpinLock(&Context->Lock);
    KeInitializeEvent(&Context->WorkEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->EventDone, NotificationEvent, FALSE);
    UfxReadConfiguration(Context);
    if (Context->Serial[0] == UNICODE_NULL)
        UfxReadSmbiosUuid(Context);
    UfxCollectEndpoints(Context);
    Fdo->UfxDevice = Context->Handle;

    Status = WdfDeviceConfigureWdmIrpDispatchCallback(WdfDevice, WdfGetDriver(), IRP_MJ_INTERNAL_DEVICE_CONTROL,
                                                      UfxEvtWdmIrpDispatch, Context);
    if (NT_SUCCESS(Status))
        Status = PsCreateSystemThread(&Thread, THREAD_ALL_ACCESS, NULL, NULL, NULL, UfxWorker, Context);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Thread, SYNCHRONIZE, *PsThreadType, KernelMode, (PVOID *)&Context->Thread, NULL);
        ZwClose(Thread);
    }
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Object);
        return Status;
    }

    *UfxDevice = Context->Handle;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
UfxDeviceEventCompleteInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_ NTSTATUS Status)
{
    PUFX_DEVICE_CONTEXT Context;

    UNREFERENCED_PARAMETER(Globals);

    if (UfxDevice == NULL)
        return;

    Context = UfxGetDeviceContext(UfxDevice);
    Context->EventStatus = Status;
    KeSetEvent(&Context->EventDone, IO_NO_INCREMENT, FALSE);
}

#define UFX_NOTIFY_FUNCTION(_Name, _Event)                                    \
static                                                                         \
VOID                                                                           \
NTAPI                                                                          \
_Name(                                                                         \
    _In_ PUFX_GLOBALS Globals,                                                 \
    _In_ UFXDEVICE UfxDevice)                                                  \
{                                                                              \
    UNREFERENCED_PARAMETER(Globals);                                           \
    if (UfxDevice != NULL)                                                     \
        UfxPost(UfxGetDeviceContext(UfxDevice), _Event, 0, NULL, NULL);        \
}

UFX_NOTIFY_FUNCTION(UfxNotifyHardwareReadyInternal, UfxEventHardwareReady)
UFX_NOTIFY_FUNCTION(UfxNotifyAttachInternal, UfxEventAttach)
UFX_NOTIFY_FUNCTION(UfxNotifyDetachInternal, UfxEventDetach)
UFX_NOTIFY_FUNCTION(UfxNotifySuspendInternal, UfxEventSuspend)
UFX_NOTIFY_FUNCTION(UfxNotifyResumeInternal, UfxEventResume)

static
VOID
NTAPI
UfxNotifyResetInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_ USB_DEVICE_SPEED Speed)
{
    UNREFERENCED_PARAMETER(Globals);

    if (UfxDevice != NULL)
        UfxPost(UfxGetDeviceContext(UfxDevice), UfxEventReset, Speed, NULL, NULL);
}

static
VOID
NTAPI
UfxPortDetectCompleteInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_ USBFN_PORT_TYPE PortType)
{
    PUFX_DEVICE_CONTEXT Context;

    UNREFERENCED_PARAMETER(Globals);

    if (UfxDevice == NULL)
        return;

    Context = UfxGetDeviceContext(UfxDevice);
    Context->PortType = PortType;
    Context->EventStatus = STATUS_SUCCESS;
    KeSetEvent(&Context->EventDone, IO_NO_INCREMENT, FALSE);
}

static
VOID
NTAPI
UfxPortDetectCompleteExInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_ USBFN_PORT_TYPE PortType,
    _In_ USBFN_ACTION Action)
{
    UNREFERENCED_PARAMETER(Action);

    UfxPortDetectCompleteInternal(Globals, UfxDevice, PortType);
}

static
VOID
NTAPI
UfxProprietaryChargerDetectCompleteInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_ PUFX_PROPRIETARY_CHARGER Charger)
{
    UNREFERENCED_PARAMETER(Charger);

    UfxDeviceEventCompleteInternal(Globals, UfxDevice, STATUS_SUCCESS);
}

static
VOID
NTAPI
UfxNotifyHardwareFailureInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_opt_ PUFX_HARDWARE_FAILURE_CONTEXT FailureContext)
{
    PUFX_DEVICE_CONTEXT Context;

    UNREFERENCED_PARAMETER(Globals);

    if (UfxDevice == NULL)
        return;

    Context = UfxGetDeviceContext(UfxDevice);
    Context->FailurePresent = FailureContext != NULL;
    if (FailureContext != NULL)
        RtlCopyMemory(Context->Failure, FailureContext, min(max(FailureContext->Size, sizeof(*FailureContext)), sizeof(Context->Failure)));
    UfxPost(Context, UfxEventFailure, 0, NULL, NULL);
}

static
BOOLEAN
NTAPI
UfxDeviceIoControlInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    UNREFERENCED_PARAMETER(Globals);
    UNREFERENCED_PARAMETER(UfxDevice);
    UNREFERENCED_PARAMETER(Request);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(IoControlCode);

    return FALSE;
}

static
NTSTATUS
NTAPI
UfxEndpointCreateInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice,
    _Inout_ PUFXENDPOINT_INIT EndpointInit,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_ PWDF_IO_QUEUE_CONFIG TransferQueueConfig,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES TransferQueueAttributes,
    _In_ PWDF_IO_QUEUE_CONFIG CommandQueueConfig,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES CommandQueueAttributes,
    _Out_ UFXENDPOINT *UfxEndpoint)
{
    PUFXENDPOINT_INIT_DATA Init = (PUFXENDPOINT_INIT_DATA)EndpointInit;
    WDF_OBJECT_ATTRIBUTES ObjectAttributes, ContextAttributes, QueueAttributes;
    PUFX_ENDPOINT_CONTEXT Endpoint;
    PUFX_DEVICE_CONTEXT Context;
    WDFOBJECT Object;
    NTSTATUS Status;
    ULONG Index;

    UNREFERENCED_PARAMETER(Globals);

    if (UfxEndpoint == NULL)
        return STATUS_INVALID_PARAMETER;
    *UfxEndpoint = NULL;
    if (UfxDevice == NULL || Init == NULL || TransferQueueConfig == NULL || CommandQueueConfig == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = UfxGetDeviceContext(UfxDevice);
    if (Attributes != NULL)
        ObjectAttributes = *Attributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
    if (ObjectAttributes.ParentObject == NULL)
        ObjectAttributes.ParentObject = UfxDevice;

    Status = WdfObjectCreate(&ObjectAttributes, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, UFX_ENDPOINT_CONTEXT);
    Status = WdfObjectAllocateContext(Object, &ContextAttributes, (PVOID *)&Endpoint);
    if (!NT_SUCCESS(Status))
        goto Fail;

    Endpoint->UfxDevice = UfxDevice;
    Endpoint->Descriptor = Init->Descriptor;
    Endpoint->Callbacks = Init->Callbacks;
    Endpoint->PipeId = UfxPipeId(Init->Descriptor.bEndpointAddress);

    if (TransferQueueAttributes != NULL)
        QueueAttributes = *TransferQueueAttributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&QueueAttributes);
    QueueAttributes.ParentObject = Object;
    Status = WdfIoQueueCreate(Context->Device, TransferQueueConfig, &QueueAttributes, &Endpoint->TransferQueue);
    if (!NT_SUCCESS(Status))
        goto Fail;

    if (CommandQueueAttributes != NULL)
        QueueAttributes = *CommandQueueAttributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&QueueAttributes);
    QueueAttributes.ParentObject = Object;
    Status = WdfIoQueueCreate(Context->Device, CommandQueueConfig, &QueueAttributes, &Endpoint->CommandQueue);
    if (!NT_SUCCESS(Status))
        goto Fail;

    if ((Init->Descriptor.bEndpointAddress & 0x0F) == 0)
    {
        Context->DefaultEndpoint = (UFXENDPOINT)Object;
    }
    else
    {
        for (Index = 0; Index < Context->EndpointCount; Index++)
        {
            if (Context->Endpoints[Index].Descriptor.bEndpointAddress == Init->Descriptor.bEndpointAddress)
                Context->Endpoints[Index].Endpoint = (UFXENDPOINT)Object;
        }
    }

    *UfxEndpoint = (UFXENDPOINT)Object;
    return STATUS_SUCCESS;

Fail:
    WdfObjectDelete(Object);
    return Status;
}

static
VOID
NTAPI
UfxEndpointInitSetEventCallbacksInternal(
    _In_ PUFX_GLOBALS Globals,
    _Inout_ PUFXENDPOINT_INIT EndpointInit,
    _In_ PUFX_ENDPOINT_CALLBACKS Callbacks)
{
    PUFXENDPOINT_INIT_DATA Init = (PUFXENDPOINT_INIT_DATA)EndpointInit;

    UNREFERENCED_PARAMETER(Globals);

    if (Init != NULL && Callbacks != NULL)
        RtlCopyMemory(&Init->Callbacks, Callbacks, min(Callbacks->Size, sizeof(Init->Callbacks)));
}

static
VOID
NTAPI
UfxEndpointNotifySetupInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXENDPOINT UfxEndpoint,
    _In_ PUSB_DEFAULT_PIPE_SETUP_PACKET SetupInfo)
{
    PUFX_ENDPOINT_CONTEXT Endpoint;

    UNREFERENCED_PARAMETER(Globals);

    if (UfxEndpoint == NULL || SetupInfo == NULL)
        return;

    Endpoint = UfxGetEndpointContext(UfxEndpoint);
    UfxPost(UfxGetDeviceContext(Endpoint->UfxDevice), UfxEventSetup, 0, SetupInfo, NULL);
}

static
WDFQUEUE
NTAPI
UfxEndpointGetTransferQueueInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXENDPOINT UfxEndpoint)
{
    UNREFERENCED_PARAMETER(Globals);

    return UfxEndpoint != NULL ? UfxGetEndpointContext(UfxEndpoint)->TransferQueue : NULL;
}

static
WDFQUEUE
NTAPI
UfxEndpointGetCommandQueueInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXENDPOINT UfxEndpoint)
{
    UNREFERENCED_PARAMETER(Globals);

    return UfxEndpoint != NULL ? UfxGetEndpointContext(UfxEndpoint)->CommandQueue : NULL;
}

static
VOID
NTAPI
UfxNotifyFinalExitInternal(
    _In_ PUFX_GLOBALS Globals,
    _In_ UFXDEVICE UfxDevice)
{
    UNREFERENCED_PARAMETER(Globals);
    UNREFERENCED_PARAMETER(UfxDevice);
}

static PVOID UfxFunctions[UfxFunctionsMax] =
{
    UfxFdoInitialize,
    UfxDeviceCreateInternal,
    UfxDeviceEventCompleteInternal,
    UfxNotifyHardwareReadyInternal,
    UfxNotifyAttachInternal,
    UfxNotifyDetachInternal,
    UfxNotifySuspendInternal,
    UfxNotifyResumeInternal,
    UfxNotifyResetInternal,
    UfxPortDetectCompleteInternal,
    UfxProprietaryChargerDetectCompleteInternal,
    UfxNotifyHardwareFailureInternal,
    UfxDeviceIoControlInternal,
    UfxDeviceIoControlInternal,
    UfxEndpointCreateInternal,
    UfxEndpointInitSetEventCallbacksInternal,
    UfxEndpointNotifySetupInternal,
    UfxEndpointGetTransferQueueInternal,
    UfxEndpointGetCommandQueueInternal,
    UfxPortDetectCompleteExInternal,
    UfxNotifyFinalExitInternal
};

static
NTSTATUS
NTAPI
UfxLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo, ClientGlobals, (PVOID const *)UfxFunctions, RTL_NUMBER_OF(UfxFunctions), 1);
}

static
VOID
NTAPI
UfxLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO UfxLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 1, 0},
    NULL,
    NULL,
    UfxLibraryBindClient,
    UfxLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject, RegistryPath, L"\\Device\\Ufx01000", &UfxLibraryInfo);
}
