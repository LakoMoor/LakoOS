/*
 * PROJECT:         LakoOS SpacemiT K1 Audio Driver
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         WaveRT render stream
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "private.h"

CK1xAudioStream::CK1xAudioStream(CK1xAudioAdapter *Adapter, PPORTWAVERTSTREAM PortStream, BOOLEAN Capture)
    : m_Adapter(Adapter), m_PortStream(PortStream), m_AudioBufferMdl(NULL), m_State(KSSTATE_STOP), m_Capture(Capture)
{
    m_Adapter->AddRef();
    m_PortStream->AddRef();
}

CK1xAudioStream::~CK1xAudioStream()
{
    m_Adapter->SetStreamState(m_Capture, KSSTATE_STOP);
    if (m_AudioBufferMdl)
        m_Adapter->FreeBuffer(m_Capture, m_PortStream, m_AudioBufferMdl);
    m_PortStream->Release();
    m_Adapter->ReleaseStream(m_Capture);
    m_Adapter->Release();
}

NTSTATUS
NTAPI
CK1xAudioStream::QueryInterface(REFIID InterfaceId, PVOID *Interface)
{
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (IsEqualGUIDAligned(InterfaceId, IID_IUnknown) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStream) ||
        IsEqualGUIDAligned(InterfaceId, IID_IMiniportWaveRTStreamNotification))
    {
        *Interface = static_cast<PMINIPORTWAVERTSTREAMNOTIFICATION>(this);
        AddRef();
        return STATUS_SUCCESS;
    }

    *Interface = NULL;
    return STATUS_NOINTERFACE;
}

NTSTATUS
NTAPI
CK1xAudioStream::SetFormat(PKSDATAFORMAT DataFormat)
{
    if (m_State == KSSTATE_RUN)
        return STATUS_INVALID_DEVICE_STATE;
    return K1xAudioIsFormatSupported(DataFormat) ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CK1xAudioStream::SetState(KSSTATE State)
{
    NTSTATUS Status;

    if (State < KSSTATE_STOP || State > KSSTATE_RUN)
        return STATUS_INVALID_PARAMETER;

    Status = m_Adapter->SetStreamState(m_Capture, State);
    if (NT_SUCCESS(Status))
        m_State = State;
    return Status;
}

NTSTATUS
NTAPI
CK1xAudioStream::GetPosition(PKSAUDIO_POSITION Position)
{
    return m_Adapter->GetPosition(m_Capture, Position);
}

NTSTATUS
NTAPI
CK1xAudioStream::AllocateAudioBuffer(
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    UNREFERENCED_PARAMETER(RequestedSize);
    UNREFERENCED_PARAMETER(AudioBufferMdl);
    UNREFERENCED_PARAMETER(ActualSize);
    UNREFERENCED_PARAMETER(OffsetFromFirstPage);
    UNREFERENCED_PARAMETER(CacheType);
    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
CK1xAudioStream::FreeAudioBuffer(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(AudioBufferMdl);
    UNREFERENCED_PARAMETER(BufferSize);
}

VOID
NTAPI
CK1xAudioStream::GetHWLatency(PKSRTAUDIO_HWLATENCY HardwareLatency)
{
    if (!HardwareLatency)
        return;
    HardwareLatency->FifoSize = 64;
    HardwareLatency->ChipsetDelay = 0;
    HardwareLatency->CodecDelay = 0;
}

NTSTATUS
NTAPI
CK1xAudioStream::GetPositionRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CK1xAudioStream::GetClockRegister(PKSRTAUDIO_HWREGISTER Register)
{
    UNREFERENCED_PARAMETER(Register);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
CK1xAudioStream::AllocateBufferWithNotification(
    ULONG NotificationCount,
    ULONG RequestedSize,
    PMDL *AudioBufferMdl,
    ULONG *ActualSize,
    ULONG *OffsetFromFirstPage,
    MEMORY_CACHING_TYPE *CacheType)
{
    NTSTATUS Status;

    if (m_AudioBufferMdl)
        return STATUS_DEVICE_BUSY;

    Status = m_Adapter->AllocateBuffer(
        m_Capture,
        m_PortStream,
        NotificationCount,
        RequestedSize,
        AudioBufferMdl,
        ActualSize,
        OffsetFromFirstPage,
        CacheType);
    if (NT_SUCCESS(Status))
        m_AudioBufferMdl = *AudioBufferMdl;
    return Status;
}

VOID
NTAPI
CK1xAudioStream::FreeBufferWithNotification(PMDL AudioBufferMdl, ULONG BufferSize)
{
    UNREFERENCED_PARAMETER(BufferSize);

    if (m_AudioBufferMdl && AudioBufferMdl == m_AudioBufferMdl)
    {
        m_Adapter->FreeBuffer(m_Capture, m_PortStream, m_AudioBufferMdl);
        m_AudioBufferMdl = NULL;
    }
}

NTSTATUS
NTAPI
CK1xAudioStream::RegisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->RegisterNotificationEvent(m_Capture, NotificationEvent);
}

NTSTATUS
NTAPI
CK1xAudioStream::UnregisterNotificationEvent(PKEVENT NotificationEvent)
{
    return m_Adapter->UnregisterNotificationEvent(m_Capture, NotificationEvent);
}
