/*
 * PROJECT:     LakoOS Win32 Base API
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RaiseException for architectures without an assembly version
 * COPYRIGHT:   Copyright 1998 Ariadne (ariadne@xs4all.nl)
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>

#define NDEBUG
#include <debug.h>

/*
 * @implemented
 */
VOID
WINAPI
RaiseException(
    _In_ DWORD dwExceptionCode,
    _In_ DWORD dwExceptionFlags,
    _In_ DWORD nNumberOfArguments,
    _In_opt_ const ULONG_PTR *lpArguments)
{
    EXCEPTION_RECORD ExceptionRecord;

    /* Setup the exception record */
    RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
    ExceptionRecord.ExceptionCode = dwExceptionCode;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.ExceptionAddress = (PVOID)RaiseException;
    ExceptionRecord.ExceptionFlags = dwExceptionFlags & EXCEPTION_NONCONTINUABLE;

    /* Check if we have arguments */
    if (!lpArguments)
    {
        /* We don't */
        ExceptionRecord.NumberParameters = 0;
    }
    else
    {
        /* We do, normalize the count */
        if (nNumberOfArguments > EXCEPTION_MAXIMUM_PARAMETERS)
            nNumberOfArguments = EXCEPTION_MAXIMUM_PARAMETERS;

        /* Set the count of parameters and copy them */
        ExceptionRecord.NumberParameters = nNumberOfArguments;
        RtlCopyMemory(ExceptionRecord.ExceptionInformation,
                      lpArguments,
                      nNumberOfArguments * sizeof(ULONG_PTR));
    }

    /* Better handling of Delphi Exceptions... a ReactOS Hack */
    if (dwExceptionCode == 0xeedface || dwExceptionCode == 0xeedfade)
    {
        DPRINT1("Delphi Exception at address: %p\n", ExceptionRecord.ExceptionInformation[0]);
        DPRINT1("Exception-Object: %p\n", ExceptionRecord.ExceptionInformation[1]);
        DPRINT1("Exception text: %lx\n", ExceptionRecord.ExceptionInformation[2]);
    }

    /* Raise the exception */
    RtlRaiseException(&ExceptionRecord);
}
