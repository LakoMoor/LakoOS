/*
 * PROJECT:     LakoOS CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RISC-V64 RVA20 memcmp for kernel and native code
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <string.h>
#include "rvstring.h"

int __cdecl
memcmp(const void *First, const void *Second, size_t Length)
{
    return RvMemcmpRva20(First, Second, Length);
}
