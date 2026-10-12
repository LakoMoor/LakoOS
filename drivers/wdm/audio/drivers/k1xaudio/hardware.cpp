/*
 * PROJECT:         LakoOS SpacemiT K1 Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         SSPA0 I2S, PDMA and ES8326 codec support
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

#define K1X_APBC_BASE                0xD4015000ULL
#define K1X_APBC_SIZE                0x100
#define K1X_APBC_TWSI2               0x038
#define K1X_APBC_SSPA0               0x080
#define K1X_APBC_GATES               0x3
#define K1X_APBC_RESET               0x4
#define K1X_APBC_SSPA0_FROM_I2S      ((7u << 4) | (1u << 3))

#define K1X_APMU_BASE                0xD4282800ULL
#define K1X_APMU_SIZE                0x100
#define K1X_APMU_DMA                 0x064
#define K1X_APMU_DMA_CLOCK           (1u << 3)
#define K1X_APMU_DMA_RELEASE         (1u << 0)

#define K1X_MPMU_BASE                0xD4050000ULL
#define K1X_MPMU_SIZE                0x100
#define K1X_MPMU_SYSCLK_PRE_CTRL     0x008
#define K1X_MPMU_SYSCLK_PRE_I2S      (1u << 29)
#define K1X_MPMU_ISCCR               0x044
#define K1X_MPMU_ISCCR_KEEP          0xA0000000u
#define K1X_MPMU_ISCCR_SYSCLK_EN     (1u << 31)
#define K1X_MPMU_ISCCR_BITCLK_EN     (1u << 29)
#define K1X_MPMU_ISCCR_64FS_48K      ((1u << 30) | (0u << 27) | (326u << 15) | 32600u)

#define K1X_PAD_BASE                 0xD401E000ULL
#define K1X_PAD_SIZE                 0x400
#define K1X_PAD_OFFSET(_Pin)         (((_Pin) + 1) * sizeof(ULONG))
#define K1X_PAD_I2C2                 0xD044u
#define K1X_PAD_SSPA0                0xC043u

#define K1X_GPIO_BASE                0xD4019000ULL
#define K1X_GPIO_SIZE                0x800
#define K1X_GPIO_LEVEL               0x000
#define K1X_GPIO_SET                 0x018
#define K1X_GPIO_CLEAR               0x024
#define K1X_GPIO_SET_OUTPUT          0x054
#define K1X_GPIO_SPEAKER_ENABLE      127

#define SSP_TOP_CTRL                 0x00
#define SSP_FIFO_CTRL                0x04
#define SSP_INT_EN                   0x08
#define SSP_DATAR                    0x10
#define SSP_STATUS                   0x14
#define SSP_PSP_CTRL                 0x18
#define SSP_RWOT_CTRL                0x24
#define SSP_TOP_TRAIL_DMA            (1u << 13)
#define SSP_TOP_DW_32                (0x1Fu << 5)
#define SSP_TOP_FRF_PSP              (3u << 1)
#define SSP_TOP_SSE                  (1u << 0)
#define SSP_FIFO_DMA_REQUESTS        ((1u << 11) | (1u << 10))
#define SSP_FIFO_THRESHOLDS          ((0xFu << 5) | 0xFu)
#define SSP_PSP_I2S                  ((0x10u << 12) | (1u << 4) | (1u << 3))

#define PDMA_DCSR(_Channel)          ((_Channel) * 4)
#define PDMA_DALGN                   0x00A0
#define PDMA_DINT                    0x00F0
#define PDMA_DRCMR(_Request)         ((((_Request) < 64) ? 0x0100 : 0x1100) + (((_Request) & 0x3F) << 2))
#define PDMA_DDADR(_Channel)         (0x0200 + ((_Channel) << 4))
#define PDMA_DSADR(_Channel)         (0x0204 + ((_Channel) << 4))
#define PDMA_DCSR_RUN                (1u << 31)
#define PDMA_DCSR_STOPSTATE          (1u << 3)
#define PDMA_DCSR_ENDINTR            (1u << 2)
#define PDMA_DCSR_BUSERR             (1u << 0)
#define PDMA_DRCMR_MAPVLD            (1u << 7)
#define PDMA_DTADR(_Channel)         (0x0208 + ((_Channel) << 4))
#define PDMA_DCMD_INCSRCADDR         (1u << 31)
#define PDMA_DCMD_INCTRGADDR         (1u << 30)
#define PDMA_DCMD_FLOWSRC            (1u << 29)
#define PDMA_DCMD_FLOWTRG            (1u << 28)
#define PDMA_DCMD_ENDIRQEN           (1u << 21)
#define PDMA_DCMD_BURST32            (3u << 16)
#define PDMA_DCMD_WIDTH4             (3u << 14)
#define PDMA_MAX_LENGTH              0x1FF0u
#define PDMA_CHANNEL_RENDER          0
#define PDMA_CHANNEL_CAPTURE         1
#define PDMA_REQUEST_SSPA0_TX        21
#define PDMA_REQUEST_SSPA0_RX        22

#define TWSI_CONTROL                 0x00
#define TWSI_STATUS                  0x04
#define TWSI_DATA                    0x0C
#define TWSI_CR_START                (1u << 0)
#define TWSI_CR_STOP                 (1u << 1)
#define TWSI_CR_NAK                  (1u << 2)
#define TWSI_CR_BYTE                 (1u << 3)
#define TWSI_CR_FIFO                 (1u << 5)
#define TWSI_CR_DMA                  (1u << 7)
#define TWSI_CR_MASTER_ABORT         (1u << 12)
#define TWSI_CR_CLOCK                (1u << 13)
#define TWSI_CR_UNIT                 (1u << 14)
#define TWSI_CR_INTERRUPTS           0xFFFC0000u
#define TWSI_SR_NAK                  (1u << 14)
#define TWSI_SR_UNIT_BUSY            (1u << 15)
#define TWSI_SR_BUS_BUSY             (1u << 16)
#define TWSI_SR_LOST                 (1u << 18)
#define TWSI_SR_TX_EMPTY             (1u << 19)
#define TWSI_SR_RX_FULL              (1u << 20)
#define TWSI_SR_BUS_ERROR            (1u << 22)
#define TWSI_SR_EVENTS               0xFFFC0000u

#define ES8326_ADDRESS               0x19
#define ES8326_RESET                 0x00
#define ES8326_CLK_CTL               0x01
#define ES8326_CLK_INV               0x02
#define ES8326_CLK_RESAMPLE          0x03
#define ES8326_CLK_DIV1              0x04
#define ES8326_CLK_DIV2              0x05
#define ES8326_CLK_DLL               0x06
#define ES8326_CLK_MUX               0x07
#define ES8326_CLK_ADC_SEL           0x08
#define ES8326_CLK_DAC_SEL           0x09
#define ES8326_CLK_ADC_OSR           0x0A
#define ES8326_CLK_DAC_OSR           0x0B
#define ES8326_CLK_DIV_CPC           0x0C
#define ES8326_CLK_TRI               0x0E
#define ES8326_CLK_VMIDS1            0x10
#define ES8326_CLK_VMIDS2            0x11
#define ES8326_CLK_CAL_TIME          0x12
#define ES8326_FMT                   0x13
#define ES8326_DAC_MUTE              0x14
#define ES8326_ADC_MUTE              0x15
#define ES8326_ANA_PDN               0x16
#define ES8326_PGA_PDN               0x17
#define ES8326_PGAGAIN               0x23
#define ES8326_ADC1_SRC              0x2A
#define ES8326_ADC2_SRC              0x2B
#define ES8326_VMIDSEL               0x18
#define ES8326_ANA_LP                0x19
#define ES8326_ANA_MICBIAS           0x1B
#define ES8326_ANA_VSEL              0x1C
#define ES8326_VMIDLOW               0x22
#define ES8326_HP_DRIVER             0x24
#define ES8326_DAC2HPMIX             0x25
#define ES8326_HP_VOL                0x26
#define ES8326_HP_CAL                0x27
#define ES8326_HP_DRIVER_REF         0x28
#define ES8326_HP_OFFSET_CAL         0x4A
#define ES8326_HPL_OFFSET_INI        0x4B
#define ES8326_HPR_OFFSET_INI        0x4C
#define ES8326_DAC_DSM               0x4D
#define ES8326_DAC_RAMPRATE          0x4E
#define ES8326_DAC_VPPSCALE          0x4F
#define ES8326_DAC_VOL               0x50
#define ES8326_DAC_CROSSTALK         0x55
#define ES8326_HPJACK_TIMER          0x56
#define ES8326_HPDET_TYPE            0x57
#define ES8326_INTOUT_IO             0x59
#define ES8326_SDINOUT1_IO           0x5A
#define ES8326_SDINOUT23_IO          0x5B
#define ES8326_HP_MISC               0xF7
#define ES8326_PULLUP_CTL            0xF9
#define ES8326_CSM_MUTE_STA          0xFC
#define ES8326_CHIP_ID1              0xFD
#define ES8326_CHIP_ID2              0xFE
#define ES8326_CHIP_VERSION          0xFF
#define ES8326_VERSION_B             3
#define ES8326_DAC_VOL_0DB           0xBF

static ULONG
ReadRegister(PUCHAR Base, ULONG Offset)
{
    return READ_REGISTER_ULONG(reinterpret_cast<PULONG>(Base + Offset));
}

static VOID
WriteRegister(PUCHAR Base, ULONG Offset, ULONG Value)
{
    WRITE_REGISTER_ULONG(reinterpret_cast<PULONG>(Base + Offset), Value);
}

static PUCHAR
MapBlock(ULONGLONG Base, SIZE_T Size)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = static_cast<LONGLONG>(Base);
    return static_cast<PUCHAR>(MmMapIoSpace(Address, Size, MmNonCached));
}

static VOID
SleepMilliseconds(ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -static_cast<LONGLONG>(Milliseconds) * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

CK1xAudioAdapter::CK1xAudioAdapter()
    : m_DeviceObject(NULL),
      m_I2sRegisters(NULL),
      m_I2sRegistersLength(0),
      m_DmaRegisters(NULL),
      m_DmaRegistersLength(0),
      m_I2cRegisters(NULL),
      m_I2cRegistersLength(0),
      m_Apbc(NULL),
      m_Apmu(NULL),
      m_Mpmu(NULL),
      m_Pads(NULL),
      m_Gpio(NULL),
      m_CodecReady(FALSE),
      m_CodecVersion(0),
      m_InterruptSync(NULL),
      m_Shutdown(0),
      m_JackPortEvents(NULL),
      m_Mute(FALSE),
      m_SyncDirection(NULL)
{
    for (ULONG Channel = 0; Channel < K1XAUDIO_CHANNELS; ++Channel)
        m_VolumeLevel[Channel] = K1XAUDIO_VOLUME_MAXIMUM;
    m_I2sPhysicalAddress.QuadPart = 0;
    m_Directions[0].Capture = FALSE;
    m_Directions[0].Channel = PDMA_CHANNEL_RENDER;
    m_Directions[0].Request = PDMA_REQUEST_SSPA0_TX;
    m_Directions[1].Capture = TRUE;
    m_Directions[1].Channel = PDMA_CHANNEL_CAPTURE;
    m_Directions[1].Request = PDMA_REQUEST_SSPA0_RX;
    KeInitializeDpc(&m_Dpc, DpcRoutine, this);
    KeInitializeSpinLock(&m_EventLock);
    KeInitializeSpinLock(&m_I2cLock);
}

CK1xAudioAdapter::~CK1xAudioAdapter()
{
    Shutdown();
    UnmapResources();
}

STDMETHODIMP
CK1xAudioAdapter::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;
    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown))
    {
        *Interface = static_cast<PUNKNOWN>(this);
        AddRef();
        return STATUS_SUCCESS;
    }
    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
CK1xAudioAdapter::MapResources(PRESOURCELIST ResourceList)
{
    PUCHAR *Mappings[3] = { &m_I2sRegisters, &m_DmaRegisters, &m_I2cRegisters };
    PULONG Lengths[3] = { &m_I2sRegistersLength, &m_DmaRegistersLength, &m_I2cRegistersLength };

    if (ResourceList->NumberOfMemories() < RTL_NUMBER_OF(Mappings) ||
        ResourceList->NumberOfInterrupts() < 1)
    {
        DbgPrint("K1XAUDIO: %lu memory and %lu interrupt resources\n",
                 ResourceList->NumberOfMemories(), ResourceList->NumberOfInterrupts());
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (ULONG Index = 0; Index < RTL_NUMBER_OF(Mappings); ++Index)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = ResourceList->FindTranslatedMemory(Index);

        if (!Descriptor || !Descriptor->u.Memory.Length)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (Index == 0)
            m_I2sPhysicalAddress = Descriptor->u.Memory.Start;
        *Mappings[Index] = static_cast<PUCHAR>(MmMapIoSpace(Descriptor->u.Memory.Start,
                                                             Descriptor->u.Memory.Length,
                                                             MmNonCached));
        if (!*Mappings[Index])
            return STATUS_INSUFFICIENT_RESOURCES;
        *Lengths[Index] = Descriptor->u.Memory.Length;
    }

    m_Apbc = MapBlock(K1X_APBC_BASE, K1X_APBC_SIZE);
    m_Apmu = MapBlock(K1X_APMU_BASE, K1X_APMU_SIZE);
    m_Mpmu = MapBlock(K1X_MPMU_BASE, K1X_MPMU_SIZE);
    m_Pads = MapBlock(K1X_PAD_BASE, K1X_PAD_SIZE);
    m_Gpio = MapBlock(K1X_GPIO_BASE, K1X_GPIO_SIZE);
    if (!m_Apbc || !m_Apmu || !m_Mpmu || !m_Pads || !m_Gpio)
        return STATUS_INSUFFICIENT_RESOURCES;
    return STATUS_SUCCESS;
}

VOID
CK1xAudioAdapter::UnmapResources()
{
    if (m_I2sRegisters)
        MmUnmapIoSpace(m_I2sRegisters, m_I2sRegistersLength);
    if (m_DmaRegisters)
        MmUnmapIoSpace(m_DmaRegisters, m_DmaRegistersLength);
    if (m_I2cRegisters)
        MmUnmapIoSpace(m_I2cRegisters, m_I2cRegistersLength);
    if (m_Apbc)
        MmUnmapIoSpace(m_Apbc, K1X_APBC_SIZE);
    if (m_Apmu)
        MmUnmapIoSpace(m_Apmu, K1X_APMU_SIZE);
    if (m_Mpmu)
        MmUnmapIoSpace(m_Mpmu, K1X_MPMU_SIZE);
    if (m_Pads)
        MmUnmapIoSpace(m_Pads, K1X_PAD_SIZE);
    if (m_Gpio)
        MmUnmapIoSpace(m_Gpio, K1X_GPIO_SIZE);
    m_I2sRegisters = m_DmaRegisters = m_I2cRegisters = NULL;
    m_Apbc = m_Apmu = m_Mpmu = m_Pads = m_Gpio = NULL;
}

VOID
CK1xAudioAdapter::ConfigurePads()
{
    static const ULONG I2cPins[] = { 84, 85 };
    static const ULONG I2sPins[] = { 137, 138, 139, 140, 141 };

    for (ULONG Index = 0; Index < RTL_NUMBER_OF(I2cPins); ++Index)
        WriteRegister(m_Pads, K1X_PAD_OFFSET(I2cPins[Index]), K1X_PAD_I2C2);
    for (ULONG Index = 0; Index < RTL_NUMBER_OF(I2sPins); ++Index)
        WriteRegister(m_Pads, K1X_PAD_OFFSET(I2sPins[Index]), K1X_PAD_SSPA0);
}

VOID
CK1xAudioAdapter::EnableClocks()
{
    ULONG Value;

    WriteRegister(m_Apmu, K1X_APMU_DMA,
                  ReadRegister(m_Apmu, K1X_APMU_DMA) | K1X_APMU_DMA_CLOCK | K1X_APMU_DMA_RELEASE);

    WriteRegister(m_Apbc, K1X_APBC_TWSI2, K1X_APBC_GATES | K1X_APBC_RESET);
    KeStallExecutionProcessor(10);
    WriteRegister(m_Apbc, K1X_APBC_TWSI2, K1X_APBC_GATES);

    Value = ReadRegister(m_Mpmu, K1X_MPMU_ISCCR) & K1X_MPMU_ISCCR_KEEP;
    WriteRegister(m_Mpmu, K1X_MPMU_ISCCR,
                  Value | K1X_MPMU_ISCCR_SYSCLK_EN | K1X_MPMU_ISCCR_BITCLK_EN | K1X_MPMU_ISCCR_64FS_48K);
    WriteRegister(m_Mpmu, K1X_MPMU_SYSCLK_PRE_CTRL,
                  ReadRegister(m_Mpmu, K1X_MPMU_SYSCLK_PRE_CTRL) | K1X_MPMU_SYSCLK_PRE_I2S);

    WriteRegister(m_Apbc, K1X_APBC_SSPA0, K1X_APBC_SSPA0_FROM_I2S | K1X_APBC_GATES | K1X_APBC_RESET);
    KeStallExecutionProcessor(10);
    WriteRegister(m_Apbc, K1X_APBC_SSPA0, K1X_APBC_SSPA0_FROM_I2S | K1X_APBC_GATES);
}

BOOLEAN
CK1xAudioAdapter::ReadGpio(ULONG Number)
{
    static const ULONG Banks[] = { 0x000, 0x004, 0x008, 0x100 };

    return (ReadRegister(m_Gpio, Banks[Number / 32] + K1X_GPIO_LEVEL) & (1u << (Number % 32))) != 0;
}

VOID
CK1xAudioAdapter::WriteGpio(ULONG Number, BOOLEAN High)
{
    static const ULONG Banks[] = { 0x000, 0x004, 0x008, 0x100 };
    ULONG Bank = Banks[Number / 32];
    ULONG Bit = 1u << (Number % 32);

    WriteRegister(m_Gpio, Bank + (High ? K1X_GPIO_SET : K1X_GPIO_CLEAR), Bit);
    WriteRegister(m_Gpio, Bank + K1X_GPIO_SET_OUTPUT, Bit);
}

static ULONG
TwsiControlBase(PUCHAR Registers)
{
    return (ReadRegister(Registers, TWSI_CONTROL) &
            ~(TWSI_CR_START | TWSI_CR_STOP | TWSI_CR_NAK | TWSI_CR_BYTE | TWSI_CR_FIFO | TWSI_CR_DMA |
              TWSI_CR_MASTER_ABORT | TWSI_CR_INTERRUPTS)) |
           TWSI_CR_UNIT | TWSI_CR_CLOCK;
}

static NTSTATUS
TwsiTransferByte(PUCHAR Registers, ULONG Flags, ULONG Event, PUCHAR Byte)
{
    ULONG Loops = 20000, Status;

    if (Event == TWSI_SR_TX_EMPTY)
        WriteRegister(Registers, TWSI_DATA, *Byte);
    WriteRegister(Registers, TWSI_CONTROL, TwsiControlBase(Registers) | Flags | TWSI_CR_BYTE);
    while (Loops--)
    {
        Status = ReadRegister(Registers, TWSI_STATUS);
        if (Status & (TWSI_SR_LOST | TWSI_SR_BUS_ERROR))
        {
            WriteRegister(Registers, TWSI_STATUS, Status & TWSI_SR_EVENTS);
            WriteRegister(Registers, TWSI_CONTROL, TwsiControlBase(Registers) | TWSI_CR_MASTER_ABORT);
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }
        if (Status & Event)
        {
            WriteRegister(Registers, TWSI_STATUS, Event);
            if (Event == TWSI_SR_RX_FULL)
                *Byte = static_cast<UCHAR>(ReadRegister(Registers, TWSI_DATA));
            else if ((Status & TWSI_SR_NAK) && !(Flags & TWSI_CR_STOP))
                return STATUS_DEVICE_PROTOCOL_ERROR;
            return STATUS_SUCCESS;
        }
        KeStallExecutionProcessor(5);
    }
    WriteRegister(Registers, TWSI_CONTROL, TwsiControlBase(Registers) | TWSI_CR_MASTER_ABORT);
    return STATUS_IO_TIMEOUT;
}

NTSTATUS
CK1xAudioAdapter::I2cAccess(UCHAR Register, PUCHAR Data, ULONG Count, BOOLEAN Write)
{
    ULONG Loops = 20000;
    KIRQL OldIrql;
    UCHAR Byte;
    NTSTATUS Status = STATUS_IO_TIMEOUT;

    KeAcquireSpinLock(&m_I2cLock, &OldIrql);
    while (Loops--)
    {
        if (!(ReadRegister(m_I2cRegisters, TWSI_STATUS) & (TWSI_SR_UNIT_BUSY | TWSI_SR_BUS_BUSY)))
        {
            Status = STATUS_SUCCESS;
            break;
        }
        KeStallExecutionProcessor(5);
    }
    if (NT_SUCCESS(Status))
    {
        WriteRegister(m_I2cRegisters, TWSI_STATUS, TWSI_SR_EVENTS);
        Byte = ES8326_ADDRESS << 1;
        Status = TwsiTransferByte(m_I2cRegisters, TWSI_CR_START, TWSI_SR_TX_EMPTY, &Byte);
    }
    if (NT_SUCCESS(Status))
    {
        Byte = Register;
        Status = TwsiTransferByte(m_I2cRegisters, 0, TWSI_SR_TX_EMPTY, &Byte);
    }
    if (NT_SUCCESS(Status) && Write)
    {
        for (ULONG Index = 0; NT_SUCCESS(Status) && Index < Count; ++Index)
        {
            Byte = Data[Index];
            Status = TwsiTransferByte(m_I2cRegisters, (Index + 1 == Count) ? TWSI_CR_STOP : 0,
                                      TWSI_SR_TX_EMPTY, &Byte);
        }
    }
    else if (NT_SUCCESS(Status))
    {
        Byte = (ES8326_ADDRESS << 1) | 1;
        Status = TwsiTransferByte(m_I2cRegisters, TWSI_CR_START, TWSI_SR_TX_EMPTY, &Byte);
        for (ULONG Index = 0; NT_SUCCESS(Status) && Index < Count; ++Index)
        {
            Status = TwsiTransferByte(m_I2cRegisters,
                                      (Index + 1 == Count) ? (TWSI_CR_STOP | TWSI_CR_NAK) : 0,
                                      TWSI_SR_RX_FULL, &Data[Index]);
        }
    }
    KeReleaseSpinLock(&m_I2cLock, OldIrql);
    return Status;
}

NTSTATUS
CK1xAudioAdapter::CodecWrite(UCHAR Register, UCHAR Value)
{
    return I2cAccess(Register, &Value, 1, TRUE);
}

NTSTATUS
CK1xAudioAdapter::CodecRead(UCHAR Register, PUCHAR Value)
{
    return I2cAccess(Register, Value, 1, FALSE);
}

NTSTATUS
CK1xAudioAdapter::CodecInitialize()
{
    static const UCHAR ClockV0[8] = { 0xE0, 0x00, 0x03, 0x2D, 0x4A, 0x0A, 0x1F, 0x1F };
    static const UCHAR ClockV3[8] = { 0xE0, 0x00, 0x31, 0x2D, 0xCA, 0x0A, 0x1F, 0x1F };
    UCHAR Id1 = 0, Id2 = 0, Version = 0, OffsetLeft = 0, OffsetRight = 0, Value = 0;
    const UCHAR *Clock;
    NTSTATUS Status;

    Status = CodecRead(ES8326_CHIP_ID1, &Id1);
    if (NT_SUCCESS(Status))
        Status = CodecRead(ES8326_CHIP_ID2, &Id2);
    if (NT_SUCCESS(Status))
        Status = CodecRead(ES8326_CHIP_VERSION, &Version);
    DbgPrint("K1XAUDIO: ES8326 id %02x%02x version %u status 0x%08lx\n", Id1, Id2, Version, Status);
    if (!NT_SUCCESS(Status))
        return Status;
    m_CodecVersion = Version;

    CodecWrite(ES8326_RESET, 0x1F);
    CodecWrite(ES8326_VMIDSEL, 0x0E);
    CodecWrite(ES8326_ANA_LP, 0xF0);
    SleepMilliseconds(15);
    CodecWrite(ES8326_HPJACK_TIMER, 0xD9);
    CodecWrite(ES8326_ANA_MICBIAS, 0xD8);
    CodecWrite(ES8326_HPDET_TYPE, 0x83);
    CodecWrite(ES8326_CLK_RESAMPLE, 0x05);
    CodecWrite(ES8326_CLK_DIV_CPC, 0x89);
    CodecWrite(ES8326_CLK_CTL, 0x7E);
    CodecWrite(ES8326_RESET, 0x17);
    CodecWrite(ES8326_HP_MISC, 0x3D);
    CodecWrite(ES8326_PULLUP_CTL, 0x00);
    CodecWrite(ES8326_HP_VOL, 0xC4);
    CodecWrite(ES8326_HP_DRIVER, 0xA7);
    SleepMilliseconds(5);
    CodecWrite(ES8326_HP_DRIVER_REF, 0x23);
    CodecWrite(ES8326_HP_DRIVER_REF, 0x33);
    CodecWrite(ES8326_HP_DRIVER, 0xA1);
    CodecWrite(ES8326_CLK_INV, 0x00);
    CodecWrite(ES8326_CLK_VMIDS1, 0xC4);
    CodecWrite(ES8326_CLK_VMIDS2, 0x81);
    CodecWrite(ES8326_CLK_CAL_TIME, 0x00);

    if (Version == ES8326_VERSION_B)
    {
        CodecWrite(ES8326_CLK_INV, 0xC0);
        CodecWrite(ES8326_CLK_DIV1, 0x03);
        CodecWrite(ES8326_CLK_DLL, 0x30);
        CodecWrite(ES8326_CLK_MUX, 0xED);
        CodecWrite(ES8326_CLK_DAC_SEL, 0x08);
        CodecWrite(ES8326_CLK_TRI, 0xC1);
        CodecWrite(ES8326_DAC_MUTE, 0x03);
        CodecWrite(ES8326_ANA_VSEL, 0x7F);
        CodecWrite(ES8326_VMIDLOW, 0x23);
        CodecWrite(ES8326_DAC2HPMIX, 0x88);
        SleepMilliseconds(20);
        CodecWrite(ES8326_HP_OFFSET_CAL, 0x8C);
        SleepMilliseconds(20);
        CodecWrite(ES8326_RESET, 0xC0);
        SleepMilliseconds(20);
        CodecWrite(ES8326_HP_OFFSET_CAL, 0x00);
        CodecRead(ES8326_CSM_MUTE_STA, &Value);
        if ((Value & 0xF0) != 0x40)
            SleepMilliseconds(50);
        CodecWrite(ES8326_HP_CAL, 0xD4);
        SleepMilliseconds(200);
        CodecWrite(ES8326_HP_CAL, 0x4D);
        SleepMilliseconds(200);
        CodecWrite(ES8326_HP_CAL, 0x00);
        CodecRead(ES8326_HPL_OFFSET_INI, &OffsetLeft);
        CodecRead(ES8326_HPR_OFFSET_INI, &OffsetRight);
        CodecWrite(ES8326_HP_OFFSET_CAL, 0x8C);
        CodecWrite(ES8326_HPL_OFFSET_INI, OffsetLeft);
        CodecWrite(ES8326_HPR_OFFSET_INI, OffsetRight);
        CodecWrite(ES8326_CLK_INV, 0x00);
    }

    CodecWrite(ES8326_DAC_CROSSTALK, 0xAA);
    CodecWrite(ES8326_DAC_RAMPRATE, 0x00);
    CodecWrite(ES8326_HP_CAL, 0x00);
    CodecWrite(ES8326_ANA_LP, 0xF0);
    CodecWrite(ES8326_ANA_VSEL, 0x7F);
    CodecWrite(ES8326_VMIDLOW, 0x03);
    CodecWrite(ES8326_DAC_DSM, 0x08);
    CodecWrite(ES8326_DAC_VPPSCALE, 0x15);
    CodecWrite(ES8326_HPDET_TYPE, (Version == ES8326_VERSION_B) ? 0x91 : 0x95);
    SleepMilliseconds(10);
    CodecWrite(ES8326_INTOUT_IO, 0x00);
    CodecWrite(ES8326_SDINOUT1_IO, 0x90);
    CodecWrite(ES8326_SDINOUT23_IO, 0x00);
    CodecWrite(ES8326_ANA_PDN, 0x00);
    CodecWrite(ES8326_RESET, 0x80);
    CodecWrite(ES8326_DAC_MUTE, 0x03);
    CodecWrite(ES8326_ADC_MUTE, 0x0F);
    CodecWrite(ES8326_ADC1_SRC, 0x44);
    CodecWrite(ES8326_ADC2_SRC, 0x66);
    CodecWrite(ES8326_FMT, 0x0C);

    Clock = (Version == 0) ? ClockV0 : ClockV3;
    for (UCHAR Index = 0; Index < 8; ++Index)
        CodecWrite(ES8326_CLK_DIV1 + Index, Clock[Index]);
    CodecWrite(ES8326_DAC2HPMIX, 0x88);
    CodecApplyVolume();

    CodecRead(ES8326_DAC_VOL, &Value);
    DbgPrint("K1XAUDIO: ES8326 initialised, DAC volume register 0x%02x\n", Value);
    m_CodecReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
CK1xAudioAdapter::CodecApplyVolume()
{
    LONG Level = max(m_VolumeLevel[0], m_VolumeLevel[1]);
    LONG Steps = (Level - K1XAUDIO_VOLUME_MINIMUM) / K1XAUDIO_VOLUME_STEP;

    CodecWrite(ES8326_DAC_VOL, static_cast<UCHAR>(min(max(Steps, 0), ES8326_DAC_VOL_0DB)));
}

VOID
CK1xAudioAdapter::CodecSetOutput(BOOLEAN Enable)
{
    if (!m_CodecReady)
        return;

    if (Enable)
    {
        CodecWrite(ES8326_RESET, 0x82);
        SleepMilliseconds(5);
        CodecWrite(ES8326_PGA_PDN, 0x40);
        CodecWrite(ES8326_ANA_PDN, 0x00);
        CodecWrite(ES8326_CLK_CTL, 0x7E);
        CodecWrite(ES8326_RESET, 0x80);
        CodecWrite(ES8326_DAC_DSM, 0x09);
        SleepMilliseconds(2);
        CodecWrite(ES8326_DAC_DSM, 0x08);
        SleepMilliseconds(2);
        CodecWrite(ES8326_HP_DRIVER_REF, 0x23);
        CodecWrite(ES8326_HP_DRIVER_REF, 0x33);
        CodecWrite(ES8326_HP_DRIVER, 0xA1);
        CodecWrite(ES8326_HP_CAL, 0x77);
        CodecWrite(ES8326_DAC_MUTE, m_Mute ? 0x03 : 0x00);
        WriteGpio(K1X_GPIO_SPEAKER_ENABLE, TRUE);
    }
    else
    {
        WriteGpio(K1X_GPIO_SPEAKER_ENABLE, FALSE);
        CodecWrite(ES8326_HP_CAL, 0x00);
        CodecWrite(ES8326_DAC_MUTE, 0x03);
        CodecWrite(ES8326_HP_DRIVER_REF, 0x03);
    }
}

VOID
CK1xAudioAdapter::CodecSetInput(BOOLEAN Enable)
{
    if (!m_CodecReady)
        return;
    if (Enable)
    {
        CodecWrite(ES8326_ANA_MICBIAS, 0xDC);
        CodecWrite(ES8326_PGA_PDN, 0x40);
        CodecWrite(ES8326_ANA_PDN, 0x00);
        SleepMilliseconds(300);
        CodecWrite(ES8326_ADC_MUTE, 0x00);
    }
    else
    {
        CodecWrite(ES8326_ADC_MUTE, 0x0F);
    }
}

VOID
CK1xAudioAdapter::ConfigureI2s()
{
    WriteRegister(m_I2sRegisters, SSP_TOP_CTRL, SSP_TOP_TRAIL_DMA | SSP_TOP_DW_32 | SSP_TOP_FRF_PSP);
    WriteRegister(m_I2sRegisters, SSP_FIFO_CTRL, SSP_FIFO_DMA_REQUESTS | SSP_FIFO_THRESHOLDS);
    WriteRegister(m_I2sRegisters, SSP_INT_EN, 0);
    WriteRegister(m_I2sRegisters, SSP_PSP_CTRL, SSP_PSP_I2S);
    WriteRegister(m_I2sRegisters, SSP_RWOT_CTRL, ReadRegister(m_I2sRegisters, SSP_RWOT_CTRL) | 1);
}

NTSTATUS
CK1xAudioAdapter::Initialize(PDEVICE_OBJECT DeviceObject, PRESOURCELIST ResourceList)
{
    NTSTATUS Status;

    if (!DeviceObject || !ResourceList)
        return STATUS_INVALID_PARAMETER;

    m_DeviceObject = DeviceObject;
    Status = MapResources(ResourceList);
    if (!NT_SUCCESS(Status))
        return Status;

    ConfigurePads();
    EnableClocks();
    ConfigureI2s();
    ResetDma(&m_Directions[0]);
    ResetDma(&m_Directions[1]);

    Status = CodecInitialize();
    if (!NT_SUCCESS(Status))
        DbgPrint("K1XAUDIO: codec unavailable 0x%08lx, digital path only\n", Status);

    Status = PcNewInterruptSync(&m_InterruptSync, NULL, ResourceList, 0, InterruptSyncModeNormal);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = m_InterruptSync->RegisterServiceRoutine(InterruptService, this, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    return m_InterruptSync->Connect();
}

BOOLEAN
CK1xAudioAdapter::ClaimStream(BOOLEAN Capture)
{
    return InterlockedCompareExchange(&m_Directions[Capture ? 1 : 0].Open, 1, 0) == 0;
}

VOID
CK1xAudioAdapter::ReleaseStream(BOOLEAN Capture)
{
    InterlockedExchange(&m_Directions[Capture ? 1 : 0].Open, 0);
}

NTSTATUS
CK1xAudioAdapter::GetVolume(ULONG Channel, PLONG Level)
{
    if (Channel >= K1XAUDIO_CHANNELS || !Level)
        return STATUS_INVALID_PARAMETER;
    *Level = InterlockedCompareExchange(&m_VolumeLevel[Channel], 0, 0);
    return STATUS_SUCCESS;
}

NTSTATUS
CK1xAudioAdapter::SetVolume(ULONG Channel, LONG Level)
{
    if (Channel >= K1XAUDIO_CHANNELS)
        return STATUS_INVALID_PARAMETER;
    Level = min(max(Level, K1XAUDIO_VOLUME_MINIMUM), K1XAUDIO_VOLUME_MAXIMUM);
    Level = K1XAUDIO_VOLUME_MINIMUM +
            ((Level - K1XAUDIO_VOLUME_MINIMUM) / K1XAUDIO_VOLUME_STEP) * K1XAUDIO_VOLUME_STEP;
    InterlockedExchange(&m_VolumeLevel[Channel], Level);
    if (m_CodecReady)
        CodecApplyVolume();
    return STATUS_SUCCESS;
}

BOOLEAN
CK1xAudioAdapter::GetMute()
{
    return InterlockedCompareExchange(&m_Mute, 0, 0) != 0;
}

VOID
CK1xAudioAdapter::SetMute(BOOLEAN Mute)
{
    InterlockedExchange(&m_Mute, Mute ? TRUE : FALSE);
    if (m_CodecReady && InterlockedCompareExchange(&m_Directions[0].Running, 1, 1))
        CodecWrite(ES8326_DAC_MUTE, Mute ? 0x03 : 0x00);
}

BOOLEAN
CK1xAudioAdapter::IsSinkConnected()
{
    return TRUE;
}

NTSTATUS
CK1xAudioAdapter::AllocateBuffer(
    BOOLEAN Capture,
    PPORTWAVERTSTREAM PortStream,
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];
    PHYSICAL_ADDRESS LowAddress, HighAddress, BoundaryAddress;
    NTSTATUS Status;

    if (!PortStream || !AudioBufferMdl || !ActualSize || !OffsetFromFirstPage || !CacheType)
        return STATUS_INVALID_PARAMETER;
    if (Direction->Mdl)
        return STATUS_DEVICE_BUSY;
    if (NotificationCount < 2 || NotificationCount > 64 ||
        RequestedSize < NotificationCount * K1XAUDIO_BLOCK_ALIGN ||
        RequestedSize > K1XAUDIO_MAX_BUFFER_SIZE ||
        RequestedSize % NotificationCount != 0 ||
        (RequestedSize / NotificationCount) % K1XAUDIO_BLOCK_ALIGN != 0)
    {
        DbgPrint("K1XAUDIO: rejected buffer of %lu bytes in %lu notifications\n", RequestedSize, NotificationCount);
        return STATUS_INVALID_PARAMETER;
    }

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = 0x7FFFFFFFULL;
    BoundaryAddress.QuadPart = 0;
    Direction->PeriodBytes = RequestedSize / NotificationCount;
    Direction->BufferSize = RequestedSize;
    Direction->NotificationCount = NotificationCount;
    Direction->Mdl = PortStream->AllocatePagesForMdl(HighAddress, RequestedSize);
    if (Direction->Mdl)
        Direction->Buffer = PortStream->MapAllocatedPages(Direction->Mdl, MmNonCached);
    Direction->DescriptorsSize = K1XAUDIO_MAX_DESCRIPTORS * sizeof(K1XAUDIO_PDMA_DESCRIPTOR);
    Direction->Descriptors = static_cast<PK1XAUDIO_PDMA_DESCRIPTOR>(MmAllocateContiguousMemorySpecifyCache(
        Direction->DescriptorsSize, LowAddress, HighAddress, BoundaryAddress, MmNonCached));
    Status = (Direction->Mdl && Direction->Buffer && Direction->Descriptors) ?
             BuildDescriptors(Direction) : STATUS_INSUFFICIENT_RESOURCES;
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("K1XAUDIO: buffer of %lu bytes unavailable 0x%08lx\n", RequestedSize, Status);
        if (Direction->Descriptors)
            MmFreeContiguousMemorySpecifyCache(Direction->Descriptors, Direction->DescriptorsSize, MmNonCached);
        if (Direction->Buffer)
            PortStream->UnmapAllocatedPages(Direction->Buffer, Direction->Mdl);
        if (Direction->Mdl)
            PortStream->FreePagesFromMdl(Direction->Mdl);
        Direction->Descriptors = NULL;
        Direction->Buffer = NULL;
        Direction->Mdl = NULL;
        Direction->BufferSize = 0;
        Direction->NotificationCount = 0;
        Direction->PeriodBytes = 0;
        Direction->SegmentCount = 0;
        return Status;
    }

    RtlZeroMemory(Direction->Buffer, RequestedSize);
    *AudioBufferMdl = Direction->Mdl;
    *ActualSize = RequestedSize;
    *OffsetFromFirstPage = 0;
    *CacheType = MmNonCached;
    return STATUS_SUCCESS;
}

VOID
CK1xAudioAdapter::FreeBuffer(BOOLEAN Capture, PPORTWAVERTSTREAM PortStream, PMDL AudioBufferMdl)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];

    Stop(Capture);
    if (!PortStream || !Direction->Mdl || AudioBufferMdl != Direction->Mdl)
        return;

    MmFreeContiguousMemorySpecifyCache(Direction->Descriptors, Direction->DescriptorsSize, MmNonCached);
    PortStream->UnmapAllocatedPages(Direction->Buffer, Direction->Mdl);
    PortStream->FreePagesFromMdl(Direction->Mdl);
    Direction->Descriptors = NULL;
    Direction->DescriptorsSize = 0;
    Direction->Buffer = NULL;
    Direction->Mdl = NULL;
    Direction->BufferSize = 0;
    Direction->NotificationCount = 0;
    Direction->PeriodBytes = 0;
    Direction->SegmentCount = 0;
}

NTSTATUS
CK1xAudioAdapter::BuildDescriptors(PK1XAUDIO_DIRECTION Direction)
{
    PPFN_NUMBER Pages = MmGetMdlPfnArray(Direction->Mdl);
    ULONG Fifo = static_cast<ULONG>(m_I2sPhysicalAddress.QuadPart) + SSP_DATAR;
    ULONG Offset = 0, Count = 0;

    if (MmGetMdlByteCount(Direction->Mdl) < Direction->BufferSize || MmGetMdlByteOffset(Direction->Mdl))
        return STATUS_INSUFFICIENT_RESOURCES;
    while (Offset < Direction->BufferSize)
    {
        ULONG64 Physical = (static_cast<ULONG64>(Pages[Offset >> PAGE_SHIFT]) << PAGE_SHIFT) + (Offset & (PAGE_SIZE - 1));
        ULONG PeriodEnd = (Offset / Direction->PeriodBytes + 1) * Direction->PeriodBytes;
        ULONG Length = min(PeriodEnd, (Offset & ~(PAGE_SIZE - 1)) + PAGE_SIZE) - Offset;
        ULONG Command = PDMA_DCMD_BURST32 | PDMA_DCMD_WIDTH4 | Length |
                        ((Offset + Length == PeriodEnd) ? PDMA_DCMD_ENDIRQEN : 0);

        if (Count == K1XAUDIO_MAX_DESCRIPTORS || Physical + Length > 0x80000000ULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        Direction->Segments[Count].Physical = static_cast<ULONG>(Physical);
        Direction->Segments[Count].Offset = Offset;
        Direction->Segments[Count].Length = Length;
        if (Direction->Capture)
        {
            Direction->Descriptors[Count].SourceAddress = Fifo;
            Direction->Descriptors[Count].TargetAddress = static_cast<ULONG>(Physical);
            Direction->Descriptors[Count].Command = Command | PDMA_DCMD_INCTRGADDR | PDMA_DCMD_FLOWSRC;
        }
        else
        {
            Direction->Descriptors[Count].SourceAddress = static_cast<ULONG>(Physical);
            Direction->Descriptors[Count].TargetAddress = Fifo;
            Direction->Descriptors[Count].Command = Command | PDMA_DCMD_INCSRCADDR | PDMA_DCMD_FLOWTRG;
        }
        Offset += Length;
        ++Count;
    }
    Direction->DescriptorsPhysicalAddress = MmGetPhysicalAddress(Direction->Descriptors);
    for (ULONG Index = 0; Index < Count; ++Index)
    {
        Direction->Descriptors[Index].NextDescriptor =
            static_cast<ULONG>(Direction->DescriptorsPhysicalAddress.QuadPart) +
            ((Index + 1) % Count) * sizeof(K1XAUDIO_PDMA_DESCRIPTOR);
    }
    Direction->SegmentCount = Count;
    KeMemoryBarrier();
    return STATUS_SUCCESS;
}

VOID
CK1xAudioAdapter::UpdateSerialPort()
{
    ULONG Top = ReadRegister(m_I2sRegisters, SSP_TOP_CTRL);

    if (m_Directions[0].Running || m_Directions[1].Running)
        Top |= SSP_TOP_SSE;
    else
        Top &= ~SSP_TOP_SSE;
    WriteRegister(m_I2sRegisters, SSP_TOP_CTRL, Top);
}

VOID
CK1xAudioAdapter::ResetDma(PK1XAUDIO_DIRECTION Direction)
{
    ULONG Loops = 1000, Channel = Direction->Channel;

    WriteRegister(m_DmaRegisters, PDMA_DCSR(Channel),
                  ReadRegister(m_DmaRegisters, PDMA_DCSR(Channel)) & ~PDMA_DCSR_RUN);
    while (!(ReadRegister(m_DmaRegisters, PDMA_DCSR(Channel)) & PDMA_DCSR_STOPSTATE) && --Loops)
        KeStallExecutionProcessor(10);
    WriteRegister(m_DmaRegisters, PDMA_DCSR(Channel), ReadRegister(m_DmaRegisters, PDMA_DCSR(Channel)));
    WriteRegister(m_DmaRegisters, PDMA_DRCMR(Direction->Request), 0);
}

NTSTATUS
NTAPI
CK1xAudioAdapter::StartSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CK1xAudioAdapter *Adapter = static_cast<CK1xAudioAdapter *>(Context);
    PK1XAUDIO_DIRECTION Direction = Adapter->m_SyncDirection;
    PUCHAR Dma = Adapter->m_DmaRegisters;
    ULONG Channel = Direction->Channel;
    UNREFERENCED_PARAMETER(InterruptSync);

    Adapter->ResetDma(Direction);
    Direction->PeriodIndex = 0;
    InterlockedExchange(&Direction->PendingInterrupts, 0);
    InterlockedExchange(&Direction->Running, 1);
    KeMemoryBarrier();
    WriteRegister(Dma, PDMA_DRCMR(Direction->Request), PDMA_DRCMR_MAPVLD | Channel);
    WriteRegister(Dma, PDMA_DALGN, ReadRegister(Dma, PDMA_DALGN) & ~(1u << Channel));
    WriteRegister(Dma, PDMA_DDADR(Channel), static_cast<ULONG>(Direction->DescriptorsPhysicalAddress.QuadPart));
    WriteRegister(Dma, PDMA_DCSR(Channel), PDMA_DCSR_RUN);
    Adapter->UpdateSerialPort();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioAdapter::StopSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CK1xAudioAdapter *Adapter = static_cast<CK1xAudioAdapter *>(Context);
    PK1XAUDIO_DIRECTION Direction = Adapter->m_SyncDirection;
    UNREFERENCED_PARAMETER(InterruptSync);

    InterlockedExchange(&Direction->Running, 0);
    Adapter->UpdateSerialPort();
    Adapter->ResetDma(Direction);
    return STATUS_SUCCESS;
}

NTSTATUS
CK1xAudioAdapter::SetStreamState(BOOLEAN Capture, KSSTATE State)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];
    NTSTATUS Status;

    if (State != KSSTATE_RUN)
    {
        Stop(Capture);
        return STATUS_SUCCESS;
    }
    if (!Direction->Mdl || !Direction->Descriptors || !m_InterruptSync)
        return STATUS_INVALID_DEVICE_STATE;
    if (InterlockedCompareExchange(&Direction->Running, 1, 1))
        return STATUS_SUCCESS;

    if (!m_Directions[0].Running && !m_Directions[1].Running)
        ConfigureI2s();
    m_SyncDirection = Direction;
    Status = m_InterruptSync->CallSynchronizedRoutine(StartSynchronized, this);
    if (NT_SUCCESS(Status))
    {
        if (Capture)
            CodecSetInput(TRUE);
        else
            CodecSetOutput(TRUE);
    }
    return Status;
}

VOID
CK1xAudioAdapter::Stop(BOOLEAN Capture)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];
    BOOLEAN WasRunning = InterlockedCompareExchange(&Direction->Running, 1, 1) != 0;

    if (m_InterruptSync)
    {
        m_SyncDirection = Direction;
        m_InterruptSync->CallSynchronizedRoutine(StopSynchronized, this);
    }
    else if (m_DmaRegisters)
    {
        InterlockedExchange(&Direction->Running, 0);
        ResetDma(Direction);
    }
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        KeFlushQueuedDpcs();
    InterlockedExchange(&Direction->PendingInterrupts, 0);
    if (WasRunning && KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        if (Capture)
            CodecSetInput(FALSE);
        else
            CodecSetOutput(FALSE);
    }
}

VOID
CK1xAudioAdapter::Shutdown()
{
    PPORTEVENTS PortEvents;

    if (InterlockedExchange(&m_Shutdown, 1))
        return;
    Stop(FALSE);
    Stop(TRUE);
    KeRemoveQueueDpc(&m_Dpc);
    PortEvents = static_cast<PPORTEVENTS>(InterlockedExchangePointer(&m_JackPortEvents, NULL));
    if (PortEvents)
        PortEvents->Release();
    if (m_InterruptSync)
    {
        m_InterruptSync->Disconnect();
        m_InterruptSync->Release();
        m_InterruptSync = NULL;
    }
}

NTSTATUS
CK1xAudioAdapter::RegisterJackEventPort(PPORTEVENTS PortEvents)
{
    PVOID Previous;

    if (!PortEvents || InterlockedCompareExchange(&m_Shutdown, 0, 0))
        return STATUS_INVALID_DEVICE_STATE;
    PortEvents->AddRef();
    Previous = InterlockedCompareExchangePointer(&m_JackPortEvents, PortEvents, NULL);
    if (Previous)
    {
        PortEvents->Release();
        return Previous == PortEvents ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CK1xAudioAdapter::InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context)
{
    CK1xAudioAdapter *Adapter = static_cast<CK1xAudioAdapter *>(Context);
    ULONG Pending = ReadRegister(Adapter->m_DmaRegisters, PDMA_DINT);
    BOOLEAN Queue = FALSE;
    UNREFERENCED_PARAMETER(InterruptSync);

    if (!(Pending & ((1u << PDMA_CHANNEL_RENDER) | (1u << PDMA_CHANNEL_CAPTURE))))
        return STATUS_UNSUCCESSFUL;
    for (ULONG Index = 0; Index < RTL_NUMBER_OF(Adapter->m_Directions); ++Index)
    {
        PK1XAUDIO_DIRECTION Direction = &Adapter->m_Directions[Index];
        ULONG ChannelStatus;

        if (!(Pending & (1u << Direction->Channel)))
            continue;
        ChannelStatus = ReadRegister(Adapter->m_DmaRegisters, PDMA_DCSR(Direction->Channel));
        WriteRegister(Adapter->m_DmaRegisters, PDMA_DCSR(Direction->Channel), ChannelStatus);
        if ((ChannelStatus & PDMA_DCSR_ENDINTR) && InterlockedCompareExchange(&Direction->Running, 1, 1))
        {
            InterlockedIncrement(&Direction->PendingInterrupts);
            Queue = TRUE;
        }
    }
    if (Queue)
        KeInsertQueueDpc(&Adapter->m_Dpc, NULL, NULL);
    return STATUS_SUCCESS;
}

VOID
NTAPI
CK1xAudioAdapter::DpcRoutine(PRKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    CK1xAudioAdapter *Adapter = static_cast<CK1xAudioAdapter *>(DeferredContext);
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Adapter->ProcessInterrupts(&Adapter->m_Directions[0]);
    Adapter->ProcessInterrupts(&Adapter->m_Directions[1]);
}

VOID
CK1xAudioAdapter::ProcessInterrupts(PK1XAUDIO_DIRECTION Direction)
{
    while (InterlockedExchange(&Direction->PendingInterrupts, 0))
    {
        ULONG Offset, Period;
        KIRQL OldIrql;

        if (!InterlockedCompareExchange(&Direction->Running, 1, 1) || !GetDmaBufferOffset(Direction, &Offset))
            continue;
        Period = Offset / Direction->PeriodBytes;
        if (Period == Direction->PeriodIndex)
            continue;
        Direction->PeriodIndex = Period;
        KeAcquireSpinLock(&m_EventLock, &OldIrql);
        if (Direction->NotificationEvent)
            KeSetEvent(Direction->NotificationEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseSpinLock(&m_EventLock, OldIrql);
    }
}

BOOLEAN
CK1xAudioAdapter::GetDmaBufferOffset(PK1XAUDIO_DIRECTION Direction, PULONG Offset)
{
    ULONG Address;

    if (!Direction->SegmentCount)
        return FALSE;
    Address = ReadRegister(m_DmaRegisters, Direction->Capture ? PDMA_DTADR(Direction->Channel) :
                                                                  PDMA_DSADR(Direction->Channel));
    for (ULONG Index = 0; Index < Direction->SegmentCount; ++Index)
    {
        if (Address >= Direction->Segments[Index].Physical &&
            Address < Direction->Segments[Index].Physical + Direction->Segments[Index].Length)
        {
            *Offset = Direction->Segments[Index].Offset + (Address - Direction->Segments[Index].Physical);
            return TRUE;
        }
    }
    for (ULONG Index = 0; Index < Direction->SegmentCount; ++Index)
    {
        if (Address == Direction->Segments[Index].Physical + Direction->Segments[Index].Length)
        {
            *Offset = (Direction->Segments[Index].Offset + Direction->Segments[Index].Length) % Direction->BufferSize;
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
CK1xAudioAdapter::GetPosition(BOOLEAN Capture, PKSAUDIO_POSITION Position)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];
    ULONG Offset = 0;

    if (!Position)
        return STATUS_INVALID_PARAMETER;
    if (!Direction->Mdl || !Direction->BufferSize)
        return STATUS_INVALID_DEVICE_STATE;
    if (InterlockedCompareExchange(&Direction->Running, 1, 1))
        GetDmaBufferOffset(Direction, &Offset);
    Position->PlayOffset = Offset;
    Position->WriteOffset = Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
CK1xAudioAdapter::RegisterNotificationEvent(BOOLEAN Capture, PKEVENT NotificationEvent)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    KeAcquireSpinLock(&m_EventLock, &OldIrql);
    if (Direction->NotificationEvent && Direction->NotificationEvent != NotificationEvent)
        Status = STATUS_DEVICE_BUSY;
    else
        Direction->NotificationEvent = NotificationEvent;
    KeReleaseSpinLock(&m_EventLock, OldIrql);
    return Status;
}

NTSTATUS
CK1xAudioAdapter::UnregisterNotificationEvent(BOOLEAN Capture, PKEVENT NotificationEvent)
{
    PK1XAUDIO_DIRECTION Direction = &m_Directions[Capture ? 1 : 0];
    KIRQL OldIrql;

    KeAcquireSpinLock(&m_EventLock, &OldIrql);
    if (Direction->NotificationEvent == NotificationEvent)
        Direction->NotificationEvent = NULL;
    KeReleaseSpinLock(&m_EventLock, OldIrql);
    return STATUS_SUCCESS;
}
