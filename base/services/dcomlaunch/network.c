/*
 * PROJECT:     ReactOS RPC Subsystem Service
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     UUID initialization.
 * COPYRIGHT:   Copyright 2019 Pierre Schweitzer
 */

/* INCLUDES *****************************************************************/

/* PSDK/NDK Headers */
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winsvc.h>
#include <ntddndis.h>
#include <iphlpapi.h>

#include <ndk/exfuncs.h>
#include <ntsecapi.h>

#define NDEBUG
#include <debug.h>

#define SEED_BUFFER_SIZE 6

/* FUNCTIONS ****************************************************************/

typedef DWORD (WINAPI *PGET_ADAPTERS_INFO)(PIP_ADAPTER_INFO, PULONG);

static BOOLEAN
getMacAddress(UCHAR * MacAddress)
{
    PGET_ADAPTERS_INFO pGetAdaptersInfo;
    PIP_ADAPTER_INFO Adapters, Adapter;
    HMODULE hIpHlpApi;
    ULONG Size = 0, Index;
    BOOLEAN Found = FALSE;

    hIpHlpApi = LoadLibraryW(L"iphlpapi.dll");
    if (hIpHlpApi == NULL)
        return FALSE;

    pGetAdaptersInfo = (PGET_ADAPTERS_INFO)GetProcAddress(hIpHlpApi, "GetAdaptersInfo");
    if (pGetAdaptersInfo == NULL ||
        pGetAdaptersInfo(NULL, &Size) != ERROR_BUFFER_OVERFLOW || Size == 0)
    {
        FreeLibrary(hIpHlpApi);
        return FALSE;
    }

    Adapters = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Adapters != NULL && pGetAdaptersInfo(Adapters, &Size) == ERROR_SUCCESS)
    {
        for (Adapter = Adapters; Adapter != NULL && !Found; Adapter = Adapter->Next)
        {
            BOOLEAN NonZero = FALSE;

            if (Adapter->AddressLength != SEED_BUFFER_SIZE)
                continue;
            if (Adapter->Type != MIB_IF_TYPE_ETHERNET && Adapter->Type != IF_TYPE_IEEE80211)
                continue;
            for (Index = 0; Index < SEED_BUFFER_SIZE; Index++)
            {
                if (Adapter->Address[Index] != 0)
                    NonZero = TRUE;
            }
            if (!NonZero)
                continue;

            memcpy(MacAddress, Adapter->Address, SEED_BUFFER_SIZE);
            Found = TRUE;
        }
    }

    if (Adapters != NULL)
        HeapFree(GetProcessHeap(), 0, Adapters);
    FreeLibrary(hIpHlpApi);
    return Found;
}

static VOID
CookupNodeId(UCHAR * NodeId)
{
    CHAR ComputerName[MAX_COMPUTERNAME_LENGTH + 1];
    CHAR * CurrentChar;
    DWORD Size;
    LARGE_INTEGER PerformanceCount;
    PDWORD NodeBegin, NodeMiddle;
    DWORD dwValue;
    MEMORYSTATUS MemoryStatus;
    LUID Luid;
    WCHAR szSystem[MAX_PATH];
    ULARGE_INTEGER FreeBytesToCaller, TotalBytes, TotalFreeBytes;

    /* Initialize node id */
    memset(NodeId, 0x11, SEED_BUFFER_SIZE * sizeof(UCHAR));

    /* Randomize it with computer name */
    Size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(ComputerName, &Size))
    {
        Size = 0;
        CurrentChar = &ComputerName[0];
        while (*CurrentChar != ANSI_NULL)
        {
            NodeId[Size] ^= *CurrentChar;
            ++CurrentChar;

            /* Don't overflow our NodeId */
            if (++Size >= SEED_BUFFER_SIZE)
            {
                Size = 0;
            }
        }
    }

    /* Now, we'll work directly on DWORD values */
    NodeBegin = (DWORD *)&NodeId[0];
    NodeMiddle = (DWORD *)&NodeId[2];

    /* Randomize with performance counter */
    if (QueryPerformanceCounter(&PerformanceCount))
    {
        *NodeMiddle = *NodeMiddle ^ PerformanceCount.u.HighPart ^ PerformanceCount.u.LowPart;

        dwValue = PerformanceCount.u.HighPart ^ PerformanceCount.u.LowPart ^ *NodeBegin;
    }
    else
    {
        dwValue = *NodeBegin;
    }

    *NodeBegin = *NodeBegin ^ dwValue;
    *NodeMiddle = *NodeMiddle ^ *NodeBegin;

    /* Then, with memory status */
    MemoryStatus.dwLength = sizeof(MemoryStatus);
    GlobalMemoryStatus(&MemoryStatus);

    *NodeBegin = *NodeBegin ^ MemoryStatus.dwMemoryLoad;
    *NodeMiddle = *NodeMiddle ^ MemoryStatus.dwTotalPhys;
    *NodeBegin = *NodeBegin ^ MemoryStatus.dwTotalPageFile ^ MemoryStatus.dwAvailPhys;
    *NodeMiddle = *NodeMiddle ^ MemoryStatus.dwTotalVirtual ^ MemoryStatus.dwAvailPageFile;
    *NodeBegin = *NodeBegin ^ MemoryStatus.dwAvailVirtual;

    /* With a LUID */
    if (AllocateLocallyUniqueId(&Luid))
    {
        *NodeBegin = *NodeBegin ^ Luid.LowPart;
        *NodeMiddle = *NodeMiddle ^ Luid.HighPart;
    }

    /* Get system directory */
    GetSystemDirectoryW(szSystem, ARRAYSIZE(szSystem));

    /* And finally with free disk space */
    if (GetDiskFreeSpaceExW(szSystem, &FreeBytesToCaller, &TotalBytes, &TotalFreeBytes))
    {
        *NodeMiddle ^= FreeBytesToCaller.LowPart ^ TotalFreeBytes.HighPart;
        *NodeMiddle ^= FreeBytesToCaller.HighPart ^ TotalFreeBytes.LowPart;
        *NodeBegin ^= TotalBytes.LowPart ^ TotalFreeBytes.LowPart;
        *NodeBegin ^= TotalBytes.HighPart ^ TotalFreeBytes.HighPart;
    }

    /*
     * Because it was locally generated, force the seed to be local
     * setting this, will trigger the check for validness in the kernel
     * and make the seed local
     */
    NodeId[0] |= 0x80u;
}

VOID DealWithDeviceEvent(VOID)
{
    NTSTATUS Status;
    UCHAR UuidSeed[SEED_BUFFER_SIZE];

    /* First, try to get a multicast MAC address */
    if (!getMacAddress(UuidSeed))
    {
        DPRINT("Failed finding a proper MAC address, will generate seed\n");
        CookupNodeId(UuidSeed);
    }

    /* Seed our UUID generator */
    Status = NtSetUuidSeed(UuidSeed);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NtSetUuidSeed failed with status: %lx\n", Status);
    }
}
