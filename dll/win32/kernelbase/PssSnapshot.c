/*
 * PROJECT:     LakoOS Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process Snapshotting process-information support
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <string.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <processsnapshot.h>

#include "wine/kernelbase.h"

NTSYSAPI NTSTATUS WINAPI NtCreateProcessEx(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, HANDLE, ULONG, HANDLE, HANDLE, HANDLE, ULONG);

C_ASSERT(sizeof(PSS_PROCESS_INFORMATION) == (sizeof(void *) == 8 ? 704 : 636));
C_ASSERT(sizeof(void *) != 8 || FIELD_OFFSET(PSS_PROCESS_INFORMATION, ParentProcessId) == 32);
C_ASSERT(sizeof(void *) != 8 || FIELD_OFFSET(PSS_PROCESS_INFORMATION, ImageFileName) == 180);
C_ASSERT(sizeof(PSS_THREAD_INFORMATION) == 8);

typedef struct _PSS_SNAPSHOT_ROS
{
    struct _PSS_SNAPSHOT_ROS *Next;
    PSS_PROCESS_INFORMATION ProcessInfo;
    HANDLE VaCloneHandle;
} PSS_SNAPSHOT_ROS;

static RTL_SRWLOCK PssSnapshotLock = RTL_SRWLOCK_INIT;
static PSS_SNAPSHOT_ROS *PssSnapshots;

static PSS_SNAPSHOT_ROS *
PssFindSnapshot(
    _In_ HANDLE Handle)
{
    PSS_SNAPSHOT_ROS *Snapshot;

    for (Snapshot = PssSnapshots; Snapshot; Snapshot = Snapshot->Next)
        if ((HANDLE)Snapshot == Handle) return Snapshot;
    return NULL;
}

DWORD
WINAPI
PssQuerySnapshot(
    _In_ HPSS SnapshotHandle,
    _In_ PSS_QUERY_INFORMATION_CLASS InformationClass,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ DWORD BufferLength)
{
    PSS_SNAPSHOT_ROS *Snapshot;
    DWORD Error = ERROR_SUCCESS;
    DWORD RequiredLength = 0;

    RtlAcquireSRWLockShared(&PssSnapshotLock);
    Snapshot = PssFindSnapshot(SnapshotHandle);
    if (!Snapshot)
    {
        Error = ERROR_INVALID_HANDLE;
        goto Done;
    }
    switch (InformationClass)
    {
        case PSS_QUERY_PROCESS_INFORMATION:
            RequiredLength = sizeof(PSS_PROCESS_INFORMATION);
            break;
        case PSS_QUERY_VA_CLONE_INFORMATION:
            RequiredLength = sizeof(PSS_VA_CLONE_INFORMATION);
            break;
        case PSS_QUERY_THREAD_INFORMATION:
            RequiredLength = sizeof(PSS_THREAD_INFORMATION);
            break;
        default:
            Error = ERROR_INVALID_PARAMETER;
            goto Done;
    }
    if (BufferLength != RequiredLength)
        Error = ERROR_BAD_LENGTH;
    else if (InformationClass == PSS_QUERY_THREAD_INFORMATION)
        Error = ERROR_NOT_FOUND;
    else if (InformationClass == PSS_QUERY_VA_CLONE_INFORMATION)
    {
        if (Snapshot->VaCloneHandle)
            RtlCopyMemory(Buffer, &Snapshot->VaCloneHandle, sizeof(Snapshot->VaCloneHandle));
        else
            Error = ERROR_NOT_FOUND;
    }
    else
        RtlCopyMemory(Buffer, &Snapshot->ProcessInfo, sizeof(Snapshot->ProcessInfo));
Done:
    RtlReleaseSRWLockShared(&PssSnapshotLock);
    return Error;
}

DWORD
WINAPI
PssFreeSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ HPSS SnapshotHandle)
{
    PSS_SNAPSHOT_ROS **Link;
    PSS_SNAPSHOT_ROS *Snapshot;
    NTSTATUS Status;

    Status = NtCompareObjects(ProcessHandle, NtCurrentProcess());
    if (Status == STATUS_NOT_SAME_OBJECT)
        return ERROR_INVALID_HANDLE;
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    for (Link = &PssSnapshots; (Snapshot = *Link); Link = &Snapshot->Next)
    {
        if ((HANDLE)Snapshot != SnapshotHandle)
            continue;
        *Link = Snapshot->Next;
        RtlReleaseSRWLockExclusive(&PssSnapshotLock);
        if (Snapshot->VaCloneHandle)
            NtClose(Snapshot->VaCloneHandle);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Snapshot);
        return ERROR_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    return ERROR_INVALID_HANDLE;
}

DWORD
WINAPI
PssCaptureSnapshot(
    _In_ HANDLE ProcessHandle,
    _In_ PSS_CAPTURE_FLAGS CaptureFlags,
    _In_opt_ DWORD ThreadContextFlags,
    _Out_ HPSS *SnapshotHandle)
{
    PSS_PROCESS_INFORMATION *Info;
    PROCESS_BASIC_INFORMATION BasicInfo;
    PSS_SNAPSHOT_ROS *Snapshot;
    KERNEL_USER_TIMES Times;
    VM_COUNTERS_EX Counters;
    ULONG_PTR Wow64Info = 0;
    ULONG ExecuteFlags = 0;
    DWORD ImageLength = MAX_PATH;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ThreadContextFlags);

    if (SnapshotHandle)
        *SnapshotHandle = NULL;
    if (!SnapshotHandle)
        return ERROR_INVALID_PARAMETER;
    if (CaptureFlags != PSS_CAPTURE_NONE && CaptureFlags != PSS_CAPTURE_VA_CLONE)
        return ERROR_NOT_SUPPORTED;

    Status = NtQueryInformationProcess(ProcessHandle, ProcessBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtQueryInformationProcess(ProcessHandle, ProcessTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    Status = NtQueryInformationProcess(ProcessHandle, ProcessVmCounters, &Counters, sizeof(Counters), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);

    Snapshot = RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY, sizeof(*Snapshot));
    if (!Snapshot)
        return ERROR_NOT_ENOUGH_MEMORY;

    Info = &Snapshot->ProcessInfo;
    Info->ExitStatus = BasicInfo.ExitStatus;
    Info->PebBaseAddress = BasicInfo.PebBaseAddress;
    Info->AffinityMask = BasicInfo.AffinityMask;
    Info->BasePriority = BasicInfo.BasePriority;
    Info->ProcessId = (DWORD)BasicInfo.UniqueProcessId;
    Info->ParentProcessId = (DWORD)BasicInfo.InheritedFromUniqueProcessId;
    Info->CreateTime.dwLowDateTime = Times.CreateTime.u.LowPart;
    Info->CreateTime.dwHighDateTime = Times.CreateTime.u.HighPart;
    Info->ExitTime.dwLowDateTime = Times.ExitTime.u.LowPart;
    Info->ExitTime.dwHighDateTime = Times.ExitTime.u.HighPart;
    Info->KernelTime.dwLowDateTime = Times.KernelTime.u.LowPart;
    Info->KernelTime.dwHighDateTime = Times.KernelTime.u.HighPart;
    Info->UserTime.dwLowDateTime = Times.UserTime.u.LowPart;
    Info->UserTime.dwHighDateTime = Times.UserTime.u.HighPart;
    Info->PriorityClass = GetPriorityClass(ProcessHandle);
    Info->PeakVirtualSize = Counters.PeakVirtualSize;
    Info->VirtualSize = Counters.VirtualSize;
    Info->PageFaultCount = Counters.PageFaultCount;
    Info->PeakWorkingSetSize = Counters.PeakWorkingSetSize;
    Info->WorkingSetSize = Counters.WorkingSetSize;
    Info->QuotaPeakPagedPoolUsage = Counters.QuotaPeakPagedPoolUsage;
    Info->QuotaPagedPoolUsage = Counters.QuotaPagedPoolUsage;
    Info->QuotaPeakNonPagedPoolUsage = Counters.QuotaPeakNonPagedPoolUsage;
    Info->QuotaNonPagedPoolUsage = Counters.QuotaNonPagedPoolUsage;
    Info->PagefileUsage = Counters.PagefileUsage;
    Info->PeakPagefileUsage = Counters.PeakPagefileUsage;
    Info->PrivateUsage = Counters.PrivateUsage;
    Status = NtQueryInformationProcess(ProcessHandle, ProcessExecuteFlags, &ExecuteFlags, sizeof(ExecuteFlags), NULL);
    if (NT_SUCCESS(Status))
        Info->ExecuteFlags = ExecuteFlags;
    Status = NtQueryInformationProcess(ProcessHandle, ProcessWow64Information, &Wow64Info, sizeof(Wow64Info), NULL);
    if (NT_SUCCESS(Status) && Wow64Info)
        Info->Flags |= PSS_PROCESS_FLAGS_WOW64;
    if (!QueryFullProcessImageNameW(ProcessHandle, 0, Info->ImageFileName, &ImageLength))
    {
        DWORD Error = GetLastError();

        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Snapshot);
        return Error;
    }

    if (CaptureFlags & PSS_CAPTURE_VA_CLONE)
    {
        Status = NtCreateProcessEx(&Snapshot->VaCloneHandle, MAXIMUM_ALLOWED, NULL,
                                   ProcessHandle, 0, NULL, NULL, NULL, 0);
        if (!NT_SUCCESS(Status))
        {
            RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Snapshot);
            return RtlNtStatusToDosError(Status);
        }
    }

    RtlAcquireSRWLockExclusive(&PssSnapshotLock);
    Snapshot->Next = PssSnapshots;
    PssSnapshots = Snapshot;
    RtlReleaseSRWLockExclusive(&PssSnapshotLock);
    *SnapshotHandle = (HPSS)Snapshot;
    return ERROR_SUCCESS;
}
