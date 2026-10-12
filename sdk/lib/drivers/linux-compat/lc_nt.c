/*
 * PROJECT:     LakoOS Linux kernel compatibility library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NT kernel services behind the Linux compatibility layer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ntstrsafe.h>
#include <pseh/pseh2.h>
#include <stdio.h>

#include "include/lc/nt.h"

#define LC_NT_TAG 'xuNL'
#define LC_NT_MAX_CPUS 64

NTHALAPI BOOLEAN NTAPI HalEnableSystemInterrupt(ULONG Vector, KIRQL Irql, KINTERRUPT_MODE InterruptMode);
NTHALAPI VOID NTAPI HalDisableSystemInterrupt(ULONG Vector, KIRQL Irql);

typedef struct _LC_NT_SPINLOCK
{
    KSPIN_LOCK Lock;
    KIRQL OldIrql;
} LC_NT_SPINLOCK;

typedef struct _LC_NT_TIMER
{
    KTIMER Timer;
    KDPC Dpc;
    void (*Routine)(void *);
    void *Context;
} LC_NT_TIMER;

typedef struct _LC_NT_THREAD_START
{
    void (*Routine)(void *);
    void *Context;
} LC_NT_THREAD_START;

typedef struct _LC_NT_RCU_BARRIER
{
    KEVENT Done;
    volatile LONG Remaining;
    KDPC Dpc[LC_NT_MAX_CPUS];
} LC_NT_RCU_BARRIER;

C_ASSERT(sizeof(LC_NT_SPINLOCK) <= LC_NT_SPINLOCK_SIZE);
C_ASSERT(sizeof(KEVENT) <= LC_NT_EVENT_SIZE);
C_ASSERT(sizeof(LC_NT_TIMER) <= LC_NT_TIMER_SIZE);

static LARGE_INTEGER LcNtPerformanceFrequency;
static ULONG LcNtRandomSeed;
static ULONG LcNtRcuNesting[LC_NT_MAX_CPUS];
static KIRQL LcNtRcuSavedIrql[LC_NT_MAX_CPUS];

void
lc_nt_spin_init(lc_nt_spinlock *lock)
{
    LC_NT_SPINLOCK *Lock = (LC_NT_SPINLOCK *)lock;

    KeInitializeSpinLock(&Lock->Lock);
    Lock->OldIrql = PASSIVE_LEVEL;
}

void
lc_nt_spin_acquire(lc_nt_spinlock *lock)
{
    LC_NT_SPINLOCK *Lock = (LC_NT_SPINLOCK *)lock;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Lock->Lock, &OldIrql);
    Lock->OldIrql = OldIrql;
}

void
lc_nt_spin_release(lc_nt_spinlock *lock)
{
    LC_NT_SPINLOCK *Lock = (LC_NT_SPINLOCK *)lock;

    KeReleaseSpinLock(&Lock->Lock, Lock->OldIrql);
}

void
lc_nt_event_init(lc_nt_event *event, int synchronization, int signaled)
{
    KeInitializeEvent((PKEVENT)event,
                      synchronization ? SynchronizationEvent : NotificationEvent,
                      signaled ? TRUE : FALSE);
}

void
lc_nt_event_set(lc_nt_event *event)
{
    KeSetEvent((PKEVENT)event, IO_NO_INCREMENT, FALSE);
}

void
lc_nt_event_clear(lc_nt_event *event)
{
    KeClearEvent((PKEVENT)event);
}

int
lc_nt_event_wait(lc_nt_event *event, int64_t timeout_100ns)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    if (timeout_100ns < 0)
        return KeWaitForSingleObject(event, Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS;

    Timeout.QuadPart = -timeout_100ns;
    Status = KeWaitForSingleObject(event, Executive, KernelMode, FALSE, &Timeout);
    return Status == STATUS_SUCCESS;
}

void *
lc_nt_current_thread(void)
{
    return KeGetCurrentThread();
}

static void (*LcNtThreadExitCallback)(void *);

static VOID
NTAPI
LcNtThreadNotify(_In_ HANDLE ProcessId, _In_ HANDLE ThreadId, _In_ BOOLEAN Create)
{
    UNREFERENCED_PARAMETER(ProcessId);

    if (!Create && LcNtThreadExitCallback)
        LcNtThreadExitCallback(ThreadId);
}

void *
lc_nt_current_thread_id(void)
{
    return PsGetCurrentThreadId();
}

int
lc_nt_set_thread_exit_callback(void (*callback)(void *thread_id))
{
    NTSTATUS Status;

    PAGED_CODE();
    LcNtThreadExitCallback = callback;
    Status = PsSetCreateThreadNotifyRoutine(LcNtThreadNotify);
    if (!NT_SUCCESS(Status))
    {
        LcNtThreadExitCallback = NULL;
        return -12;
    }
    return 0;
}

void
lc_nt_clear_thread_exit_callback(void)
{
    PAGED_CODE();
    PsRemoveCreateThreadNotifyRoutine(LcNtThreadNotify);
    LcNtThreadExitCallback = NULL;
}

static KSTART_ROUTINE LcNtThreadStart;

static VOID
NTAPI
LcNtThreadStart(_In_ PVOID StartContext)
{
    LC_NT_THREAD_START Start = *(LC_NT_THREAD_START *)StartContext;

    ExFreePoolWithTag(StartContext, LC_NT_TAG);
    Start.Routine(Start.Context);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

int
lc_nt_create_thread(void (*routine)(void *), void *context, void **handle)
{
    LC_NT_THREAD_START *Start;
    HANDLE ThreadHandle;
    PVOID ThreadObject;
    NTSTATUS Status;

    Start = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Start), LC_NT_TAG);
    if (!Start)
        return -12;
    Start->Routine = routine;
    Start->Context = context;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, LcNtThreadStart, Start);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Start, LC_NT_TAG);
        return -12;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle, SYNCHRONIZE, *PsThreadType, KernelMode, &ThreadObject, NULL);
    ZwClose(ThreadHandle);
    if (!NT_SUCCESS(Status))
        return -22;

    *handle = ThreadObject;
    return 0;
}

void
lc_nt_wait_thread(void *handle)
{
    KeWaitForSingleObject(handle, Executive, KernelMode, FALSE, NULL);
    ObDereferenceObject(handle);
}

void
lc_nt_yield(void)
{
    ZwYieldExecution();
}

int
lc_nt_at_passive(void)
{
    return KeGetCurrentIrql() == PASSIVE_LEVEL;
}

int
lc_nt_at_dispatch_or_above(void)
{
    return KeGetCurrentIrql() >= DISPATCH_LEVEL;
}

unsigned int
lc_nt_processor_count(void)
{
    return KeQueryActiveProcessorCount(NULL);
}

static KDEFERRED_ROUTINE LcNtTimerDpc;

static VOID
NTAPI
LcNtTimerDpc(_In_ PKDPC Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2)
{
    LC_NT_TIMER *Timer = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Timer->Routine(Timer->Context);
}

void
lc_nt_timer_init(lc_nt_timer *timer, void (*routine)(void *), void *context)
{
    LC_NT_TIMER *Timer = (LC_NT_TIMER *)timer;

    KeInitializeTimer(&Timer->Timer);
    KeInitializeDpc(&Timer->Dpc, LcNtTimerDpc, Timer);
    Timer->Routine = routine;
    Timer->Context = context;
}

void
lc_nt_timer_set(lc_nt_timer *timer, int64_t due_100ns)
{
    LC_NT_TIMER *Timer = (LC_NT_TIMER *)timer;
    LARGE_INTEGER DueTime;

    DueTime.QuadPart = due_100ns > 0 ? -due_100ns : -1;
    KeSetTimer(&Timer->Timer, DueTime, &Timer->Dpc);
}

int
lc_nt_timer_cancel(lc_nt_timer *timer)
{
    LC_NT_TIMER *Timer = (LC_NT_TIMER *)timer;
    BOOLEAN Removed;

    Removed = KeCancelTimer(&Timer->Timer);
    if (!Removed)
        Removed = KeRemoveQueueDpc(&Timer->Dpc);
    return Removed ? 1 : 0;
}

uint64_t
lc_nt_time_ns(void)
{
    LARGE_INTEGER Counter, Frequency;
    uint64_t Seconds, Remainder;

    Counter = KeQueryPerformanceCounter(&Frequency);
    if (!Frequency.QuadPart)
        return 0;
    Seconds = (uint64_t)Counter.QuadPart / (uint64_t)Frequency.QuadPart;
    Remainder = (uint64_t)Counter.QuadPart % (uint64_t)Frequency.QuadPart;
    return Seconds * 1000000000ULL + Remainder * 1000000000ULL / (uint64_t)Frequency.QuadPart;
}

void
lc_nt_stall_us(uint32_t us)
{
    while (us > 1000)
    {
        KeStallExecutionProcessor(1000);
        us -= 1000;
    }
    KeStallExecutionProcessor(us);
}

void
lc_nt_sleep_100ns(int64_t interval)
{
    LARGE_INTEGER Interval;

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        lc_nt_stall_us((uint32_t)((interval + 9) / 10));
        return;
    }
    Interval.QuadPart = interval > 0 ? -interval : -1;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

void *
lc_nt_alloc(size_t size, int zero)
{
    void *Block;

    Block = ExAllocatePoolWithTag(NonPagedPool, size ? size : 1, LC_NT_TAG);
    if (Block && zero)
        RtlZeroMemory(Block, size);
    return Block;
}

void
lc_nt_free(void *ptr)
{
    if (ptr)
        ExFreePoolWithTag(ptr, LC_NT_TAG);
}

static MEMORY_CACHING_TYPE LcNtCacheType(int cache);

void *
lc_nt_alloc_pages(size_t size, uint64_t highest_address, int cache, uint64_t *pfns, size_t pfn_count)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PPFN_NUMBER Pfn;
    PMDL Mdl;
    size_t Index;

    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)highest_address;
    Skip.QuadPart = 0;
    Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, size, LcNtCacheType(cache), MM_ALLOCATE_FULLY_REQUIRED);
    if (!Mdl)
        return NULL;
    if (MmGetMdlByteCount(Mdl) < size)
    {
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
        return NULL;
    }

    Pfn = MmGetMdlPfnArray(Mdl);
    for (Index = 0; Index < pfn_count; ++Index)
        pfns[Index] = Pfn[Index];
    return Mdl;
}

void
lc_nt_free_pages(void *allocation)
{
    PMDL Mdl = allocation;

    if (!Mdl)
        return;
    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

static MEMORY_CACHING_TYPE
LcNtCacheType(int cache)
{
    switch (cache)
    {
        case LC_NT_CACHE_UNCACHED:
            return MmNonCached;
        case LC_NT_CACHE_WRITECOMBINED:
            return MmWriteCombined;
        default:
            return MmCached;
    }
}

void *
lc_nt_map_pfns(const uint64_t *pfns, size_t count, int cache, int user, void **map_cookie)
{
    PPFN_NUMBER Pfn;
    PVOID Address = NULL;
    SIZE_T MdlSize;
    PMDL Mdl;
    size_t Index;

    if (!count || count > (MAXULONG >> PAGE_SHIFT))
        return NULL;

    MdlSize = MmSizeOfMdl(NULL, count << PAGE_SHIFT);
    Mdl = ExAllocatePoolWithTag(NonPagedPool, MdlSize, LC_NT_TAG);
    if (!Mdl)
        return NULL;
    MmInitializeMdl(Mdl, NULL, count << PAGE_SHIFT);
    Pfn = MmGetMdlPfnArray(Mdl);
    for (Index = 0; Index < count; ++Index)
        Pfn[Index] = (PFN_NUMBER)pfns[Index];
    Mdl->MdlFlags |= MDL_PAGES_LOCKED;

    if (user)
    {
        _SEH2_TRY
        {
            Address = MmMapLockedPagesSpecifyCache(Mdl, UserMode, LcNtCacheType(cache), NULL, FALSE, NormalPagePriority);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Address = NULL;
        }
        _SEH2_END;
    }
    else
    {
        Address = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, LcNtCacheType(cache), NULL, FALSE, NormalPagePriority);
    }

    if (!Address)
    {
        ExFreePoolWithTag(Mdl, LC_NT_TAG);
        return NULL;
    }

    *map_cookie = Mdl;
    return Address;
}

void
lc_nt_unmap_pfns(void *va, void *map_cookie)
{
    PMDL Mdl = map_cookie;

    if (!va || !Mdl)
        return;
    MmUnmapLockedPages(va, Mdl);
    ExFreePoolWithTag(Mdl, LC_NT_TAG);
}

void *
lc_nt_map_io(uint64_t phys, size_t size, int cache)
{
    PHYSICAL_ADDRESS Address;

    Address.QuadPart = (LONGLONG)phys;
    return MmMapIoSpace(Address, size, LcNtCacheType(cache));
}

void
lc_nt_unmap_io(void *va, size_t size)
{
    if (va)
        MmUnmapIoSpace(va, size);
}

int
lc_nt_copy_from_user(void *dst, const void *src, size_t size)
{
    int Result = 0;

    if (!size)
        return 0;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead((PVOID)src, size, 1);
        RtlCopyMemory(dst, src, size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = -14;
    }
    _SEH2_END;
    return Result;
}

int
lc_nt_copy_to_user(void *dst, const void *src, size_t size)
{
    int Result = 0;

    if (!size)
        return 0;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(dst, size, 1);
        RtlCopyMemory(dst, src, size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = -14;
    }
    _SEH2_END;
    return Result;
}

int
lc_nt_clear_user(void *dst, size_t size)
{
    int Result = 0;

    if (!size)
        return 0;
    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(dst, size, 1);
        RtlZeroMemory(dst, size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = -14;
    }
    _SEH2_END;
    return Result;
}

int
lc_nt_read_file(const char *name, void **data, size_t *size)
{
    WCHAR PathBuffer[260];
    FILE_STANDARD_INFORMATION Standard;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Path;
    IO_STATUS_BLOCK Io;
    LARGE_INTEGER Offset;
    HANDLE File;
    NTSTATUS Status;
    PVOID Buffer;
    SIZE_T Length;
    size_t Index;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return -22;

    Status = RtlStringCbCopyW(PathBuffer, sizeof(PathBuffer), L"\\SystemRoot\\System32\\drivers\\");
    if (!NT_SUCCESS(Status))
        return -22;
    Length = wcslen(PathBuffer);
    for (Index = 0; name[Index]; ++Index)
    {
        if (Length + 1 >= RTL_NUMBER_OF(PathBuffer))
            return -36;
        PathBuffer[Length++] = name[Index] == '/' ? L'\\' : (WCHAR)(UCHAR)name[Index];
    }
    PathBuffer[Length] = UNICODE_NULL;

    RtlInitUnicodeString(&Path, PathBuffer);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&File, GENERIC_READ | SYNCHRONIZE, &Attributes, &Io, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);
    if (!NT_SUCCESS(Status))
        return -2;

    Status = ZwQueryInformationFile(File, &Io, &Standard, sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status) || Standard.EndOfFile.HighPart || !Standard.EndOfFile.LowPart)
    {
        ZwClose(File);
        return -5;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Standard.EndOfFile.LowPart, LC_NT_TAG);
    if (!Buffer)
    {
        ZwClose(File);
        return -12;
    }

    Offset.QuadPart = 0;
    Status = ZwReadFile(File, NULL, NULL, NULL, &Io, Buffer, Standard.EndOfFile.LowPart, &Offset, NULL);
    ZwClose(File);
    if (!NT_SUCCESS(Status) || Io.Information != Standard.EndOfFile.LowPart)
    {
        ExFreePoolWithTag(Buffer, LC_NT_TAG);
        return -5;
    }

    *data = Buffer;
    *size = Standard.EndOfFile.LowPart;
    return 0;
}

void
lc_nt_free_file(void *data)
{
    if (data)
        ExFreePoolWithTag(data, LC_NT_TAG);
}

void
lc_nt_irq_mask(uint32_t vector, uint8_t irql)
{
    HalDisableSystemInterrupt(vector, irql);
}

void
lc_nt_irq_unmask(uint32_t vector, uint8_t irql, int level_sensitive)
{
    HalEnableSystemInterrupt(vector, irql, level_sensitive ? LevelSensitive : Latched);
}

static KDEFERRED_ROUTINE LcNtRcuBarrierDpc;

static VOID
NTAPI
LcNtRcuBarrierDpc(_In_ PKDPC Dpc, _In_opt_ PVOID DeferredContext, _In_opt_ PVOID SystemArgument1, _In_opt_ PVOID SystemArgument2)
{
    LC_NT_RCU_BARRIER *Barrier = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (InterlockedDecrement(&Barrier->Remaining) == 0)
        KeSetEvent(&Barrier->Done, IO_NO_INCREMENT, FALSE);
}

void
lc_nt_rcu_synchronize(void)
{
    LC_NT_RCU_BARRIER *Barrier;
    ULONG Count, Index;

    Count = KeQueryActiveProcessorCount(NULL);
    if (Count > LC_NT_MAX_CPUS)
        Count = LC_NT_MAX_CPUS;

    Barrier = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Barrier), LC_NT_TAG);
    if (!Barrier)
    {
        LARGE_INTEGER Interval;

        Interval.QuadPart = -10 * 1000 * 10;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        return;
    }

    KeInitializeEvent(&Barrier->Done, NotificationEvent, FALSE);
    Barrier->Remaining = (LONG)Count;
    for (Index = 0; Index < Count; ++Index)
    {
        KeInitializeDpc(&Barrier->Dpc[Index], LcNtRcuBarrierDpc, Barrier);
        KeSetTargetProcessorDpc(&Barrier->Dpc[Index], (CCHAR)Index);
        KeInsertQueueDpc(&Barrier->Dpc[Index], NULL, NULL);
    }
    KeWaitForSingleObject(&Barrier->Done, Executive, KernelMode, FALSE, NULL);
    ExFreePoolWithTag(Barrier, LC_NT_TAG);
}

void
lc_nt_rcu_read_lock(void)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    ULONG Cpu;

    if (OldIrql < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Cpu = KeGetCurrentProcessorNumber();
    if (Cpu >= LC_NT_MAX_CPUS)
        return;
    if (LcNtRcuNesting[Cpu]++ == 0)
        LcNtRcuSavedIrql[Cpu] = OldIrql;
}

void
lc_nt_rcu_read_unlock(void)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();

    if (Cpu >= LC_NT_MAX_CPUS || !LcNtRcuNesting[Cpu])
        return;
    if (--LcNtRcuNesting[Cpu] == 0)
        KeLowerIrql(LcNtRcuSavedIrql[Cpu]);
}

void
lc_nt_memory_barrier(void)
{
    KeMemoryBarrier();
}

uint32_t
lc_nt_read32(volatile void *address)
{
    return READ_REGISTER_ULONG((volatile ULONG *)address);
}

uint64_t
lc_nt_read64(volatile void *address)
{
    uint64_t Value;

    KeMemoryBarrier();
    Value = *(volatile uint64_t *)address;
    KeMemoryBarrier();
    return Value;
}

void
lc_nt_write32(volatile void *address, uint32_t value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)address, value);
}

void
lc_nt_write64(volatile void *address, uint64_t value)
{
    KeMemoryBarrier();
    *(volatile uint64_t *)address = value;
    KeMemoryBarrier();
}

void
lc_nt_log(int level, const char *text)
{
    if (level > 6)
        return;
    DbgPrint("%s", text);
}

void
lc_nt_bug(const char *file, int line)
{
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, (ULONG_PTR)STATUS_ASSERTION_FAILURE, (ULONG_PTR)file, (ULONG_PTR)line, 0);
}

int
lc_nt_in_interrupt(void)
{
    return KeGetCurrentIrql() > DISPATCH_LEVEL;
}

int
lc_nt_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    return _vsnprintf(buf, size, fmt, args);
}

uint32_t
lc_nt_random(void)
{
    if (!LcNtRandomSeed)
        LcNtRandomSeed = (ULONG)KeQueryPerformanceCounter(&LcNtPerformanceFrequency).LowPart | 1;
    return RtlRandomEx(&LcNtRandomSeed);
}

uint32_t
lc_nt_current_pid(void)
{
    return (uint32_t)(ULONG_PTR)PsGetCurrentProcessId();
}
