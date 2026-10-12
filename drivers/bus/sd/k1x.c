/*
 * PROJECT:     LakoOS SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 SDHCI host support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "sdbus.h"
#include "hardware.h"

#define NDEBUG
#include <debug.h>

#define K1X_APMU_BASE           0xD4282800ULL
#define K1X_APMU_SIZE           0x100
#define K1X_APMU_SDH0           0x054
#define K1X_APMU_SDH1           0x058
#define K1X_APMU_SDH2           0x0E0
#define K1X_APMU_AXI_RESET      (1UL << 0)
#define K1X_APMU_HOST_RESET     (1UL << 1)
#define K1X_APMU_AXI_CLOCK      (1UL << 3)
#define K1X_APMU_HOST_CLOCK     (1UL << 4)
#define K1X_APMU_CLOCK_SELECT   0x000007E0UL
#define K1X_APMU_FREQ_CHANGE    (1UL << 11)

#define K1X_GPIO_BASE           0xD4019000ULL
#define K1X_GPIO_SIZE           0x200
#define K1X_GPIO_LEVEL          0x000
#define K1X_GPIO_SET            0x018
#define K1X_GPIO_SET_OUTPUT     0x054

#define K1X_MFPR_BASE           0xD401E000ULL
#define K1X_MFPR_SIZE           0x200
#define K1X_MFPR_OFFSET(Gpio)   (((Gpio) + 1) * sizeof(ULONG))
#define K1X_MFPR_FUNCTION_MASK  0x7UL
#define K1X_MFPR_SDH1           0x0000D041UL
#define K1X_SDH1_FIRST_PAD      15
#define K1X_SDH1_PAD_COUNT      6
#define K1X_POWER_ON_DELAY_MS   10

#define K1X_SDHC_TX_CFG         0x11C
#define K1X_SDHC_TX_INT_CLK     0x40000000UL
#define K1X_SDHC_MMC_CTRL       0x114
#define K1X_SDHC_MMC_CARD_MODE  0x00001000UL
#define K1X_SDHC_MMC_STROBE     0x00000100UL
#define K1X_SDHC_MMC_HS400      0x00000200UL
#define K1X_SDHC_PHY_CTRL       0x160
#define K1X_SDHC_PHY_FUNC_EN    0x00000001UL
#define K1X_SDHC_PHY_PLL_LOCK   0x00000002UL
#define K1X_SDHC_PHY_DLLCFG     0x168
#define K1X_SDHC_DLL_SETUP      0x00000054UL
#define K1X_SDHC_DLL_ENABLE     0x80000000UL
#define K1X_SDHC_PHY_DLLCFG1    0x16C
#define K1X_SDHC_DLL_REG1_MASK  0x000000FFUL
#define K1X_SDHC_DLL_REG1       0x00000092UL
#define K1X_SDHC_PHY_DLLSTS     0x170
#define K1X_SDHC_DLL_LOCKED     0x00000001UL
#define K1X_SDHC_PHY_PADCFG     0x178
#define K1X_SDHC_RX_BIAS        (1UL << 5)
#define K1X_SDHC_DRIVE_MASK     0x7UL
#define K1X_SDHC_DRIVE_DEFAULT  0x4UL

typedef struct _SDBUS_K1X_EXTENSION
{
    SDBUS_HARDWARE_EXTENSION Header;
    ULONG64 Base;
    ULONG ClockKhz;
    ULONG PhyModule;
    ULONG CardDetectGpio;
    BOOLEAN HasCardDetect;
    BOOLEAN CardDetectInverted;
    BOOLEAN NonRemovable;
    BOOLEAN Hs400EnhancedStrobe;
    BOOLEAN HasPowerGpio;
    BOOLEAN HasRegOnGpio;
    ULONG PowerGpio;
    ULONG RegOnGpio;
} SDBUS_K1X_EXTENSION, *PSDBUS_K1X_EXTENSION;

static ULONG
SdBusK1xBigEndian(_In_reads_bytes_(4) const UCHAR *Bytes)
{
    return ((ULONG)Bytes[0] << 24) | ((ULONG)Bytes[1] << 16) | ((ULONG)Bytes[2] << 8) | Bytes[3];
}

static BOOLEAN
SdBusK1xQuery(_In_ HANDLE Key, _In_z_ PCWSTR Name, _Out_writes_bytes_(Size) PUCHAR Buffer,
              _In_ ULONG Size, _Out_ PULONG Length)
{
    UCHAR Storage[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + 64];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Storage;
    UNICODE_STRING ValueName;
    ULONG Result;

    *Length = 0;
    RtlInitUnicodeString(&ValueName, Name);
    if (!NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, Information,
                                    sizeof(Storage), &Result)) ||
        Information->DataLength > Size)
        return FALSE;
    RtlCopyMemory(Buffer, Information->Data, Information->DataLength);
    *Length = Information->DataLength;
    return TRUE;
}

static VOID
SdBusK1xUpdate(_In_ PUCHAR Base, _In_ ULONG Offset, _In_ ULONG Mask, _In_ ULONG Value)
{
    PULONG Register = (PULONG)(Base + Offset);

    WRITE_REGISTER_ULONG(Register, (READ_REGISTER_ULONG(Register) & ~Mask) | (Value & Mask));
}

static BOOLEAN
SdBusK1xIsCompatible(_In_ HANDLE Key, _In_z_ const CHAR *Wanted)
{
    UCHAR Value[64];
    SIZE_T WantedLength = strlen(Wanted);
    ULONG Length, Offset = 0;

    if (!SdBusK1xQuery(Key, L"compatible", Value, sizeof(Value), &Length))
        return FALSE;
    while (Offset < Length)
    {
        SIZE_T Entry = 0;

        while (Offset + Entry < Length && Value[Offset + Entry])
            Entry++;
        if (Entry == WantedLength && !memcmp(Value + Offset, Wanted, WantedLength))
            return TRUE;
        Offset += (ULONG)Entry + 1;
    }
    return FALSE;
}

static HANDLE
SdBusK1xOpenChild(_In_ HANDLE Parent, _In_z_ const CHAR *Compatible)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Basic = (PKEY_BASIC_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    HANDLE Child;
    ULONG Index, Result;

    for (Index = 0;
         NT_SUCCESS(ZwEnumerateKey(Parent, Index, KeyBasicInformation, Basic, sizeof(Buffer), &Result));
         ++Index)
    {
        Name.Buffer = Basic->Name;
        Name.Length = (USHORT)Basic->NameLength;
        Name.MaximumLength = (USHORT)Basic->NameLength;
        InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, Parent, NULL);
        if (!NT_SUCCESS(ZwOpenKey(&Child, KEY_READ, &Attributes)))
            continue;
        if (SdBusK1xIsCompatible(Child, Compatible))
            return Child;
        ZwClose(Child);
    }
    return NULL;
}

static VOID
SdBusK1xFindPowerSequence(_In_ HANDLE Key, _Inout_ PSDBUS_K1X_EXTENSION Extension)
{
    UCHAR Value[16];
    HANDLE Rf, Wlan;
    ULONG Length;

    Rf = SdBusK1xOpenChild(Key, "spacemit,rf-pwrseq");
    if (!Rf)
        return;
    if (SdBusK1xQuery(Rf, L"pwr-gpios", Value, sizeof(Value), &Length) && Length >= 8)
    {
        Extension->PowerGpio = SdBusK1xBigEndian(Value + 4);
        Extension->HasPowerGpio = Extension->PowerGpio < 128;
    }
    Wlan = SdBusK1xOpenChild(Rf, "spacemit,wlan-pwrseq");
    if (Wlan)
    {
        if (SdBusK1xQuery(Wlan, L"regon-gpios", Value, sizeof(Value), &Length) && Length >= 8)
        {
            Extension->RegOnGpio = SdBusK1xBigEndian(Value + 4);
            Extension->HasRegOnGpio = Extension->RegOnGpio < 128;
        }
        ZwClose(Wlan);
    }
    ZwClose(Rf);
}

static VOID
SdBusK1xSleep(_In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KeStallExecutionProcessor(Milliseconds * 1000);
        return;
    }
    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static VOID
SdBusK1xDriveHigh(_In_ PUCHAR Mfpr, _In_ PUCHAR Gpio, _In_ ULONG Number)
{
    static const ULONG Banks[] = { 0x000, 0x004, 0x008, 0x100 };
    PUCHAR Bank = Gpio + Banks[Number / 32];
    ULONG Bit = 1UL << (Number % 32);

    SdBusK1xUpdate(Mfpr, K1X_MFPR_OFFSET(Number), K1X_MFPR_FUNCTION_MASK, 0);
    WRITE_REGISTER_ULONG((PULONG)(Bank + K1X_GPIO_SET), Bit);
    WRITE_REGISTER_ULONG((PULONG)(Bank + K1X_GPIO_SET_OUTPUT), Bit);
}

static NTSTATUS
SdBusK1xPowerSequence(_In_ PSDBUS_K1X_EXTENSION Extension)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR Mfpr, Gpio;

    if (!Extension->HasPowerGpio && !Extension->HasRegOnGpio)
        return STATUS_SUCCESS;
    Address.QuadPart = K1X_MFPR_BASE;
    Mfpr = MmMapIoSpace(Address, K1X_MFPR_SIZE, MmNonCached);
    Address.QuadPart = K1X_GPIO_BASE;
    Gpio = MmMapIoSpace(Address, K1X_GPIO_SIZE, MmNonCached);
    if (Mfpr && Gpio)
    {
        if (Extension->HasPowerGpio)
        {
            SdBusK1xDriveHigh(Mfpr, Gpio, Extension->PowerGpio);
            SdBusK1xSleep(K1X_POWER_ON_DELAY_MS);
        }
        if (Extension->HasRegOnGpio)
        {
            SdBusK1xDriveHigh(Mfpr, Gpio, Extension->RegOnGpio);
            SdBusK1xSleep(K1X_POWER_ON_DELAY_MS);
        }
    }
    if (Gpio)
        MmUnmapIoSpace(Gpio, K1X_GPIO_SIZE);
    if (Mfpr)
        MmUnmapIoSpace(Mfpr, K1X_MFPR_SIZE);
    return (Mfpr && Gpio) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static NTSTATUS
SdBusK1xSelectSdh1(_In_ PUCHAR Apmu)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR Mfpr;
    ULONG Pad, Loops;

    SdBusK1xUpdate(Apmu, K1X_APMU_SDH1, K1X_APMU_CLOCK_SELECT,
                   READ_REGISTER_ULONG((PULONG)(Apmu + K1X_APMU_SDH0)));
    SdBusK1xUpdate(Apmu, K1X_APMU_SDH1, K1X_APMU_FREQ_CHANGE, K1X_APMU_FREQ_CHANGE);
    for (Loops = 0;
         Loops < 100 && (READ_REGISTER_ULONG((PULONG)(Apmu + K1X_APMU_SDH1)) & K1X_APMU_FREQ_CHANGE);
         ++Loops)
    {
        KeStallExecutionProcessor(10);
    }

    Address.QuadPart = K1X_MFPR_BASE;
    Mfpr = MmMapIoSpace(Address, K1X_MFPR_SIZE, MmNonCached);
    if (!Mfpr)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Pad = 0; Pad < K1X_SDH1_PAD_COUNT; ++Pad)
        WRITE_REGISTER_ULONG((PULONG)(Mfpr + K1X_MFPR_OFFSET(K1X_SDH1_FIRST_PAD + Pad)), K1X_MFPR_SDH1);
    MmUnmapIoSpace(Mfpr, K1X_MFPR_SIZE);
    return STATUS_SUCCESS;
}

static NTSTATUS
SdBusK1xPowerUp(_In_ PSDBUS_K1X_EXTENSION Extension)
{
    PHYSICAL_ADDRESS Address;
    PUCHAR Apmu;
    ULONG Host;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (Extension->Base)
    {
        case 0xD4280000ULL:
            Host = K1X_APMU_SDH0;
            break;
        case 0xD4280800ULL:
            Host = K1X_APMU_SDH1;
            break;
        case 0xD4281000ULL:
            Host = K1X_APMU_SDH2;
            break;
        default:
            return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Address.QuadPart = K1X_APMU_BASE;
    Apmu = MmMapIoSpace(Address, K1X_APMU_SIZE, MmNonCached);
    if (!Apmu)
        return STATUS_INSUFFICIENT_RESOURCES;
    SdBusK1xUpdate(Apmu, K1X_APMU_SDH0, K1X_APMU_AXI_CLOCK | K1X_APMU_AXI_RESET,
                   K1X_APMU_AXI_CLOCK | K1X_APMU_AXI_RESET);
    SdBusK1xUpdate(Apmu, Host, K1X_APMU_HOST_CLOCK | K1X_APMU_HOST_RESET,
                   K1X_APMU_HOST_CLOCK | K1X_APMU_HOST_RESET);
    KeStallExecutionProcessor(10);
    if (Host == K1X_APMU_SDH1)
        Status = SdBusK1xSelectSdh1(Apmu);
    MmUnmapIoSpace(Apmu, K1X_APMU_SIZE);
    if (NT_SUCCESS(Status))
        Status = SdBusK1xPowerSequence(Extension);
    return Status;
}

static BOOLEAN
SdBusK1xCardPresent(_In_ PSDBUS_K1X_EXTENSION Extension)
{
    static const ULONG Banks[] = { 0x000, 0x004, 0x008, 0x100 };
    PHYSICAL_ADDRESS Address;
    PUCHAR Gpio;
    BOOLEAN High;

    if (Extension->NonRemovable || !Extension->HasCardDetect || Extension->CardDetectGpio >= 128)
        return TRUE;
    Address.QuadPart = K1X_GPIO_BASE;
    Gpio = MmMapIoSpace(Address, K1X_GPIO_SIZE, MmNonCached);
    if (!Gpio)
        return TRUE;
    High = (READ_REGISTER_ULONG((PULONG)(Gpio + Banks[Extension->CardDetectGpio / 32] + K1X_GPIO_LEVEL)) &
            (1UL << (Extension->CardDetectGpio % 32))) != 0;
    MmUnmapIoSpace(Gpio, K1X_GPIO_SIZE);
    return Extension->CardDetectInverted ? !High : High;
}

static VOID
SdBusK1xRelease(_In_ PFDO_EXTENSION FdoExtension)
{
    ExFreePoolWithTag(FdoExtension->HardwareExtension, TAG_SDBUS);
    FdoExtension->HardwareExtension = NULL;
}

static VOID
SdBusK1xInitializeController(_In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_K1X_EXTENSION Extension = FdoExtension->HardwareExtension;
    PUCHAR Registers = FdoExtension->RegisterBase;
    UCHAR HostControl;

    if (Extension->PhyModule)
    {
        SdBusK1xUpdate(Registers, K1X_SDHC_PHY_CTRL, K1X_SDHC_PHY_FUNC_EN | K1X_SDHC_PHY_PLL_LOCK,
                       K1X_SDHC_PHY_FUNC_EN | K1X_SDHC_PHY_PLL_LOCK);
        SdBusK1xUpdate(Registers, K1X_SDHC_PHY_PADCFG, K1X_SDHC_RX_BIAS | K1X_SDHC_DRIVE_MASK,
                       K1X_SDHC_RX_BIAS | K1X_SDHC_DRIVE_DEFAULT);
        SdBusK1xUpdate(Registers, K1X_SDHC_MMC_CTRL, K1X_SDHC_MMC_CARD_MODE, K1X_SDHC_MMC_CARD_MODE);
    }
    else
    {
        SdBusK1xUpdate(Registers, K1X_SDHC_TX_CFG, K1X_SDHC_TX_INT_CLK, K1X_SDHC_TX_INT_CLK);
    }

    HostControl = SdBusReadReg8(FdoExtension, SDHCI_HOST_CONTROL) &
                  ~(SDHCI_HC_CARD_DETECT_SIGNAL | SDHCI_HC_CARD_DETECT_TEST);
    HostControl |= SDHCI_HC_CARD_DETECT_SIGNAL;
    if (SdBusK1xCardPresent(Extension))
        HostControl |= SDHCI_HC_CARD_DETECT_TEST;
    SdBusWriteReg8(FdoExtension, SDHCI_HOST_CONTROL, HostControl);
}

static VOID
SdBusK1xGetSlotCapabilities(_In_ PFDO_EXTENSION FdoExtension, _Inout_ PULONG Capabilities,
                            _Inout_ PULONG Capabilities2)
{
    PSDBUS_K1X_EXTENSION Extension = FdoExtension->HardwareExtension;
    ULONG Mhz = Extension->ClockKhz / 1000;

    *Capabilities &= ~(SDHCI_CAP_BASE_CLK_MASK | SDHCI_CAP_VOLTAGE_180 | SDHCI_CAP_SLOT_TYPE_MASK);
    *Capabilities |= (min(Mhz, 255UL) << SDHCI_CAP_BASE_CLK_SHIFT) & SDHCI_CAP_BASE_CLK_MASK;
    *Capabilities |= SDHCI_CAP_VOLTAGE_330;
    if (Extension->NonRemovable)
        *Capabilities |= SDHCI_CAP_SLOT_TYPE_EMBEDDED;
    *Capabilities2 &= ~(SDHCI_CAP2_SDR50_SUPPORT | SDHCI_CAP2_SDR104_SUPPORT |
                        SDHCI_CAP2_DDR50_SUPPORT | SDHCI_CAP2_HS400_SUPPORT);
}

static ULONG
SdBusK1xGetBaseClock(_In_ PFDO_EXTENSION FdoExtension)
{
    return ((PSDBUS_K1X_EXTENSION)FdoExtension->HardwareExtension)->ClockKhz;
}

static NTSTATUS
SdBusK1xSetHs400EnhancedStrobe(_In_ PFDO_EXTENSION FdoExtension, _In_ BOOLEAN Enable)
{
    PSDBUS_K1X_EXTENSION Extension = FdoExtension->HardwareExtension;
    PUCHAR Registers = FdoExtension->RegisterBase;
    USHORT HostControl2;
    ULONG Loops;

    if (!Extension->Hs400EnhancedStrobe || !Extension->PhyModule)
        return STATUS_NOT_SUPPORTED;

    HostControl2 = SdBusReadReg16(FdoExtension, SDHCI_HOST_CONTROL2) & ~SDHCI_HC2_UHS_MODE_MASK;
    if (!Enable)
    {
        SdBusK1xUpdate(Registers, K1X_SDHC_MMC_CTRL, K1X_SDHC_MMC_STROBE | K1X_SDHC_MMC_HS400, 0);
        SdBusK1xUpdate(Registers, K1X_SDHC_PHY_DLLCFG, K1X_SDHC_DLL_ENABLE, 0);
        SdBusWriteReg16(FdoExtension, SDHCI_HOST_CONTROL2, HostControl2);
        return STATUS_SUCCESS;
    }

    SdBusWriteReg16(FdoExtension, SDHCI_HOST_CONTROL2,
                    HostControl2 | SDHCI_HC2_V18_SIGNAL_ENABLE | SDHCI_HC2_UHS_HS400);
    SdBusK1xUpdate(Registers, K1X_SDHC_TX_CFG, K1X_SDHC_TX_INT_CLK, 0);
    SdBusK1xUpdate(Registers, K1X_SDHC_MMC_CTRL, K1X_SDHC_MMC_STROBE | K1X_SDHC_MMC_HS400,
                   K1X_SDHC_MMC_STROBE | K1X_SDHC_MMC_HS400);
    SdBusK1xUpdate(Registers, K1X_SDHC_PHY_DLLCFG, K1X_SDHC_DLL_ENABLE, 0);
    SdBusK1xUpdate(Registers, K1X_SDHC_PHY_DLLCFG, K1X_SDHC_DLL_SETUP, K1X_SDHC_DLL_SETUP);
    SdBusK1xUpdate(Registers, K1X_SDHC_PHY_DLLCFG1, K1X_SDHC_DLL_REG1_MASK, K1X_SDHC_DLL_REG1);
    SdBusK1xUpdate(Registers, K1X_SDHC_PHY_DLLCFG, K1X_SDHC_DLL_ENABLE, K1X_SDHC_DLL_ENABLE);
    for (Loops = 0; Loops < 100; ++Loops)
    {
        if (READ_REGISTER_ULONG((PULONG)(Registers + K1X_SDHC_PHY_DLLSTS)) & K1X_SDHC_DLL_LOCKED)
            return STATUS_SUCCESS;
        KeStallExecutionProcessor(10);
    }
    DPRINT1("SdBusK1xSetHs400EnhancedStrobe: PHY DLL did not lock\n");
    return STATUS_IO_TIMEOUT;
}

static const SDBUS_HARDWARE_OPS SdBusK1xOps =
{
    SdBusK1xRelease,
    SdBusK1xInitializeController,
    NULL,
    NULL,
    NULL,
    NULL,
    SdBusK1xGetSlotCapabilities,
    SdBusK1xGetBaseClock,
    SdBusK1xSetHs400EnhancedStrobe
};

NTSTATUS
SdBusK1xAttach(_In_ PFDO_EXTENSION FdoExtension)
{
    PSDBUS_K1X_EXTENSION Extension;
    UCHAR Value[64];
    HANDLE Key;
    ULONG Length;
    NTSTATUS Status;

    Status = IoOpenDeviceRegistryKey(FdoExtension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_READ, &Key);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Extension), TAG_SDBUS);
    if (!Extension)
    {
        ZwClose(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Header.Ops = &SdBusK1xOps;

    Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    if (SdBusK1xQuery(Key, L"reg", Value, sizeof(Value), &Length) && Length >= 16 &&
        SdBusK1xQuery(Key, L"clk-src-freq", Value + 16, sizeof(Value) - 16, &Length) && Length >= 4)
    {
        Extension->Base = ((ULONG64)SdBusK1xBigEndian(Value) << 32) | SdBusK1xBigEndian(Value + 4);
        Extension->ClockKhz = SdBusK1xBigEndian(Value + 16) / 1000;
        Status = STATUS_SUCCESS;
    }
    if (SdBusK1xQuery(Key, L"sdh-phy-module", Value, sizeof(Value), &Length) && Length >= 4)
        Extension->PhyModule = SdBusK1xBigEndian(Value);
    if (SdBusK1xQuery(Key, L"cd-gpios", Value, sizeof(Value), &Length) && Length >= 8)
    {
        Extension->CardDetectGpio = SdBusK1xBigEndian(Value + 4);
        Extension->HasCardDetect = TRUE;
    }
    Extension->CardDetectInverted = SdBusK1xQuery(Key, L"cd-inverted", Value, sizeof(Value), &Length);
    Extension->NonRemovable = SdBusK1xQuery(Key, L"non-removable", Value, sizeof(Value), &Length);
    Extension->Hs400EnhancedStrobe = SdBusK1xQuery(Key, L"mmc-hs400-enhanced-strobe", Value, sizeof(Value), &Length);
    SdBusK1xFindPowerSequence(Key, Extension);
    ZwClose(Key);

    if (NT_SUCCESS(Status) && !Extension->ClockKhz)
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
    if (NT_SUCCESS(Status))
        Status = SdBusK1xPowerUp(Extension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SdBusK1xAttach: K1 host setup failed 0x%08lx\n", Status);
        ExFreePoolWithTag(Extension, TAG_SDBUS);
        return Status;
    }

    FdoExtension->HardwareExtension = Extension;
    DPRINT("SdBusK1xAttach: host 0x%I64x clock %lu kHz phy %lu cd %lu\n",
           Extension->Base, Extension->ClockKhz, Extension->PhyModule, Extension->CardDetectGpio);
    return STATUS_SUCCESS;
}
