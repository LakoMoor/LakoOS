/*
 * PROJECT:     LakoOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify the owner of locally captured process snapshots
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DWORD (WINAPI *PSS_CAPTURE)(HANDLE, DWORD, DWORD, HANDLE *);
typedef DWORD (WINAPI *PSS_FREE)(HANDLE, HANDLE);
typedef DWORD (WINAPI *PSS_QUERY)(HANDLE, DWORD, void *, DWORD);

static DWORD
SnapshotChild(const char *ReadyName, const char *StopName)
{
    HANDLE Ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, ReadyName);
    HANDLE Stop = OpenEventA(SYNCHRONIZE, FALSE, StopName);
    DWORD Wait = WAIT_FAILED;

    if (Ready && Stop && SetEvent(Ready))
        Wait = WaitForSingleObject(Stop, 20000);
    if (Ready) CloseHandle(Ready);
    if (Stop) CloseHandle(Stop);
    return Wait == WAIT_OBJECT_0 ? 0 : 1;
}

static void
CheckSnapshotOwner(PSS_CAPTURE Capture, PSS_FREE Free, HANDLE Target, HANDLE Owner, const char *Name, BOOL ExpectedSuccess)
{
    HANDLE Snapshot = NULL;
    DWORD Error;

    Error = Capture(Target, 0, 0, &Snapshot);
    ok(Error == ERROR_SUCCESS, "%s capture returned %lu\n", Name, Error);
    if (Error != ERROR_SUCCESS) return;
    ok(Snapshot != NULL, "%s capture returned a null snapshot\n", Name);
    if (!Snapshot) return;
    Error = Free(Owner, Snapshot);
    if (ExpectedSuccess)
        ok(Error == ERROR_SUCCESS, "%s free returned %lu\n", Name, Error);
    else
        ok(Error != ERROR_SUCCESS, "%s free accepted a different owner\n", Name);
}

static void
CheckSnapshotQuery(PSS_CAPTURE Capture, PSS_QUERY Query, PSS_FREE Free, HANDLE Target, const char *Name)
{
    const DWORD ProcessSize = sizeof(HANDLE) == 8 ? 704 : 636;
    struct
    {
        DWORD InformationClass;
        DWORD Length;
        DWORD Error;
    } Cases[] = {
        {0, 0, ERROR_BAD_LENGTH},
        {0, ProcessSize - 1, ERROR_BAD_LENGTH},
        {0, ProcessSize, ERROR_SUCCESS},
        {0, ProcessSize + sizeof(HANDLE), ERROR_BAD_LENGTH},
        {1, 0, ERROR_BAD_LENGTH},
        {1, sizeof(HANDLE) - 1, ERROR_BAD_LENGTH},
        {1, sizeof(HANDLE), ERROR_NOT_FOUND},
        {1, 2 * sizeof(HANDLE), ERROR_BAD_LENGTH},
        {MAXDWORD, 704 + sizeof(HANDLE), ERROR_INVALID_PARAMETER}
    };
    union
    {
        ULONG_PTR Alignment;
        BYTE Bytes[704 + sizeof(HANDLE)];
    } Buffer;
    BYTE Expected[sizeof(Buffer)];
    HANDLE Snapshot = NULL;
    DWORD Error, LastError, Index, Offset;

    Error = Capture(Target, 0, 0, &Snapshot);
    ok(Error == ERROR_SUCCESS, "%s query capture returned %lu\n", Name, Error);
    if (Error != ERROR_SUCCESS) return;
    ok(Snapshot != NULL, "%s query capture returned a null snapshot\n", Name);
    if (!Snapshot) return;
    memset(Expected, 0xa5, sizeof(Expected));
    for (Index = 0; Index < _countof(Cases); ++Index)
    {
        memset(&Buffer, 0xa5, sizeof(Buffer));
        SetLastError(0xdeadbeef);
        Error = Query(Snapshot, Cases[Index].InformationClass, &Buffer, Cases[Index].Length);
        LastError = GetLastError();
        ok(Error == Cases[Index].Error, "%s query class %lu length %lu returned %lu, expected %lu\n",
           Name, Cases[Index].InformationClass, Cases[Index].Length, Error, Cases[Index].Error);
        ok(LastError == 0xdeadbeef, "%s query class %lu length %lu changed last error to %#lx\n",
           Name, Cases[Index].InformationClass, Cases[Index].Length, LastError);
        Offset = Cases[Index].Error == ERROR_SUCCESS ? ProcessSize : 0;
        ok(!memcmp(Buffer.Bytes + Offset, Expected + Offset, sizeof(Buffer) - Offset),
           "%s query class %lu length %lu modified bytes outside its successful result\n",
           Name, Cases[Index].InformationClass, Cases[Index].Length);
    }
    Error = Free(GetCurrentProcess(), Snapshot);
    ok(Error == ERROR_SUCCESS, "%s query free returned %lu\n", Name, Error);
}

static void
CheckSnapshotThreadQuery(PSS_CAPTURE Capture, PSS_QUERY Query, PSS_FREE Free)
{
    const DWORD ProcessSize = sizeof(HANDLE) == 8 ? 704 : 636;
    const DWORD Lengths[] = {0, 7, 8, 9, 16};
    const DWORD Classes[] = {0, 1, 5, MAXDWORD};
    union
    {
        ULONG_PTR Alignment;
        BYTE Bytes[704 + sizeof(HANDLE)];
    } Buffer;
    BYTE Expected[sizeof(Buffer)];
    HANDLE Snapshot;
    DWORD Flags, Index, Error, ExpectedError, LastError, Length, ClassIndex;

    memset(Expected, 0xa5, sizeof(Expected));
    for (Flags = 0; Flags <= 1; ++Flags)
    {
        Snapshot = NULL;
        Error = Capture(GetCurrentProcess(), Flags, 0, &Snapshot);
        ok(Error == ERROR_SUCCESS && Snapshot, "Thread query capture flags %#lx returned %lu\n", Flags, Error);
        if (Error != ERROR_SUCCESS || !Snapshot) continue;
        for (Index = 0; Index < _countof(Lengths); ++Index)
        {
            memset(&Buffer, 0xa5, sizeof(Buffer));
            SetLastError(0xdeadbeef);
            Error = Query(Snapshot, 5, &Buffer, Lengths[Index]);
            LastError = GetLastError();
            ExpectedError = Lengths[Index] == 8 ? ERROR_NOT_FOUND : ERROR_BAD_LENGTH;
            ok(Error == ExpectedError, "Thread query flags %#lx length %lu returned %lu instead of %lu\n",
               Flags, Lengths[Index], Error, ExpectedError);
            ok(LastError == 0xdeadbeef, "Thread query changed last error to %#lx\n", LastError);
            ok(!memcmp(&Buffer, Expected, sizeof(Buffer)), "Absent thread query modified its output\n");
        }
        SetLastError(0xdeadbeef);
        Error = Query(Snapshot, 5, NULL, 8);
        LastError = GetLastError();
        ok(Error == ERROR_NOT_FOUND, "Null-buffer absent thread query returned %lu\n", Error);
        ok(LastError == 0xdeadbeef, "Null-buffer thread query changed last error to %#lx\n", LastError);
        Error = Free(GetCurrentProcess(), Snapshot);
        ok(Error == ERROR_SUCCESS, "Thread query free returned %lu\n", Error);
    }
    for (ClassIndex = 0; ClassIndex < _countof(Classes); ++ClassIndex)
        for (Index = 0; Index < 3; ++Index)
        {
            Length = Classes[ClassIndex] == 0 ? ProcessSize : Classes[ClassIndex] == 1 ? sizeof(HANDLE) : 8;
            Length = Index == 0 ? 0 : Length + Index - 1;
            memset(&Buffer, 0xa5, sizeof(Buffer));
            SetLastError(0xdeadbeef);
            Error = Query(NULL, Classes[ClassIndex], &Buffer, Length);
            LastError = GetLastError();
            ok(Error == ERROR_INVALID_HANDLE, "Null snapshot query class %lu length %lu returned %lu\n",
               Classes[ClassIndex], Length, Error);
            ok(LastError == 0xdeadbeef, "Null snapshot query changed last error to %#lx\n", LastError);
            ok(!memcmp(&Buffer, Expected, sizeof(Buffer)), "Null snapshot query modified its output\n");
        }
}

static void
CheckCloneByte(HANDLE Process, const void *Address, BYTE Expected, const char *Name)
{
    BYTE Value = 0xcc;
    SIZE_T Bytes = 0;
    BOOL Result;

    Result = ReadProcessMemory(Process, Address, &Value, sizeof(Value), &Bytes);
    ok(Result, "%s read failed: %lu\n", Name, GetLastError());
    ok(Bytes == sizeof(Value), "%s read %I64u bytes\n", Name, (ULONGLONG)Bytes);
    ok(Value == Expected, "%s byte %#x, expected %#x\n", Name, Value, Expected);
}

static void
CheckCloneGuard(HANDLE Process, const void *Address, const char *Name)
{
    MEMORY_BASIC_INFORMATION Information;
    BYTE Value;
    SIZE_T Bytes, Length;
    DWORD Error, Index;
    BOOL Result;

    for (Index = 0; Index < 2; ++Index)
    {
        Value = 0xcc;
        Bytes = 0;
        SetLastError(0xdeadbeef);
        Result = ReadProcessMemory(Process, Address, &Value, sizeof(Value), &Bytes);
        Error = GetLastError();
        ok(!Result && Error == ERROR_PARTIAL_COPY,
           "%s guard read %lu returned %u, error %lu\n", Name, Index, Result, Error);
        ok(Bytes == 0 && Value == 0xcc,
           "%s guard read %lu changed bytes %I64u or value %#x\n", Name, Index, (ULONGLONG)Bytes, Value);
        memset(&Information, 0, sizeof(Information));
        Length = VirtualQueryEx(Process, Address, &Information, sizeof(Information));
        ok(Length == sizeof(Information), "%s guard query %lu returned %I64u\n", Name, Index, (ULONGLONG)Length);
        if (Length == sizeof(Information))
            ok(Information.Protect == (PAGE_READWRITE | PAGE_GUARD),
               "%s guard read %lu changed protection to %#lx\n", Name, Index, Information.Protect);
    }
}

static void
CheckSnapshotClone(PSS_CAPTURE Capture, PSS_QUERY Query, PSS_FREE Free)
{
    SYSTEM_INFO System;
    HANDLE Snapshot = NULL, Clone = NULL, Duplicate = NULL;
    HANDLE SharedMapping = NULL, CopyMapping = NULL;
    BYTE *Private = NULL, *Guard = NULL, *Shared = NULL, *CopyClean = NULL, *CopyDirty = NULL, *Initial = NULL;
    const void *PrivateAddress, *SharedAddress, *CleanAddress, *DirtyAddress;
    struct
    {
        HANDLE Handle;
        ULONG_PTR Canary;
    } Information;
    DWORD Error, OldProtect, ClonePid, ExitCode = 0, Wait;
    BOOL Result;

    GetSystemInfo(&System);
    Private = VirtualAlloc(NULL, System.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Guard = VirtualAlloc(NULL, System.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    SharedMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, System.dwPageSize, NULL);
    CopyMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, System.dwPageSize, NULL);
    ok(Private && Guard && SharedMapping && CopyMapping, "Clone fixture allocation failed: %lu\n", GetLastError());
    if (!Private || !Guard || !SharedMapping || !CopyMapping) goto Cleanup;
    Shared = MapViewOfFile(SharedMapping, FILE_MAP_WRITE, 0, 0, System.dwPageSize);
    Initial = MapViewOfFile(CopyMapping, FILE_MAP_WRITE, 0, 0, System.dwPageSize);
    ok(Shared && Initial, "Clone writable mapping failed: %lu\n", GetLastError());
    if (!Shared || !Initial) goto Cleanup;
    *Private = 0x11;
    *Guard = 0x21;
    *Shared = 0x31;
    *Initial = 0x51;
    Result = UnmapViewOfFile(Initial);
    ok(Result, "Initial copy view unmap failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    Initial = NULL;
    CopyClean = MapViewOfFile(CopyMapping, FILE_MAP_COPY, 0, 0, System.dwPageSize);
    CopyDirty = MapViewOfFile(CopyMapping, FILE_MAP_COPY, 0, 0, System.dwPageSize);
    ok(CopyClean && CopyDirty, "Clone copy mapping failed: %lu\n", GetLastError());
    if (!CopyClean || !CopyDirty) goto Cleanup;
    *CopyDirty = 0x61;
    Result = VirtualProtect(Guard, System.dwPageSize, PAGE_READWRITE | PAGE_GUARD, &OldProtect);
    ok(Result, "Guard fixture protection failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;

    Error = Capture(GetCurrentProcess(), 1, 0, &Snapshot);
    ok(Error == ERROR_SUCCESS, "VA clone capture returned %lu\n", Error);
    if (Error != ERROR_SUCCESS) goto Cleanup;
    ok(Snapshot != NULL, "VA clone capture returned a null snapshot\n");
    if (!Snapshot) goto Cleanup;
    Information.Handle = NULL;
    Information.Canary = 0x13579bdf;
    SetLastError(0xdeadbeef);
    Error = Query(Snapshot, 1, &Information, sizeof(HANDLE));
    ok(GetLastError() == 0xdeadbeef, "VA clone query changed last error to %#lx\n", GetLastError());
    ok(Error == ERROR_SUCCESS, "VA clone query returned %lu\n", Error);
    ok(Information.Canary == 0x13579bdf, "VA clone query changed its output canary\n");
    if (Error != ERROR_SUCCESS) goto Cleanup;
    Clone = Information.Handle;
    ok(Clone != NULL, "VA clone query returned a null process handle\n");
    if (!Clone) goto Cleanup;
    Information.Handle = NULL;
    Error = Query(Snapshot, 1, &Information, sizeof(HANDLE));
    ok(Error == ERROR_SUCCESS && Information.Handle == Clone,
       "Repeated VA clone query returned %lu, handle %p instead of %p\n", Error, Information.Handle, Clone);
    ok(Information.Canary == 0x13579bdf, "Repeated VA clone query changed its output canary\n");
    ClonePid = GetProcessId(Clone);
    ok(ClonePid != 0 && ClonePid != GetCurrentProcessId(), "VA clone process ID %lu\n", ClonePid);
    Result = DuplicateHandle(GetCurrentProcess(), Clone, GetCurrentProcess(), &Duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS);
    ok(Result, "VA clone handle duplication failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    ok(GetProcessId(Duplicate) == ClonePid, "Duplicate refers to a different process\n");

    *Private = 0x12;
    *Shared = 0x32;
    *CopyClean = 0x52;
    *CopyDirty = 0x62;
    CheckCloneByte(Clone, Private, 0x11, "clone private after source write");
    CheckCloneByte(Clone, Shared, 0x32, "clone shared after source write");
    CheckCloneByte(Clone, CopyClean, 0x51, "clone clean copy after source write");
    CheckCloneByte(Clone, CopyDirty, 0x61, "clone dirty copy after source write");
    CheckCloneGuard(Clone, Guard, "clone");

    PrivateAddress = Private;
    SharedAddress = Shared;
    CleanAddress = CopyClean;
    DirtyAddress = CopyDirty;
    Result = VirtualFree(Private, 0, MEM_RELEASE);
    ok(Result, "Source private release failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    Private = NULL;
    Result = UnmapViewOfFile(Shared);
    ok(Result, "Source shared unmap failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    Shared = NULL;
    Result = UnmapViewOfFile(CopyClean);
    ok(Result, "Source clean copy unmap failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    CopyClean = NULL;
    Result = UnmapViewOfFile(CopyDirty);
    ok(Result, "Source dirty copy unmap failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    CopyDirty = NULL;
    Error = Free(GetCurrentProcess(), Snapshot);
    Snapshot = NULL;
    Clone = NULL;
    ok(Error == ERROR_SUCCESS, "VA clone snapshot free returned %lu\n", Error);
    ok(GetProcessId(Duplicate) == ClonePid, "Duplicate process ID changed after snapshot free\n");
    Wait = WaitForSingleObject(Duplicate, 0);
    ok(Wait == WAIT_TIMEOUT, "Duplicate wait after snapshot free returned %#lx\n", Wait);
    Result = GetExitCodeProcess(Duplicate, &ExitCode);
    ok(Result && ExitCode == STILL_ACTIVE, "Duplicate exit query returned %u, code %lu\n", Result, ExitCode);
    CheckCloneByte(Duplicate, PrivateAddress, 0x11, "duplicate private after release");
    CheckCloneByte(Duplicate, SharedAddress, 0x32, "duplicate shared after release");
    CheckCloneByte(Duplicate, CleanAddress, 0x51, "duplicate clean copy after release");
    CheckCloneByte(Duplicate, DirtyAddress, 0x61, "duplicate dirty copy after release");
    CheckCloneGuard(Duplicate, Guard, "duplicate after snapshot free");

Cleanup:
    if (Snapshot)
    {
        Error = Free(GetCurrentProcess(), Snapshot);
        ok(Error == ERROR_SUCCESS, "VA clone cleanup free returned %lu\n", Error);
    }
    if (Duplicate) CloseHandle(Duplicate);
    if (CopyDirty) UnmapViewOfFile(CopyDirty);
    if (CopyClean) UnmapViewOfFile(CopyClean);
    if (Initial) UnmapViewOfFile(Initial);
    if (Shared) UnmapViewOfFile(Shared);
    if (CopyMapping) CloseHandle(CopyMapping);
    if (SharedMapping) CloseHandle(SharedMapping);
    if (Guard) VirtualFree(Guard, 0, MEM_RELEASE);
    if (Private) VirtualFree(Private, 0, MEM_RELEASE);
}

START_TEST(PssSnapshot)
{
    char **Arguments;
    char Application[MAX_PATH], CommandLine[2 * MAX_PATH + 256];
    char ReadyName[96], StopName[96];
    int ArgumentCount;
    HANDLE Ready = NULL, Stop = NULL, OwnerAll = NULL, OwnerDocumented = NULL;
    DWORD Pid = GetCurrentProcessId(), Wait, Length, ExitCode = STILL_ACTIVE;
    PROCESS_INFORMATION Process = {0};
    STARTUPINFOA Startup = {0};
    PSS_CAPTURE Capture;
    PSS_FREE Free;
    PSS_QUERY Query;
    BOOL Result;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount == 5 && !strcmp(Arguments[2], "child"))
        ExitProcess(SnapshotChild(Arguments[3], Arguments[4]));

    Capture = (PSS_CAPTURE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PssCaptureSnapshot");
    Free = (PSS_FREE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PssFreeSnapshot");
    Query = (PSS_QUERY)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PssQuerySnapshot");
    ok(Capture != NULL, "PssCaptureSnapshot is missing\n");
    ok(Free != NULL, "PssFreeSnapshot is missing\n");
    ok(Query != NULL, "PssQuerySnapshot is missing\n");
    if (!Capture || !Free || !Query) return;

    OwnerAll = OpenProcess(PROCESS_ALL_ACCESS, FALSE, Pid);
    OwnerDocumented = OpenProcess(PROCESS_VM_READ | PROCESS_VM_OPERATION | PROCESS_DUP_HANDLE, FALSE, Pid);
    ok(OwnerAll != NULL, "OpenProcess all access failed: %lu\n", GetLastError());
    ok(OwnerDocumented != NULL, "OpenProcess documented rights failed: %lu\n", GetLastError());
    if (!OwnerAll || !OwnerDocumented) goto Cleanup;

    Result = SUCCEEDED(StringCchPrintfA(ReadyName, _countof(ReadyName), "Local\\PssSnapshotReady-%lu", Pid)) &&
             SUCCEEDED(StringCchPrintfA(StopName, _countof(StopName), "Local\\PssSnapshotStop-%lu", Pid));
    ok(Result, "Failed to format child event names\n");
    if (!Result) goto Cleanup;
    Ready = CreateEventA(NULL, TRUE, FALSE, ReadyName);
    Stop = CreateEventA(NULL, TRUE, FALSE, StopName);
    ok(Ready != NULL && Stop != NULL, "CreateEvent failed: %lu\n", GetLastError());
    if (!Ready || !Stop) goto Cleanup;
    Length = GetModuleFileNameA(NULL, Application, _countof(Application));
    ok(Length != 0 && Length < _countof(Application), "GetModuleFileName failed: %lu\n", GetLastError());
    if (!Length || Length >= _countof(Application)) goto Cleanup;
    Result = SUCCEEDED(StringCchPrintfA(CommandLine, _countof(CommandLine), "\"%s\" PssSnapshot child %s %s",
                                        Application, ReadyName, StopName));
    ok(Result, "Failed to format child command line\n");
    if (!Result) goto Cleanup;
    Startup.cb = sizeof(Startup);
    Result = CreateProcessA(Application, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                            NULL, NULL, &Startup, &Process);
    ok(Result, "CreateProcess failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    Wait = WaitForSingleObject(Ready, 5000);
    ok(Wait == WAIT_OBJECT_0, "Child readiness wait returned %#lx\n", Wait);
    if (Wait != WAIT_OBJECT_0) goto Cleanup;

    CheckSnapshotThreadQuery(Capture, Query, Free);
    CheckSnapshotClone(Capture, Query, Free);

    CheckSnapshotQuery(Capture, Query, Free, GetCurrentProcess(), "self");
    CheckSnapshotQuery(Capture, Query, Free, Process.hProcess, "remote");

    CheckSnapshotOwner(Capture, Free, GetCurrentProcess(), GetCurrentProcess(), "self/pseudo", TRUE);
    CheckSnapshotOwner(Capture, Free, GetCurrentProcess(), OwnerAll, "self/real-all", TRUE);
    CheckSnapshotOwner(Capture, Free, GetCurrentProcess(), OwnerDocumented, "self/real-documented", TRUE);
    CheckSnapshotOwner(Capture, Free, Process.hProcess, GetCurrentProcess(), "remote/pseudo", TRUE);
    CheckSnapshotOwner(Capture, Free, Process.hProcess, OwnerAll, "remote/real-all", TRUE);
    CheckSnapshotOwner(Capture, Free, Process.hProcess, OwnerDocumented, "remote/real-documented", TRUE);
    CheckSnapshotOwner(Capture, Free, Process.hProcess, Process.hProcess, "remote/wrong-owner", FALSE);

Cleanup:
    if (Stop) SetEvent(Stop);
    if (Process.hProcess)
    {
        Wait = WaitForSingleObject(Process.hProcess, 5000);
        ok(Wait == WAIT_OBJECT_0, "Child exit wait returned %#lx\n", Wait);
        if (Wait != WAIT_OBJECT_0)
        {
            TerminateProcess(Process.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(Process.hProcess, 5000);
        }
        Result = GetExitCodeProcess(Process.hProcess, &ExitCode);
        ok(Result, "GetExitCodeProcess failed: %lu\n", GetLastError());
        if (Result) ok(ExitCode == 0, "Child exit code %lu\n", ExitCode);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
    if (Stop) CloseHandle(Stop);
    if (Ready) CloseHandle(Ready);
    if (OwnerDocumented) CloseHandle(OwnerDocumented);
    if (OwnerAll) CloseHandle(OwnerAll);
}
