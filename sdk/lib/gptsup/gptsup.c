/*
 * PROJECT:     LakoOS GPT Support Library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Identifiers and default names of new GPT partitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#define NTOS_MODE_USER
#include <ndk/rtlfuncs.h>
#include <initguid.h>
#include <diskguid.h>
#include <csprng.h>
#include <gptsup.h>

BOOLEAN
NTAPI
GptCreatePartitionId(
    _Out_ GUID* PartitionId)
{
    if (!RosCsprngFill(PartitionId, sizeof(*PartitionId)))
        return FALSE;

    PartitionId->Data3 = (PartitionId->Data3 & 0x0FFF) | 0x4000;
    PartitionId->Data4[0] = (PartitionId->Data4[0] & 0x3F) | 0x80;
    return TRUE;
}

PCWSTR
NTAPI
GptDefaultPartitionName(
    _In_ const GUID* PartitionType)
{
    if (IsEqualGUID(PartitionType, &PARTITION_SYSTEM_GUID))
        return L"EFI system partition";
    if (IsEqualGUID(PartitionType, &PARTITION_MSFT_RESERVED_GUID))
        return L"Microsoft reserved partition";
    if (IsEqualGUID(PartitionType, &PARTITION_BASIC_DATA_GUID))
        return L"Basic data partition";
    return L"";
}
