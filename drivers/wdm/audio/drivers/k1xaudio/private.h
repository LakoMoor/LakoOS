/*
 * PROJECT:         LakoOS SpacemiT K1 Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Internal driver definitions
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ks.h>
#include <portcls.h>
#include <ksmedia.h>

#define TAG_K1XAUDIO 'aX1K'
#define K1XAUDIO_SAMPLE_RATE 48000
#define K1XAUDIO_CHANNELS 2
#define K1XAUDIO_BITS_PER_SAMPLE 16
#define K1XAUDIO_BLOCK_ALIGN 4
#define K1XAUDIO_MAX_BUFFER_SIZE (256 * 1024)
#define K1XAUDIO_VOLUME_MINIMUM (-191 * 0x8000)
#define K1XAUDIO_VOLUME_MAXIMUM 0
#define K1XAUDIO_VOLUME_STEP 0x8000
#define K1XAUDIO_MAX_DESCRIPTORS 192

PVOID
__cdecl
operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag);

template <typename Interface> class CUnknownImpl : public Interface
{
  private:
    volatile LONG m_RefCount;

  protected:
    CUnknownImpl() : m_RefCount(0)
    {
    }

    virtual ~CUnknownImpl()
    {
    }

  public:
    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_RefCount);
    }

    STDMETHODIMP_(ULONG) Release()
    {
        ULONG RefCount = InterlockedDecrement(&m_RefCount);
        if (!RefCount)
            delete this;
        return RefCount;
    }
};

typedef struct _K1XAUDIO_SEGMENT
{
    ULONG Physical;
    ULONG Offset;
    ULONG Length;
} K1XAUDIO_SEGMENT, *PK1XAUDIO_SEGMENT;

typedef struct DECLSPEC_ALIGN(16) _K1XAUDIO_PDMA_DESCRIPTOR
{
    ULONG NextDescriptor;
    ULONG SourceAddress;
    ULONG TargetAddress;
    ULONG Command;
} K1XAUDIO_PDMA_DESCRIPTOR, *PK1XAUDIO_PDMA_DESCRIPTOR;

typedef struct _K1XAUDIO_DIRECTION
{
    BOOLEAN Capture;
    ULONG Channel;
    ULONG Request;
    volatile LONG Open;
    volatile LONG Running;
    volatile LONG PendingInterrupts;
    PMDL Mdl;
    PVOID Buffer;
    ULONG BufferSize;
    ULONG NotificationCount;
    ULONG PeriodBytes;
    ULONG PeriodIndex;
    PK1XAUDIO_PDMA_DESCRIPTOR Descriptors;
    ULONG DescriptorsSize;
    PHYSICAL_ADDRESS DescriptorsPhysicalAddress;
    K1XAUDIO_SEGMENT Segments[K1XAUDIO_MAX_DESCRIPTORS];
    ULONG SegmentCount;
    PKEVENT NotificationEvent;
} K1XAUDIO_DIRECTION, *PK1XAUDIO_DIRECTION;

class CK1xAudioAdapter : public CUnknownImpl<IUnknown>
{
  public:
    CK1xAudioAdapter();
    virtual ~CK1xAudioAdapter();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);

    NTSTATUS Initialize(PDEVICE_OBJECT DeviceObject, PRESOURCELIST ResourceList);
    VOID Stop(BOOLEAN Capture);
    VOID Shutdown();
    NTSTATUS RegisterJackEventPort(PPORTEVENTS PortEvents);

    BOOLEAN ClaimStream(BOOLEAN Capture);
    VOID ReleaseStream(BOOLEAN Capture);

    NTSTATUS AllocateBuffer(
        BOOLEAN Capture,
        PPORTWAVERTSTREAM PortStream,
        ULONG NotificationCount,
        ULONG RequestedSize,
        PMDL *AudioBufferMdl,
        ULONG *ActualSize,
        ULONG *OffsetFromFirstPage,
        MEMORY_CACHING_TYPE *CacheType);
    VOID FreeBuffer(BOOLEAN Capture, PPORTWAVERTSTREAM PortStream, PMDL AudioBufferMdl);
    NTSTATUS SetStreamState(BOOLEAN Capture, KSSTATE State);
    NTSTATUS GetPosition(BOOLEAN Capture, PKSAUDIO_POSITION Position);
    NTSTATUS RegisterNotificationEvent(BOOLEAN Capture, PKEVENT NotificationEvent);
    NTSTATUS UnregisterNotificationEvent(BOOLEAN Capture, PKEVENT NotificationEvent);
    NTSTATUS GetVolume(ULONG Channel, PLONG Level);
    NTSTATUS SetVolume(ULONG Channel, LONG Level);
    BOOLEAN GetMute();
    VOID SetMute(BOOLEAN Mute);
    BOOLEAN IsSinkConnected();

  private:
    static NTSTATUS NTAPI InterruptService(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI StartSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static NTSTATUS NTAPI StopSynchronized(PINTERRUPTSYNC InterruptSync, PVOID Context);
    static VOID NTAPI DpcRoutine(PRKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

    NTSTATUS MapResources(PRESOURCELIST ResourceList);
    VOID UnmapResources();
    VOID EnableClocks();
    VOID ConfigurePads();
    NTSTATUS I2cAccess(UCHAR Register, PUCHAR Data, ULONG Count, BOOLEAN Write);
    NTSTATUS CodecWrite(UCHAR Register, UCHAR Value);
    NTSTATUS CodecRead(UCHAR Register, PUCHAR Value);
    NTSTATUS CodecInitialize();
    VOID CodecSetOutput(BOOLEAN Enable);
    VOID CodecSetInput(BOOLEAN Enable);
    VOID CodecApplyVolume();
    VOID ConfigureI2s();
    VOID ResetDma(PK1XAUDIO_DIRECTION Direction);
    VOID UpdateSerialPort();
    NTSTATUS BuildDescriptors(PK1XAUDIO_DIRECTION Direction);
    VOID ProcessInterrupts(PK1XAUDIO_DIRECTION Direction);
    BOOLEAN GetDmaBufferOffset(PK1XAUDIO_DIRECTION Direction, PULONG Offset);
    BOOLEAN ReadGpio(ULONG Number);
    VOID WriteGpio(ULONG Number, BOOLEAN High);

    PDEVICE_OBJECT m_DeviceObject;
    PUCHAR m_I2sRegisters;
    ULONG m_I2sRegistersLength;
    PHYSICAL_ADDRESS m_I2sPhysicalAddress;
    PUCHAR m_DmaRegisters;
    ULONG m_DmaRegistersLength;
    PUCHAR m_I2cRegisters;
    ULONG m_I2cRegistersLength;
    PUCHAR m_Apbc;
    PUCHAR m_Apmu;
    PUCHAR m_Mpmu;
    PUCHAR m_Pads;
    PUCHAR m_Gpio;
    BOOLEAN m_CodecReady;
    UCHAR m_CodecVersion;

    PINTERRUPTSYNC m_InterruptSync;
    KDPC m_Dpc;
    KSPIN_LOCK m_EventLock;
    KSPIN_LOCK m_I2cLock;
    volatile LONG m_Shutdown;
    PVOID volatile m_JackPortEvents;
    volatile LONG m_VolumeLevel[K1XAUDIO_CHANNELS];
    volatile LONG m_Mute;
    PK1XAUDIO_DIRECTION m_SyncDirection;
    K1XAUDIO_DIRECTION m_Directions[2];
};

class CK1xAudioTopology : public CUnknownImpl<IMiniportTopology>
{
  public:
    explicit CK1xAudioTopology(CK1xAudioAdapter *Adapter);
    virtual ~CK1xAudioTopology();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportTopology;

    CK1xAudioAdapter *GetAdapter() const
    {
        return m_Adapter;
    }

    VOID AddEventToEventList(PKSEVENT_ENTRY EventEntry);

  private:
    CK1xAudioAdapter *m_Adapter;
    PPORTEVENTS m_PortEvents;
};

class CK1xAudioWave : public CUnknownImpl<IMiniportWaveRT>
{
  public:
    explicit CK1xAudioWave(CK1xAudioAdapter *Adapter);
    virtual ~CK1xAudioWave();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRT;

  private:
    CK1xAudioAdapter *m_Adapter;
};

class CK1xAudioStream : public CUnknownImpl<IMiniportWaveRTStreamNotification>
{
  public:
    CK1xAudioStream(CK1xAudioAdapter *Adapter, PPORTWAVERTSTREAM PortStream, BOOLEAN Capture);
    virtual ~CK1xAudioStream();

    STDMETHODIMP QueryInterface(REFIID InterfaceId, PVOID *Interface);
    IMP_IMiniportWaveRTStream;
    IMP_IMiniportWaveRTStreamNotification;

  private:
    CK1xAudioAdapter *m_Adapter;
    PPORTWAVERTSTREAM m_PortStream;
    PMDL m_AudioBufferMdl;
    KSSTATE m_State;
    BOOLEAN m_Capture;
};

typedef struct _K1XAUDIO_DEVICE_EXTENSION
{
    ULONG_PTR PortClassReserved[64];
    CK1xAudioAdapter *Adapter;
} K1XAUDIO_DEVICE_EXTENSION, *PK1XAUDIO_DEVICE_EXTENSION;

extern PCFILTER_DESCRIPTOR K1xAudioWaveFilterDescriptor;
extern PCFILTER_DESCRIPTOR K1xAudioTopologyFilterDescriptor;

BOOLEAN
K1xAudioIsFormatSupported(PKSDATAFORMAT DataFormat);

NTSTATUS
K1xAudioCreateTopology(PUNKNOWN *Unknown, CK1xAudioAdapter *Adapter);

NTSTATUS
K1xAudioCreateWave(PUNKNOWN *Unknown, CK1xAudioAdapter *Adapter);
