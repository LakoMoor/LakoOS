/*
 * PROJECT:     LakoOS SpacemiT K1 USB3 Glue Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Powers the SpacemiT K1 USB3 block and enumerates its xHCI core
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <initguid.h>
#include "k1xdwc3.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;

static ULONG
K1Read(_In_ PUCHAR Base, _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Base + Offset));
}

static VOID
K1Write(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + Offset), Value);
}

static VOID
K1Update(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Mask, _In_ ULONG Value)
{
    K1Write(Base, Offset, (K1Read(Base, Offset) & ~Mask) | (Value & Mask));
}

static VOID
K1Sleep(_In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static BOOLEAN
K1Poll(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Mask, _In_ ULONG Milliseconds)
{
    ULONG Loops = Milliseconds * 20;

    while (Loops--)
    {
        if (K1Read(Base, Offset) & Mask)
            return TRUE;
        KeStallExecutionProcessor(50);
    }
    return (K1Read(Base, Offset) & Mask) != 0;
}

static PUCHAR
K1Map(_In_ ULONG64 Base, _In_ SIZE_T Size)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = (LONGLONG)Base;
    return MmMapIoSpace(Address, Size, MmNonCached);
}

static VOID
K1GpioOutput(_In_ PUCHAR Gpio, _In_ ULONG Number, _In_ BOOLEAN High)
{
    static const ULONG Banks[] = { 0x000, 0x004, 0x008, 0x100 };
    PUCHAR Bank = Gpio + Banks[Number / 32];
    ULONG Bit = 1UL << (Number % 32);

    K1Write(Bank, High ? K1X_GPIO_SET : K1X_GPIO_CLEAR, Bit);
    K1Write(Bank, K1X_GPIO_SET_OUTPUT, Bit);
}

static ULONG
K1ReadBigEndian(_In_reads_bytes_(4) const UCHAR *Bytes)
{
    return ((ULONG)Bytes[0] << 24) | ((ULONG)Bytes[1] << 16) | ((ULONG)Bytes[2] << 8) | Bytes[3];
}

static NTSTATUS
K1QueryBinary(_In_ HANDLE Key, _In_z_ PCWSTR Name, _Out_writes_bytes_(Size) PUCHAR Buffer,
              _In_ ULONG Size, _Out_ PULONG Length)
{
    UCHAR Storage[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 128];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    UNICODE_STRING ValueName;
    ULONG Result;
    NTSTATUS Status;

    *Length = 0;
    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information,
                             sizeof(Storage), &Result);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information->DataLength > Size)
        return STATUS_BUFFER_OVERFLOW;
    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    *Length = Information->DataLength;
    return STATUS_SUCCESS;
}

static BOOLEAN
K1StringListContains(_In_reads_bytes_(Length) const UCHAR *List, _In_ ULONG Length, _In_z_ const CHAR *Wanted)
{
    SIZE_T WantedLength = strlen(Wanted);
    ULONG Offset = 0;

    while (Offset < Length)
    {
        SIZE_T Entry = 0;

        while (Offset + Entry < Length && List[Offset + Entry])
            Entry++;

        if (Entry == WantedLength && !memcmp(List + Offset, Wanted, WantedLength))
            return TRUE;
        Offset += (ULONG)Entry + 1;
    }
    return FALSE;
}

static NTSTATUS
K1FindCore(_In_ PK1XDWC3_EXTENSION Extension)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 128 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    HANDLE Parameters;
    ULONG Index, Result;
    NTSTATUS Status;

    Status = IoOpenDeviceRegistryKey(Extension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Parameters);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = STATUS_NOT_FOUND;
    for (Index = 0; ; ++Index)
    {
        UCHAR Value[128];
        OBJECT_ATTRIBUTES Attributes;
        UNICODE_STRING Name;
        HANDLE Child;
        ULONG Length;

        if (!NT_SUCCESS(ZwEnumerateKey(Parameters, Index, KeyBasicInformation, Basic,
                                       sizeof(Buffer) - sizeof(WCHAR), &Result)))
            break;
        Name.Buffer = Basic->Name;
        Name.Length = (USHORT)Basic->NameLength;
        Name.MaximumLength = (USHORT)Basic->NameLength;
        InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parameters, NULL);
        if (!NT_SUCCESS(ZwOpenKey(&Child, KEY_READ, &Attributes)))
            continue;
        if (NT_SUCCESS(K1QueryBinary(Child, L"compatible", Value, sizeof(Value), &Length)) &&
            K1StringListContains(Value, Length, "snps,dwc3") &&
            NT_SUCCESS(K1QueryBinary(Child, L"reg", Value, sizeof(Value), &Length)) && Length >= 16)
        {
            Extension->CoreBase = ((ULONG64)K1ReadBigEndian(Value) << 32) | K1ReadBigEndian(Value + 4);
            Extension->CoreSize = K1ReadBigEndian(Value + 12);
            if (NT_SUCCESS(K1QueryBinary(Child, L"interrupts", Value, sizeof(Value), &Length)) && Length >= 4)
            {
                Extension->CoreInterrupt = K1ReadBigEndian(Value);
                Status = STATUS_SUCCESS;
            }
        }
        ZwClose(Child);
        if (NT_SUCCESS(Status))
            break;
    }
    ZwClose(Parameters);
    if (NT_SUCCESS(Status) && (Extension->CoreSize < DWC3_REGISTER_SPAN || !Extension->CoreInterrupt))
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    return Status;
}

static VOID
K1ReadGpioList(_In_ HANDLE Key, _In_z_ PCWSTR Gpios, _In_z_ PCWSTR ActiveLow, _In_z_ PCWSTR InterDelay,
               _Out_ PK1XDWC3_GPIO_LIST List)
{
    UCHAR Value[K1X_HUB_MAX_GPIOS * 3 * sizeof(ULONG)];
    BOOLEAN Inverted;
    ULONG Length, Index;

    RtlZeroMemory(List, sizeof(*List));
    Inverted = NT_SUCCESS(K1QueryBinary(Key, ActiveLow, Value, sizeof(Value), &Length));
    if (NT_SUCCESS(K1QueryBinary(Key, InterDelay, Value, sizeof(Value), &Length)) && Length == sizeof(ULONG))
        List->InterDelay = K1ReadBigEndian(Value);
    if (!NT_SUCCESS(K1QueryBinary(Key, Gpios, Value, sizeof(Value), &Length)))
        return;
    for (Index = 0; Index < Length / (3 * sizeof(ULONG)); ++Index)
    {
        ULONG Number = K1ReadBigEndian(Value + (Index * 3 + 1) * sizeof(ULONG));
        ULONG Flags = K1ReadBigEndian(Value + (Index * 3 + 2) * sizeof(ULONG));

        if (Number >= K1X_GPIO_COUNT)
        {
            List->Count = 0;
            return;
        }
        List->Number[List->Count] = Number;
        List->ActiveLow[List->Count] = ((Flags & 1) != 0) != Inverted;
        List->Count++;
    }
}

static VOID
K1ReadHubPower(_In_ PK1XDWC3_EXTENSION Extension, _Out_ PK1XDWC3_HUB_POWER Power)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"vbus-supply");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Parameters, Supply;
    UCHAR Value[16];
    ULONG Length;

    RtlZeroMemory(Power, sizeof(*Power));
    Power->VbusDelay = K1X_HUB_VBUS_DELAY_MS;
    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(Extension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Parameters)))
        return;
    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parameters, NULL);
    if (NT_SUCCESS(ZwOpenKey(&Supply, KEY_READ, &Attributes)))
    {
        if (!NT_SUCCESS(K1QueryBinary(Supply, L"status", Value, sizeof(Value), &Length)) ||
            K1StringListContains(Value, Length, "okay"))
        {
            K1ReadGpioList(Supply, L"hub-gpios", L"hub_gpio_active_low", L"hub_inter_delay_ms", &Power->Hub);
            K1ReadGpioList(Supply, L"vbus-gpios", L"vbus_gpio_active_low", L"vbus_inter_delay_ms", &Power->Vbus);
            if (NT_SUCCESS(K1QueryBinary(Supply, L"vbus_delay_ms", Value, sizeof(Value), &Length)) &&
                Length == sizeof(ULONG))
            {
                Power->VbusDelay = K1ReadBigEndian(Value);
            }
        }
        ZwClose(Supply);
    }
    ZwClose(Parameters);
}

static VOID
K1GpioListSet(_In_ PUCHAR Gpio, _In_ const K1XDWC3_GPIO_LIST *List, _In_ BOOLEAN On)
{
    ULONG Index;

    for (Index = 0; Index < List->Count; ++Index)
    {
        ULONG Entry = On ? Index : List->Count - 1 - Index;

        K1GpioOutput(Gpio, List->Number[Entry], On != List->ActiveLow[Entry]);
        if (List->InterDelay)
            K1Sleep(List->InterDelay);
    }
}

static NTSTATUS
K1PowerUp(_In_ PK1XDWC3_EXTENSION Extension)
{
    K1XDWC3_HUB_POWER HubPower;
    PUCHAR Apmu = K1Map(K1X_APMU_BASE, K1X_APMU_SIZE);
    PUCHAR Usb2 = K1Map(K1X_USB2PHY_BASE, K1X_USB2PHY_SIZE);
    PUCHAR Combo = K1Map(K1X_COMBPHY_BASE, K1X_COMBPHY_SIZE);
    PUCHAR Gpio = K1Map(K1X_GPIO_BASE, K1X_GPIO_SIZE);
    PUCHAR Core = K1Map(Extension->CoreBase, DWC3_REGISTER_SPAN);
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    ULONG Identifier;

    if (!Apmu || !Usb2 || !Combo || !Gpio || !Core)
        goto Done;

    K1ReadHubPower(Extension, &HubPower);
    K1GpioListSet(Gpio, &HubPower.Vbus, FALSE);
    K1GpioListSet(Gpio, &HubPower.Hub, FALSE);
    K1Sleep(K1X_HUB_POWER_OFF_MS);

    K1Update(Apmu, K1X_APMU_USB, K1X_APMU_USB_USB30_CLOCK, K1X_APMU_USB_USB30_CLOCK);
    K1Update(Apmu, K1X_APMU_USB, K1X_APMU_USB_USB30_RESET, 0);
    KeStallExecutionProcessor(20);
    K1Update(Apmu, K1X_APMU_USB, K1X_APMU_USB_USB30_RESET, K1X_APMU_USB_USB30_RESET);
    KeStallExecutionProcessor(150);

    Status = STATUS_IO_TIMEOUT;
    if (!K1Poll(Usb2, K1X_USB2PHY_CONTROL, K1X_USB2PHY_CONTROL_PLL, 50))
    {
        DPRINT1("K1XDWC3: USB2 PHY PLL not ready\n");
        goto Done;
    }
    K1Write(Usb2, K1X_USB2PHY_CONTROL, 0x60EF);
    K1Write(Usb2, K1X_USB2PHY_GATING, 0x1C);
    K1Update(Usb2, K1X_USB2PHY_ANALOG, K1X_USB2PHY_ANALOG_ISEL | K1X_USB2PHY_ANALOG_IREG,
             K1X_USB2PHY_ANALOG_ISEL_15 | K1X_USB2PHY_ANALOG_IREG);
    K1Update(Usb2, K1X_USB2PHY_PATH, K1X_USB2PHY_PATH_HS_SOURCE, 0);
    K1Update(Usb2, K1X_USB2PHY_HOST, K1X_USB2PHY_HOST_NO_CLEAR, K1X_USB2PHY_HOST_NO_CLEAR);

    K1Update(Apmu, K1X_APMU_PHY_SELECT, K1X_APMU_PHY_SELECT_USB3, K1X_APMU_PHY_SELECT_USB3);
    K1Update(Apmu, K1X_APMU_PCIE0, K1X_APMU_PCIE0_COMBO_RESET, 0);
    K1Write(Combo, K1X_COMBPHY_MODE, 0);
    K1Write(Combo, K1X_COMBPHY_PLL_CONTROL, 0x603A2276);
    K1Write(Combo, K1X_COMBPHY_STATUS, 0x97C);
    K1Write(Combo, K1X_COMBPHY_RX_CONTROL, 0);
    if (!K1Poll(Combo, K1X_COMBPHY_STATUS, K1X_COMBPHY_STATUS_PLL, 10))
    {
        DPRINT1("K1XDWC3: USB3 PHY PLL not ready\n");
        goto Done;
    }
    K1Update(Combo, K1X_COMBPHY_LFPS, K1X_COMBPHY_LFPS_MASK, K1X_COMBPHY_LFPS_DEFAULT);

    Identifier = K1Read(Core, DWC3_GSNPSID);
    if ((Identifier & 0xFFFF0000UL) != 0x55330000UL &&
        (Identifier & 0xFFFF0000UL) != 0x33310000UL &&
        (Identifier & 0xFFFF0000UL) != 0x33320000UL)
    {
        DPRINT1("K1XDWC3: unexpected core identifier 0x%08lx\n", Identifier);
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    K1Update(Core, DWC3_GCTL, DWC3_GCTL_PRTCAP_MASK, DWC3_GCTL_PRTCAP_HOST);
    K1Update(Core, DWC3_GUSB2PHYCFG, DWC3_GUSB2PHYCFG_SUSPHY | DWC3_GUSB2PHYCFG_ENBLSLPM, 0);
    K1Update(Core, DWC3_GUSB3PIPECTL, DWC3_GUSB3PIPECTL_SUSPHY | DWC3_GUSB3PIPECTL_DEPOCHG, 0);
    K1Update(Core, DWC3_GUCTL1, DWC3_GUCTL1_PARKMODE_SS | DWC3_GUCTL1_IPGAP_CHECK,
             DWC3_GUCTL1_PARKMODE_SS | DWC3_GUCTL1_IPGAP_CHECK);

    K1GpioListSet(Gpio, &HubPower.Hub, TRUE);
    if (HubPower.Vbus.Count)
        K1Sleep(HubPower.VbusDelay);
    K1GpioListSet(Gpio, &HubPower.Vbus, TRUE);
    DPRINT("K1XDWC3: core 0x%08lx at 0x%I64x interrupt %lu ready\n",
           Identifier, Extension->CoreBase, Extension->CoreInterrupt);
    Status = STATUS_SUCCESS;

Done:
    if (Core)
        MmUnmapIoSpace(Core, DWC3_REGISTER_SPAN);
    if (Gpio)
        MmUnmapIoSpace(Gpio, K1X_GPIO_SIZE);
    if (Combo)
        MmUnmapIoSpace(Combo, K1X_COMBPHY_SIZE);
    if (Usb2)
        MmUnmapIoSpace(Usb2, K1X_USB2PHY_SIZE);
    if (Apmu)
        MmUnmapIoSpace(Apmu, K1X_APMU_SIZE);
    return Status;
}

static NTSTATUS
K1QueryBusInterface(_In_ PK1XDWC3_EXTENSION Extension)
{
    PDEVICE_OBJECT Top = IoGetAttachedDeviceReference(Extension->LowerDevice);
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Top, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
    {
        ObDereferenceObject(Top);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&Extension->BusInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    Status = IoCallDriver(Top, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    ObDereferenceObject(Top);
    Extension->BusInterfaceValid = NT_SUCCESS(Status);
    return Status;
}

static NTSTATUS
K1CreateChild(_In_ PK1XDWC3_EXTENSION Extension)
{
    PK1XDWC3_EXTENSION ChildExtension;
    PDEVICE_OBJECT Child;
    NTSTATUS Status;

    if (Extension->Child)
        return STATUS_SUCCESS;
    Status = IoCreateDevice(Extension->Self->DriverObject, sizeof(K1XDWC3_EXTENSION), NULL,
                            FILE_DEVICE_CONTROLLER, FILE_AUTOGENERATED_DEVICE_NAME, FALSE, &Child);
    if (!NT_SUCCESS(Status))
        return Status;
    ChildExtension = Child->DeviceExtension;
    RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
    ChildExtension->Type = K1XDWC3_PDO;
    ChildExtension->Self = Child;
    ChildExtension->Parent = Extension;
    ChildExtension->CoreBase = Extension->CoreBase;
    ChildExtension->CoreSize = Extension->CoreSize;
    ChildExtension->CoreInterrupt = Extension->CoreInterrupt;
    Child->Flags &= ~DO_DEVICE_INITIALIZING;
    Extension->Child = Child;
    Extension->ChildReported = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1CopyString(_In_reads_bytes_(Size) const WCHAR *Source, _In_ SIZE_T Size, _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, K1XDWC3_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1ChildResources(_In_ PK1XDWC3_EXTENSION Extension, _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors[2]);
    PCM_RESOURCE_LIST List = ExAllocatePoolWithTag(PagedPool, Size, K1XDWC3_TAG);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, Size);
    List->Count = 1;
    List->List[0].InterfaceType = Internal;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 2;
    Resource = &List->List[0].PartialResourceList.PartialDescriptors[0];
    Resource->Type = CmResourceTypeMemory;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Resource->u.Memory.Start.QuadPart = (LONGLONG)Extension->CoreBase;
    Resource->u.Memory.Length = Extension->CoreSize;
    Resource++;
    Resource->Type = CmResourceTypeInterrupt;
    Resource->ShareDisposition = CmResourceShareShared;
    Resource->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Resource->u.Interrupt.Level = Extension->CoreInterrupt;
    Resource->u.Interrupt.Vector = Extension->CoreInterrupt;
    Resource->u.Interrupt.Affinity = 1;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1ChildRequirements(_In_ PK1XDWC3_EXTENSION Extension, _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors[2]);
    PIO_RESOURCE_REQUIREMENTS_LIST List = ExAllocatePoolWithTag(PagedPool, Size, K1XDWC3_TAG);
    PIO_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, Size);
    List->ListSize = (ULONG)Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 2;
    Resource = &List->List[0].Descriptors[0];
    Resource->Type = CmResourceTypeMemory;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Resource->u.Memory.Length = Extension->CoreSize;
    Resource->u.Memory.Alignment = 1;
    Resource->u.Memory.MinimumAddress.QuadPart = (LONGLONG)Extension->CoreBase;
    Resource->u.Memory.MaximumAddress.QuadPart = (LONGLONG)(Extension->CoreBase + Extension->CoreSize - 1);
    Resource++;
    Resource->Type = CmResourceTypeInterrupt;
    Resource->ShareDisposition = CmResourceShareShared;
    Resource->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Resource->u.Interrupt.MinimumVector = Extension->CoreInterrupt;
    Resource->u.Interrupt.MaximumVector = Extension->CoreInterrupt;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static NTSTATUS
K1ChildPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XDWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR Id[] = L"FDT\\snps_dwc3";
                    Status = K1CopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                {
                    static const WCHAR Ids[] = L"FDT\\snps_dwc3\0";
                    Status = K1CopyString(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR Id[] = L"0";
                    Status = K1CopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
            {
                static const WCHAR Text[] = L"SpacemiT K1 USB 3.0 xHCI controller";
                Status = K1CopyString(Text, sizeof(Text), &Irp->IoStatus.Information);
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = K1ChildResources(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = K1ChildRequirements(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
        {
            PPNP_BUS_INFORMATION Information = ExAllocatePoolWithTag(PagedPool, sizeof(*Information), K1XDWC3_TAG);

            if (!Information)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Information->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
            Information->LegacyBusType = Internal;
            Information->BusNumber = 0;
            Irp->IoStatus.Information = (ULONG_PTR)Information;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_INTERFACE:
            if (IsEqualGUID(Stack->Parameters.QueryInterface.InterfaceType, &GUID_BUS_INTERFACE_STANDARD) &&
                Extension->Parent && Extension->Parent->BusInterfaceValid &&
                Stack->Parameters.QueryInterface.Interface &&
                Stack->Parameters.QueryInterface.Size >= sizeof(BUS_INTERFACE_STANDARD))
            {
                PBUS_INTERFACE_STANDARD Interface = (PBUS_INTERFACE_STANDARD)Stack->Parameters.QueryInterface.Interface;

                *Interface = Extension->Parent->BusInterface;
                if (Interface->InterfaceReference)
                    Interface->InterfaceReference(Interface->Context);
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), K1XDWC3_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;
                ObReferenceObject(DeviceObject);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            if (!Extension->Parent || !Extension->Parent->ChildReported)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Extension->Parent)
                    Extension->Parent->Child = NULL;
                IoDeleteDevice(DeviceObject);
                return Status;
            }
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS
K1FdoPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XDWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = K1FindCore(Extension);
            if (NT_SUCCESS(Status))
                Status = K1QueryBusInterface(Extension);
            if (NT_SUCCESS(Status))
                Status = K1PowerUp(Extension);
            if (NT_SUCCESS(Status))
                Status = K1CreateChild(Extension);
            if (NT_SUCCESS(Status))
                IoInvalidateDeviceRelations(Extension->PhysicalDevice, BusRelations);
            else
                DPRINT1("K1XDWC3: start failed 0x%08lx\n", Status);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;
                ULONG Extra = (Extension->Child && Extension->ChildReported) ? 1 : 0;

                Relations = ExAllocatePoolWithTag(PagedPool,
                                                  FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + Extra + 1]),
                                                  K1XDWC3_TAG);
                if (!Relations)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                Relations->Count = Count;
                if (Count)
                    RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
                if (Extra)
                {
                    Relations->Objects[Relations->Count++] = Extension->Child;
                    ObReferenceObject(Extension->Child);
                }
                if (Previous)
                    ExFreePool(Previous);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            Extension->ChildReported = FALSE;
            if (Extension->Child)
            {
                PK1XDWC3_EXTENSION ChildExtension = Extension->Child->DeviceExtension;

                ChildExtension->Parent = NULL;
                IoDeleteDevice(Extension->Child);
                Extension->Child = NULL;
            }
            if (Extension->BusInterfaceValid && Extension->BusInterface.InterfaceDereference)
                Extension->BusInterface.InterfaceDereference(Extension->BusInterface.Context);
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1DispatchPnp(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XDWC3_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Extension->Type == K1XDWC3_PDO)
        return K1ChildPnp(DeviceObject, Irp);
    return K1FdoPnp(DeviceObject, Irp);
}

static NTSTATUS NTAPI
K1DispatchPower(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XDWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (Extension->Type == K1XDWC3_PDO)
    {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1DispatchOther(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    PK1XDWC3_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Extension->Type == K1XDWC3_PDO)
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static NTSTATUS NTAPI
K1AddDevice(_In_ PDRIVER_OBJECT DriverObject, _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PK1XDWC3_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(K1XDWC3_EXTENSION), NULL, FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Type = K1XDWC3_FDO;
    Extension->Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = K1DispatchOther;
    DriverObject->MajorFunction[IRP_MJ_PNP] = K1DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = K1DispatchPower;
    DriverObject->DriverExtension->AddDevice = K1AddDevice;
    return STATUS_SUCCESS;
}
