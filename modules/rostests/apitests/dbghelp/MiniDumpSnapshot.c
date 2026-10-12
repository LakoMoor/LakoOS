/*
 * PROJECT:     LakoOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Verify minidumps use captured process snapshot memory and metadata
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <dbghelp.h>
#include <processsnapshot.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wine/test.h>

typedef DWORD (WINAPI *PSS_CAPTURE)(HANDLE, PSS_CAPTURE_FLAGS, DWORD, HPSS *);
typedef DWORD (WINAPI *PSS_QUERY)(HPSS, PSS_QUERY_INFORMATION_CLASS, void *, DWORD);
typedef DWORD (WINAPI *PSS_FREE)(HANDLE, HPSS);

struct snapshot_control
{
    HANDLE Ready, Stop;
    ULONG_PTR Address, Image;
    DWORD Error;
};

struct snapshot_callback
{
    HPSS Snapshot;
    ULONG_PTR Address;
    DWORD Calls[32];
    BOOL Added;
};

static void
CloseTestHandle(HANDLE Handle)
{
    if (Handle && Handle != INVALID_HANDLE_VALUE)
        ok(CloseHandle(Handle), "CloseHandle failed: %lu\n", GetLastError());
}

static DWORD
SnapshotChild(HANDLE Mapping)
{
    struct snapshot_control *Control;
    HANDLE Ready, Stop;
    BYTE *Memory;
    DWORD ExitCode = 1;

    Control = MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Control));
    if (!Control) return 1;
    Ready = Control->Ready;
    Stop = Control->Stop;
    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Memory) Control->Error = GetLastError();
    else
    {
        memset(Memory, 0x41, 4096);
        Control->Address = (ULONG_PTR)Memory;
        Control->Image = (ULONG_PTR)GetModuleHandleW(NULL);
    }
    if (SetEvent(Ready) && WaitForSingleObject(Stop, 20000) == WAIT_OBJECT_0 && Memory)
        ExitCode = 0;
    if (Memory && !VirtualFree(Memory, 0, MEM_RELEASE)) ExitCode = 1;
    if (!CloseHandle(Ready)) ExitCode = 1;
    if (!CloseHandle(Stop)) ExitCode = 1;
    if (!UnmapViewOfFile(Control)) ExitCode = 1;
    if (!CloseHandle(Mapping)) ExitCode = 1;
    return ExitCode;
}

static BOOL CALLBACK
SnapshotCallback(void *Argument, PMINIDUMP_CALLBACK_INPUT Input, PMINIDUMP_CALLBACK_OUTPUT Output)
{
    struct snapshot_callback *State = Argument;
    DWORD Type = Input->CallbackType;

    if (Type < _countof(State->Calls)) ++State->Calls[Type];
    if (Type == IsProcessSnapshotCallback || Type == VmStartCallback ||
        Type == SecondaryFlagsCallback || Type == IncludeModuleCallback || Type == ModuleCallback)
        ok(Input->ProcessHandle == (HANDLE)State->Snapshot,
           "Callback %lu has process handle %p instead of snapshot %p\n",
           Type, Input->ProcessHandle, State->Snapshot);
    if (Type == IsProcessSnapshotCallback || Type == VmStartCallback)
    {
        ok(Input->ProcessId == 0, "Callback %lu process ID %lu\n", Type, Input->ProcessId);
        ok(Output->Status == E_NOTIMPL, "Callback %lu initial status %#lx\n", Type, Output->Status);
    }
    if (Type == IsProcessSnapshotCallback) Output->Status = S_FALSE;
    if (Type == MemoryCallback)
    {
        if (!State->Address || State->Added) return FALSE;
        State->Added = TRUE;
        Output->MemoryBase = State->Address;
        Output->MemorySize = 16;
    }
    if (Type == RemoveMemoryCallback || Type == WriteKernelMinidumpCallback) return FALSE;
    return TRUE;
}

static const void *
DumpRange(const BYTE *Base, SIZE_T Size, ULONG64 Offset, ULONG64 Length)
{
    if (Offset > Size || Length > Size - Offset) return NULL;
    return Base + (SIZE_T)Offset;
}

static const void *
DumpStream(const BYTE *Base, SIZE_T Size, ULONG Type, ULONG *Length)
{
    const MINIDUMP_HEADER *Header = DumpRange(Base, Size, 0, sizeof(*Header));
    const MINIDUMP_DIRECTORY *Directory;
    ULONG Index;

    *Length = 0;
    if (!Header || Header->Signature != MINIDUMP_SIGNATURE) return NULL;
    Directory = DumpRange(Base, Size, Header->StreamDirectoryRva,
                          (ULONG64)Header->NumberOfStreams * sizeof(*Directory));
    if (!Directory) return NULL;
    for (Index = 0; Index < Header->NumberOfStreams; ++Index)
        if (Directory[Index].StreamType == Type)
        {
            *Length = Directory[Index].Location.DataSize;
            return DumpRange(Base, Size, Directory[Index].Location.Rva, *Length);
        }
    return NULL;
}

static ULONGLONG
FileTimeValue(FILETIME Time)
{
    return ((ULONGLONG)Time.dwHighDateTime << 32) | Time.dwLowDateTime;
}

static void
CheckDumpContents(const BYTE *Base, SIZE_T Size, const struct snapshot_control *Control,
                  const PSS_PROCESS_INFORMATION *Process)
{
    const MINIDUMP_THREAD_LIST *Threads;
    const MINIDUMP_MODULE_LIST *Modules;
    const MINIDUMP_MEMORY_LIST *Memory;
    const MINIDUMP_MISC_INFO *Misc;
    ULONG Length, Index;
    BOOL Found = FALSE, Valid;
    SYSTEMTIME Epoch = {1970, 1, 4, 1, 0, 0, 0, 0};
    FILETIME EpochTime;

    Threads = DumpStream(Base, Size, ThreadListStream, &Length);
    ok(Threads && Length >= sizeof(ULONG), "Missing or truncated thread list\n");
    if (Threads && Length >= sizeof(ULONG))
        ok(Threads->NumberOfThreads == 0, "Uncaptured thread count %lu\n", (DWORD)Threads->NumberOfThreads);

    Modules = DumpStream(Base, Size, ModuleListStream, &Length);
    Valid = Modules && Length >= sizeof(ULONG) &&
            Modules->NumberOfModules <= (Length - sizeof(ULONG)) / sizeof(MINIDUMP_MODULE);
    ok(Valid, "Missing or truncated module list\n");
    if (Valid)
    {
        ok(Modules->NumberOfModules != 0, "Snapshot contains no modules\n");
        for (Index = 0; Index < Modules->NumberOfModules; ++Index)
            if (Modules->Modules[Index].BaseOfImage == Control->Image) Found = TRUE;
        ok(Found, "Captured executable module is missing\n");
    }

    Memory = DumpStream(Base, Size, MemoryListStream, &Length);
    Valid = Memory && Length >= sizeof(ULONG) &&
            Memory->NumberOfMemoryRanges <= (Length - sizeof(ULONG)) / sizeof(MINIDUMP_MEMORY_DESCRIPTOR);
    ok(Valid, "Missing or truncated memory list\n");
    Found = FALSE;
    if (Valid)
        for (Index = 0; Index < Memory->NumberOfMemoryRanges; ++Index)
        {
            const MINIDUMP_MEMORY_DESCRIPTOR *Entry = &Memory->MemoryRanges[Index];
            ULONG64 Delta = Control->Address - Entry->StartOfMemoryRange;
            const BYTE *Data;
            ULONG Byte;
            if (Control->Address < Entry->StartOfMemoryRange || Delta > Entry->Memory.DataSize ||
                16 > Entry->Memory.DataSize - Delta) continue;
            Data = DumpRange(Base, Size, (ULONG64)Entry->Memory.Rva + Delta, 16);
            ok(Data != NULL, "Captured memory is outside the dump\n");
            if (!Data) continue;
            Found = TRUE;
            for (Byte = 0; Byte < 16; ++Byte)
                ok(Data[Byte] == 0x41, "Captured byte %lu is %#x instead of 0x41\n", Byte, Data[Byte]);
            break;
        }
    ok(Found, "Snapshot private memory is missing\n");

    Misc = DumpStream(Base, Size, MiscInfoStream, &Length);
    ok(Misc && Length >= sizeof(*Misc), "Missing or truncated process metadata\n");
    if (Misc && Length >= sizeof(*Misc))
    {
        ok((Misc->Flags1 & (MINIDUMP_MISC1_PROCESS_ID | MINIDUMP_MISC1_PROCESS_TIMES)) ==
           (MINIDUMP_MISC1_PROCESS_ID | MINIDUMP_MISC1_PROCESS_TIMES), "Missing process metadata flags %#lx\n", (DWORD)Misc->Flags1);
        ok(Misc->ProcessId == Process->ProcessId, "Dump PID %lu instead of captured PID %lu\n",
           (DWORD)Misc->ProcessId, Process->ProcessId);
        Valid = SystemTimeToFileTime(&Epoch, &EpochTime);
        ok(Valid, "Epoch conversion failed: %lu\n", GetLastError());
        if (Valid)
            ok(Misc->ProcessCreateTime == (FileTimeValue(Process->CreateTime) - FileTimeValue(EpochTime)) / 10000000,
               "Dump creation time %lu does not match captured process\n", (DWORD)Misc->ProcessCreateTime);
        ok(Misc->ProcessKernelTime == FileTimeValue(Process->KernelTime) / 10000000,
           "Dump kernel time %lu does not match captured process\n", (DWORD)Misc->ProcessKernelTime);
        ok(Misc->ProcessUserTime == FileTimeValue(Process->UserTime) / 10000000,
           "Dump user time %lu does not match captured process\n", (DWORD)Misc->ProcessUserTime);
    }
}

static void
CheckSnapshotDump(HPSS Snapshot, DWORD Pid, const struct snapshot_control *Control,
                  const PSS_PROCESS_INFORMATION *Process, BOOL ExpectedSuccess)
{
    struct snapshot_callback State = {0};
    MINIDUMP_CALLBACK_INFORMATION Callback = {SnapshotCallback, &State};
    char Directory[MAX_PATH], Path[MAX_PATH];
    HANDLE File = INVALID_HANDLE_VALUE, Mapping = NULL;
    const BYTE *Base = NULL;
    LARGE_INTEGER Size;
    DWORD Error, Length;
    BOOL Result;

    Length = GetTempPathA(_countof(Directory), Directory);
    ok(Length && Length < _countof(Directory), "GetTempPath failed: %lu\n", GetLastError());
    if (!Length || Length >= _countof(Directory)) return;
    Result = GetTempFileNameA(Directory, "pss", 0, Path) != 0;
    ok(Result, "GetTempFileName failed: %lu\n", GetLastError());
    if (!Result) return;
    File = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFile failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE) goto Cleanup;
    State.Snapshot = Snapshot;
    if (Control) State.Address = Control->Address;
    SetLastError(0xdeadbeef);
    Result = MiniDumpWriteDump((HANDLE)Snapshot, Pid, File, MiniDumpNormal, NULL, NULL, &Callback);
    Error = GetLastError();
    ok(Result == ExpectedSuccess, "Snapshot dump returned %u, error %#lx\n", Result, Error);
    ok(State.Calls[IsProcessSnapshotCallback] == 1 && State.Calls[VmStartCallback] == 1 &&
       State.Calls[IoStartCallback] == 1 && State.Calls[SecondaryFlagsCallback] == 1,
       "Negotiation counts snapshot=%lu vm=%lu io=%lu secondary=%lu\n",
       State.Calls[IsProcessSnapshotCallback], State.Calls[VmStartCallback],
       State.Calls[IoStartCallback], State.Calls[SecondaryFlagsCallback]);
    ok(!State.Calls[ThreadCallback] && !State.Calls[IncludeThreadCallback], "Uncaptured threads were enumerated\n");
    if (!ExpectedSuccess)
    {
        ok(Error == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "Missing clone dump error %#lx\n", Error);
        ok(State.Calls[CancelCallback] == 0, "Missing clone requested cancellation\n");
    }
    if (!GetFileSizeEx(File, &Size))
    {
        ok(0, "GetFileSizeEx failed: %lu\n", GetLastError());
        goto Cleanup;
    }
    if (!ExpectedSuccess) ok(Size.QuadPart == 0, "Failed snapshot dump contains data\n");
    if (!Result || !ExpectedSuccess) goto Cleanup;
    ok(Size.QuadPart > 0 && (ULONGLONG)Size.QuadPart <= (SIZE_T)-1, "Invalid dump size\n");
    if (Size.QuadPart <= 0 || (ULONGLONG)Size.QuadPart > (SIZE_T)-1) goto Cleanup;
    Mapping = CreateFileMappingW(File, NULL, PAGE_READONLY, 0, 0, NULL);
    ok(Mapping != NULL, "Dump mapping creation failed: %lu\n", GetLastError());
    if (!Mapping) goto Cleanup;
    Base = MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
    ok(Base != NULL, "Dump mapping failed: %lu\n", GetLastError());
    if (Base) CheckDumpContents(Base, (SIZE_T)Size.QuadPart, Control, Process);
Cleanup:
    if (Base) ok(UnmapViewOfFile(Base), "Dump unmap failed: %lu\n", GetLastError());
    CloseTestHandle(Mapping);
    CloseTestHandle(File);
    ok(DeleteFileA(Path), "Dump deletion failed: %lu\n", GetLastError());
}

START_TEST(MiniDumpSnapshot)
{
    char **Arguments, Application[MAX_PATH], CommandLine[2 * MAX_PATH];
    int Count;
    SECURITY_ATTRIBUTES Security = {sizeof(Security), NULL, TRUE};
    struct snapshot_control *Control = NULL;
    HANDLE Mapping = NULL, Ready = NULL, Stop = NULL;
    PROCESS_INFORMATION Child = {0};
    STARTUPINFOA Startup = {0};
    PSS_PROCESS_INFORMATION Process;
    PSS_CAPTURE Capture;
    PSS_QUERY Query;
    PSS_FREE Free;
    HPSS Snapshot = NULL;
    DWORD Error, Length, Wait, ExitCode = STILL_ACTIVE;
    BYTE Mutation[16];
    SIZE_T Written;
    BOOL Result;

    Count = winetest_get_mainargs(&Arguments);
    if (Count == 4 && !strcmp(Arguments[2], "child"))
        ExitProcess(SnapshotChild((HANDLE)(ULONG_PTR)_strtoui64(Arguments[3], NULL, 16)));
    Capture = (PSS_CAPTURE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PssCaptureSnapshot");
    Query = (PSS_QUERY)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PssQuerySnapshot");
    Free = (PSS_FREE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "PssFreeSnapshot");
    ok(Capture && Query && Free, "Process snapshot APIs are missing\n");
    if (!Capture || !Query || !Free) return;
    Error = Capture(GetCurrentProcess(), PSS_CAPTURE_NONE, 0, &Snapshot);
    ok(Error == ERROR_SUCCESS && Snapshot, "Empty snapshot capture returned %lu\n", Error);
    if (!Error && Snapshot)
    {
        CheckSnapshotDump(Snapshot, GetCurrentProcessId(), NULL, NULL, FALSE);
        ok(Free(GetCurrentProcess(), Snapshot) == ERROR_SUCCESS, "Empty snapshot release failed\n");
        Snapshot = NULL;
    }
    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &Security, PAGE_READWRITE, 0, sizeof(*Control), NULL);
    Ready = CreateEventW(&Security, TRUE, FALSE, NULL);
    Stop = CreateEventW(&Security, TRUE, FALSE, NULL);
    ok(Mapping && Ready && Stop, "Child setup failed: %lu\n", GetLastError());
    if (!Mapping || !Ready || !Stop) goto Cleanup;
    Control = MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*Control));
    ok(Control != NULL, "Control mapping failed: %lu\n", GetLastError());
    if (!Control) goto Cleanup;
    Control->Ready = Ready;
    Control->Stop = Stop;
    Length = GetModuleFileNameA(NULL, Application, _countof(Application));
    ok(Length && Length < _countof(Application), "GetModuleFileName failed: %lu\n", GetLastError());
    if (!Length || Length >= _countof(Application)) goto Cleanup;
    Count = _snprintf(CommandLine, sizeof(CommandLine), "\"%s\" MiniDumpSnapshot child %I64x",
                      Application, (ULONGLONG)(ULONG_PTR)Mapping);
    ok(Count > 0 && (SIZE_T)Count < sizeof(CommandLine), "Child command line was truncated\n");
    if (Count <= 0 || (SIZE_T)Count >= sizeof(CommandLine)) goto Cleanup;
    Startup.cb = sizeof(Startup);
    Result = CreateProcessA(Application, CommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &Startup, &Child);
    ok(Result, "CreateProcess failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    Wait = WaitForSingleObject(Ready, 5000);
    ok(Wait == WAIT_OBJECT_0, "Child ready wait %#lx\n", Wait);
    if (Wait != WAIT_OBJECT_0) goto Cleanup;
    ok(Control->Error == 0 && Control->Address && Control->Image, "Child setup error %lu\n", Control->Error);
    if (Control->Error || !Control->Address || !Control->Image) goto Cleanup;
    Error = Capture(Child.hProcess, PSS_CAPTURE_VA_CLONE, 0, &Snapshot);
    ok(Error == ERROR_SUCCESS && Snapshot, "Clone snapshot capture returned %lu\n", Error);
    if (Error || !Snapshot) goto Cleanup;
    Error = Query(Snapshot, PSS_QUERY_PROCESS_INFORMATION, &Process, sizeof(Process));
    ok(Error == ERROR_SUCCESS, "Captured process query returned %lu\n", Error);
    if (Error) goto Cleanup;
    memset(Mutation, 0x44, sizeof(Mutation));
    Written = 0;
    Result = WriteProcessMemory(Child.hProcess, (void *)Control->Address, Mutation, sizeof(Mutation), &Written);
    ok(Result && Written == sizeof(Mutation), "Source mutation failed: %lu, written %I64u\n", GetLastError(), (ULONGLONG)Written);
    if (!Result || Written != sizeof(Mutation)) goto Cleanup;
    CheckSnapshotDump(Snapshot, Child.dwProcessId, Control, &Process, TRUE);
    Result = SetEvent(Stop);
    ok(Result, "Child stop signal failed: %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    Wait = WaitForSingleObject(Child.hProcess, 5000);
    ok(Wait == WAIT_OBJECT_0, "Child exit wait %#lx\n", Wait);
    if (Wait != WAIT_OBJECT_0) goto Cleanup;
    CheckSnapshotDump(Snapshot, Child.dwProcessId, Control, &Process, TRUE);
Cleanup:
    if (Snapshot) ok(Free(GetCurrentProcess(), Snapshot) == ERROR_SUCCESS, "Clone snapshot release failed\n");
    if (Stop) ok(SetEvent(Stop), "Cleanup child signal failed: %lu\n", GetLastError());
    if (Child.hProcess)
    {
        Wait = WaitForSingleObject(Child.hProcess, 5000);
        ok(Wait == WAIT_OBJECT_0, "Cleanup child wait %#lx\n", Wait);
        if (Wait != WAIT_OBJECT_0)
        {
            ok(TerminateProcess(Child.hProcess, ERROR_TIMEOUT), "Child termination failed: %lu\n", GetLastError());
            ok(WaitForSingleObject(Child.hProcess, 5000) == WAIT_OBJECT_0, "Terminated child did not exit\n");
        }
        Result = GetExitCodeProcess(Child.hProcess, &ExitCode);
        ok(Result && ExitCode == 0, "Child completion failed: result %u, exit %lu\n", Result, ExitCode);
    }
    CloseTestHandle(Child.hThread);
    CloseTestHandle(Child.hProcess);
    if (Control) ok(UnmapViewOfFile(Control), "Control unmap failed: %lu\n", GetLastError());
    CloseTestHandle(Stop);
    CloseTestHandle(Ready);
    CloseTestHandle(Mapping);
}
