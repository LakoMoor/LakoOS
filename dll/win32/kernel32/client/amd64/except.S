/*
 * PROJECT:     LakoOS Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RaiseException for the amd64 architecture
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <asm.inc>
#include <ksamd64.inc>

EXTERN RtlRaiseException:PROC

.code64

PUBLIC RaiseException
FUNC RaiseException
    sub rsp, HEX(B8)
    .ALLOCSTACK HEX(B8)
    .ENDPROLOG

    and edx, EXCEPTION_NONCONTINUABLE
    mov [rsp + HEX(20) + ErExceptionCode], ecx
    mov [rsp + HEX(20) + ErExceptionFlags], edx
    mov qword ptr [rsp + HEX(20) + ErExceptionRecord], 0
    lea rax, RaiseException[rip]
    mov [rsp + HEX(20) + ErExceptionAddress], rax

    xor eax, eax
    mov ecx, EXCEPTION_MAXIMUM_PARAMETERS
.ClearArguments:
    mov [rsp + rcx * 8 + HEX(20) + ErExceptionInformation - 8], rax
    dec ecx
    jnz .ClearArguments

    test r9, r9
    jz .StoreCount
    mov eax, r8d
    cmp eax, EXCEPTION_MAXIMUM_PARAMETERS
    jbe .CopyArguments
    mov eax, EXCEPTION_MAXIMUM_PARAMETERS
.CopyArguments:
    xor ecx, ecx
.CopyNext:
    cmp ecx, eax
    jae .StoreCount
    mov rdx, [r9 + rcx * 8]
    mov [rsp + rcx * 8 + HEX(20) + ErExceptionInformation], rdx
    inc ecx
    jmp .CopyNext

.StoreCount:
    mov [rsp + HEX(20) + ErNumberParameters], eax
    lea rcx, [rsp + HEX(20)]
    call RtlRaiseException
    add rsp, HEX(B8)
    ret
ENDFUNC

END
