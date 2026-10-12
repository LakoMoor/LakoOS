/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS system libraries
 * PURPOSE:           System setup
 * FILE:              dll/win32/syssetup/netinstall.c
 * PROGRAMER:         Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include <rpc.h>
#include <winsvc.h>
#include <strsafe.h>

#define NDEBUG
#include <debug.h>

typedef struct _COMPONENT_INFO
{
    PWSTR pszInfPath;
    PWSTR pszInfSection;
    PWSTR pszComponentId;
    PWSTR pszDescription;
    PWSTR pszClassGuid;
    DWORD dwCharacteristics;
} COMPONENT_INFO, *PCOMPONENT_INFO;


/* GLOBALS ******************************************************************/


/* FUNCTIONS ****************************************************************/

static
BOOL
SetServiceString(
    _In_ HKEY hKey,
    _In_ PCWSTR ValueName,
    _In_ DWORD Type,
    _In_ PCWSTR Data,
    _In_ DWORD cbData)
{
    LONG rc = RegSetValueExW(hKey, ValueName, 0, Type, (const BYTE*)Data, cbData);
    SetLastError(rc);
    return (rc == ERROR_SUCCESS);
}

static
BOOL
InstallOfflineService(
    _In_ HINF hInf,
    _In_ PCWSTR ServiceName,
    _In_ PCWSTR ServiceSection)
{
    WCHAR WindowsDirectory[MAX_PATH];
    WCHAR Binary[MAX_PATH];
    WCHAR Buffer[MAX_PATH];
    WCHAR Services[MAX_PATH], Groups[MAX_PATH];
    INFCONTEXT Context;
    INT ServiceType, StartType, ErrorControl;
    DWORD cchDirectory, cchServices = 0, cchGroups = 0, cchField, i, FieldCount;
    HKEY hServiceKey;
    LONG rc;
    BOOL ret = FALSE;

    if (!SetupFindFirstLineW(hInf, ServiceSection, L"ServiceType", &Context) ||
        !SetupGetIntField(&Context, 1, &ServiceType) ||
        !SetupFindFirstLineW(hInf, ServiceSection, L"StartType", &Context) ||
        !SetupGetIntField(&Context, 1, &StartType) ||
        !SetupFindFirstLineW(hInf, ServiceSection, L"ErrorControl", &Context) ||
        !SetupGetIntField(&Context, 1, &ErrorControl) ||
        !SetupGetLineTextW(NULL, hInf, ServiceSection, L"ServiceBinary", Binary, ARRAYSIZE(Binary), NULL))
    {
        return FALSE;
    }

    cchDirectory = GetWindowsDirectoryW(WindowsDirectory, ARRAYSIZE(WindowsDirectory));
    if (cchDirectory != 0 && cchDirectory < ARRAYSIZE(WindowsDirectory) &&
        wcslen(Binary) > cchDirectory && Binary[cchDirectory] == L'\\' &&
        _wcsnicmp(Binary, WindowsDirectory, cchDirectory) == 0)
    {
        StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"%s%s",
                         (ServiceType & SERVICE_WIN32) ? L"%SystemRoot%" : L"\\SystemRoot",
                         &Binary[cchDirectory]);
        StringCchCopyW(Binary, ARRAYSIZE(Binary), Buffer);
    }

    StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), L"SYSTEM\\CurrentControlSet\\Services\\%s", ServiceName);
    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                         Buffer,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         KEY_ALL_ACCESS,
                         NULL,
                         &hServiceKey,
                         NULL);
    if (rc != ERROR_SUCCESS)
    {
        SetLastError(rc);
        return FALSE;
    }

    if (RegSetValueExW(hServiceKey, L"Type", 0, REG_DWORD, (const BYTE*)&ServiceType, sizeof(DWORD)) != ERROR_SUCCESS ||
        RegSetValueExW(hServiceKey, L"Start", 0, REG_DWORD, (const BYTE*)&StartType, sizeof(DWORD)) != ERROR_SUCCESS ||
        RegSetValueExW(hServiceKey, L"ErrorControl", 0, REG_DWORD, (const BYTE*)&ErrorControl, sizeof(DWORD)) != ERROR_SUCCESS ||
        !SetServiceString(hServiceKey, L"ImagePath", REG_EXPAND_SZ, Binary, (DWORD)((wcslen(Binary) + 1) * sizeof(WCHAR))))
    {
        goto done;
    }

    if (!SetupGetLineTextW(NULL, hInf, ServiceSection, L"DisplayName", Buffer, ARRAYSIZE(Buffer), NULL))
        StringCchCopyW(Buffer, ARRAYSIZE(Buffer), ServiceName);
    if (!SetServiceString(hServiceKey, L"DisplayName", REG_SZ, Buffer, (DWORD)((wcslen(Buffer) + 1) * sizeof(WCHAR))))
        goto done;

    if (SetupGetLineTextW(NULL, hInf, ServiceSection, L"Description", Buffer, ARRAYSIZE(Buffer), NULL) &&
        !SetServiceString(hServiceKey, L"Description", REG_SZ, Buffer, (DWORD)((wcslen(Buffer) + 1) * sizeof(WCHAR))))
    {
        goto done;
    }

    if (SetupGetLineTextW(NULL, hInf, ServiceSection, L"LoadOrderGroup", Buffer, ARRAYSIZE(Buffer), NULL) &&
        *Buffer &&
        !SetServiceString(hServiceKey, L"Group", REG_SZ, Buffer, (DWORD)((wcslen(Buffer) + 1) * sizeof(WCHAR))))
    {
        goto done;
    }

    if (SetupGetLineTextW(NULL, hInf, ServiceSection, L"StartName", Buffer, ARRAYSIZE(Buffer), NULL) && *Buffer)
    {
        if (!SetServiceString(hServiceKey, L"ObjectName", REG_SZ, Buffer, (DWORD)((wcslen(Buffer) + 1) * sizeof(WCHAR))))
            goto done;
    }
    else if (ServiceType & SERVICE_WIN32)
    {
        if (!SetServiceString(hServiceKey, L"ObjectName", REG_SZ, L"LocalSystem", sizeof(L"LocalSystem")))
            goto done;
    }

    if (SetupFindFirstLineW(hInf, ServiceSection, L"Dependencies", &Context))
    {
        FieldCount = SetupGetFieldCount(&Context);
        for (i = 1; i <= FieldCount; i++)
        {
            if (!SetupGetStringFieldW(&Context, i, Buffer, ARRAYSIZE(Buffer), NULL) || !*Buffer)
                continue;

            if (Buffer[0] == SC_GROUP_IDENTIFIERW)
            {
                cchField = (DWORD)wcslen(&Buffer[1]) + 1;
                if (cchGroups + cchField + 1 > ARRAYSIZE(Groups))
                    continue;
                CopyMemory(&Groups[cchGroups], &Buffer[1], cchField * sizeof(WCHAR));
                cchGroups += cchField;
            }
            else
            {
                cchField = (DWORD)wcslen(Buffer) + 1;
                if (cchServices + cchField + 1 > ARRAYSIZE(Services))
                    continue;
                CopyMemory(&Services[cchServices], Buffer, cchField * sizeof(WCHAR));
                cchServices += cchField;
            }
        }

        if (cchServices)
        {
            Services[cchServices++] = UNICODE_NULL;
            if (!SetServiceString(hServiceKey, L"DependOnService", REG_MULTI_SZ, Services, cchServices * sizeof(WCHAR)))
                goto done;
        }
        if (cchGroups)
        {
            Groups[cchGroups++] = UNICODE_NULL;
            if (!SetServiceString(hServiceKey, L"DependOnGroup", REG_MULTI_SZ, Groups, cchGroups * sizeof(WCHAR)))
                goto done;
        }
    }

    ret = SetupInstallFromInfSectionW(NULL,
                                      hInf,
                                      ServiceSection,
                                      SPINST_REGISTRY,
                                      hServiceKey,
                                      NULL,
                                      0,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL);

done:
    RegCloseKey(hServiceKey);
    return ret;
}

static
BOOL
InstallOfflineServices(
    _In_ HINF hInf,
    _In_ PCWSTR Section)
{
    WCHAR ServiceName[MAX_PATH];
    WCHAR ServiceSection[MAX_PATH];
    INFCONTEXT Context;
    BOOL ret;

    if (!SetupFindFirstLineW(hInf, Section, NULL, &Context))
    {
        SetLastError(ERROR_SECTION_NOT_FOUND);
        return FALSE;
    }

    for (ret = SetupFindFirstLineW(hInf, Section, L"AddService", &Context);
         ret;
         ret = SetupFindNextMatchLineW(&Context, L"AddService", &Context))
    {
        if (!SetupGetStringFieldW(&Context, 1, ServiceName, ARRAYSIZE(ServiceName), NULL) ||
            !*ServiceName ||
            !SetupGetStringFieldW(&Context, 3, ServiceSection, ARRAYSIZE(ServiceSection), NULL))
        {
            continue;
        }

        if (!InstallOfflineService(hInf, ServiceName, ServiceSection))
            return FALSE;
    }

    return TRUE;
}

static
BOOL
InstallInfSections(
    _In_ HWND hWnd,
    _In_ HKEY hKey,
    _In_ LPCWSTR InfFile,
    _In_ LPCWSTR InfSection,
    _In_ BOOL bOffline)
{
    WCHAR Buffer[MAX_PATH];
    HINF hInf = INVALID_HANDLE_VALUE;
    UINT BufferSize;
    PVOID Context = NULL;
    BOOL ret = FALSE;

    DPRINT("InstallInfSections()\n");

    if (InfSection == NULL)
        return FALSE;

    /* Get Windows directory */
    BufferSize = ARRAYSIZE(Buffer) - 5 - wcslen(InfFile);
    if (GetWindowsDirectoryW(Buffer, BufferSize) > BufferSize)
    {
        /* Function failed */
        SetLastError(ERROR_GEN_FAILURE);
        goto cleanup;
    }

    /* We have enough space to add some information in the buffer */
    if (Buffer[wcslen(Buffer) - 1] != '\\')
        wcscat(Buffer, L"\\");
    wcscat(Buffer, L"Inf\\");
    wcscat(Buffer, InfFile);

    /* Install specified section */
    hInf = SetupOpenInfFileW(Buffer, NULL, INF_STYLE_WIN4, NULL);
    if (hInf == INVALID_HANDLE_VALUE)
        goto cleanup;

    Context = SetupInitDefaultQueueCallback(hWnd);
    if (Context == NULL)
        goto cleanup;

    ret = SetupInstallFromInfSectionW(
                hWnd, hInf,
                InfSection, SPINST_ALL,
                hKey, NULL, SP_COPY_NEWER,
                SetupDefaultQueueCallbackW, Context,
                NULL, NULL);
    if (ret == FALSE)
    {
        DPRINT1("SetupInstallFromInfSectionW(%S) failed (Error %lx)\n", InfSection, GetLastError());
        goto cleanup;
    }

    wcscpy(Buffer, InfSection);
    wcscat(Buffer, L".Services");

    if (bOffline)
        ret = InstallOfflineServices(hInf, Buffer);
    else
        ret = SetupInstallServicesFromInfSectionW(hInf, Buffer, 0);
    if (ret == FALSE)
    {
        DPRINT1("SetupInstallServicesFromInfSectionW(%S) failed (Error %lx)\n", Buffer, GetLastError());
        goto cleanup;
    }

cleanup:
    if (Context)
        SetupTermDefaultQueueCallback(Context);

    if (hInf != INVALID_HANDLE_VALUE)
        SetupCloseInfFile(hInf);

    DPRINT("InstallInfSections() done %u\n", ret);

    return ret;
}


static
BOOL
CreateInstanceKey(
    _In_ PCOMPONENT_INFO pComponentInfo,
    _Out_ PHKEY pInstanceKey)
{
    WCHAR szKeyBuffer[128];
    LPWSTR UuidString = NULL;
    UUID Uuid;
    RPC_STATUS RpcStatus;
    HKEY hInstanceKey;
    DWORD rc;
    BOOL ret = FALSE;

    DPRINT("CreateInstanceKey()\n");

    *pInstanceKey = NULL;

    wcscpy(szKeyBuffer, L"SYSTEM\\CurrentControlSet\\Control\\Network\\");
    wcscat(szKeyBuffer, pComponentInfo->pszClassGuid);
    wcscat(szKeyBuffer, L"\\{");

    /* Create a new UUID */
    RpcStatus = UuidCreate(&Uuid);
    if (RpcStatus != RPC_S_OK && RpcStatus != RPC_S_UUID_LOCAL_ONLY)
    {
        DPRINT1("UuidCreate() failed with RPC status 0x%lx\n", RpcStatus);
        goto done;
    }

    RpcStatus = UuidToStringW(&Uuid, &UuidString);
    if (RpcStatus != RPC_S_OK)
    {
        DPRINT1("UuidToStringW() failed with RPC status 0x%lx\n", RpcStatus);
        goto done;
    }

    wcscat(szKeyBuffer, UuidString);
    wcscat(szKeyBuffer, L"}");

    RpcStringFreeW(&UuidString);

    DPRINT("szKeyBuffer %S\n", szKeyBuffer);

    rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE,
                         szKeyBuffer,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         KEY_CREATE_SUB_KEY | KEY_SET_VALUE,
                         NULL,
                         &hInstanceKey,
                         NULL);
    if (rc != ERROR_SUCCESS)
    {
        DPRINT1("RegCreateKeyExW() failed with error 0x%lx\n", rc);
        goto done;
    }

    rc = RegSetValueExW(hInstanceKey,
                        L"Characteristics",
                        0,
                        REG_DWORD,
                        (LPBYTE)&pComponentInfo->dwCharacteristics,
                        sizeof(DWORD));
    if (rc != ERROR_SUCCESS)
    {
        DPRINT1("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto done;
    }

    rc = RegSetValueExW(hInstanceKey,
                        L"ComponentId",
                        0,
                        REG_SZ,
                        (LPBYTE)pComponentInfo->pszComponentId,
                        (wcslen(pComponentInfo->pszComponentId) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        DPRINT1("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto done;
    }

    rc = RegSetValueExW(hInstanceKey,
                        L"Description",
                        0,
                        REG_SZ,
                        (LPBYTE)pComponentInfo->pszDescription,
                        (wcslen(pComponentInfo->pszDescription) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        DPRINT1("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto done;
    }

    rc = RegSetValueExW(hInstanceKey,
                        L"InfPath",
                        0,
                        REG_SZ,
                        (LPBYTE)pComponentInfo->pszInfPath,
                        (wcslen(pComponentInfo->pszInfPath) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        DPRINT1("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto done;
    }

    rc = RegSetValueExW(hInstanceKey,
                        L"InfSection",
                        0,
                        REG_SZ,
                        (LPBYTE)pComponentInfo->pszInfSection,
                        (wcslen(pComponentInfo->pszInfSection) + 1) * sizeof(WCHAR));
    if (rc != ERROR_SUCCESS)
    {
        DPRINT1("RegSetValueExW() failed with error 0x%lx\n", rc);
        goto done;
    }

    *pInstanceKey = hInstanceKey;
    ret = TRUE;

done:
    if (ret == FALSE)
        RegCloseKey(hInstanceKey);

    DPRINT("CreateInstanceKey() done %u\n", ret);

    return ret;
}


static
BOOL
CheckInfFile(
    _In_ PWSTR pszFullInfName,
    _In_ PWSTR pszComponentId,
    _In_ PCOMPONENT_INFO pComponentInfo)
{
    WCHAR szLineBuffer[MAX_PATH];
    HINF hInf = INVALID_HANDLE_VALUE;
    INFCONTEXT MfgContext, DevContext, MiscContext;
    DWORD dwLength;

    hInf = SetupOpenInfFileW(pszFullInfName,
                             NULL,
                             INF_STYLE_WIN4,
                             NULL);
    if (hInf == INVALID_HANDLE_VALUE)
    {
        DPRINT1("\n");
        return FALSE;
    }

    if (!SetupFindFirstLineW(hInf,
                             L"Manufacturer",
                             NULL,
                             &MfgContext))
    {
        DPRINT("No Manufacurer section found!\n");
        goto done;
    }

    for (;;)
    {
        if (!SetupGetStringFieldW(&MfgContext,
                                  1,
                                  szLineBuffer,
                                  MAX_PATH,
                                  NULL))
            break;

        DPRINT("Manufacturer: %S\n", szLineBuffer);
        if (!SetupFindFirstLineW(hInf,
                                 szLineBuffer,
                                 NULL,
                                 &DevContext))
            break;

        for (;;)
        {
            if (!SetupGetStringFieldW(&DevContext,
                                      2,
                                      szLineBuffer,
                                      MAX_PATH,
                                      NULL))
                break;

            DPRINT("Device: %S\n", szLineBuffer);
            if (_wcsicmp(szLineBuffer, pszComponentId) == 0)
            {
                DPRINT("Found it!\n");

                /* Get the section name*/
                SetupGetStringFieldW(&DevContext,
                                     1,
                                     NULL,
                                     0,
                                     &dwLength);

                pComponentInfo->pszInfSection = HeapAlloc(GetProcessHeap(),
                                                          0,
                                                          dwLength * sizeof(WCHAR));
                if (pComponentInfo->pszInfSection)
                {
                    SetupGetStringFieldW(&DevContext,
                                         1,
                                         pComponentInfo->pszInfSection,
                                         dwLength,
                                         &dwLength);
                }

                /* Get the description*/
                SetupGetStringFieldW(&DevContext,
                                     0,
                                     NULL,
                                     0,
                                     &dwLength);

                pComponentInfo->pszDescription = HeapAlloc(GetProcessHeap(),
                                                           0,
                                                           dwLength * sizeof(WCHAR));
                if (pComponentInfo->pszDescription)
                {
                    SetupGetStringFieldW(&DevContext,
                                         0,
                                         pComponentInfo->pszDescription,
                                         dwLength,
                                         &dwLength);
                }

                /* Get the class GUID */
                if (SetupFindFirstLineW(hInf,
                                        L"Version",
                                        L"ClassGuid",
                                        &MiscContext))
                {
                    SetupGetStringFieldW(&MiscContext,
                                         1,
                                         NULL,
                                         0,
                                         &dwLength);

                    pComponentInfo->pszClassGuid = HeapAlloc(GetProcessHeap(),
                                                             0,
                                                             dwLength * sizeof(WCHAR));
                    if (pComponentInfo->pszInfSection)
                    {
                        SetupGetStringFieldW(&MiscContext,
                                             1,
                                             pComponentInfo->pszClassGuid,
                                             dwLength,
                                             &dwLength);
                    }
                }

                /* Get the Characteristics value */
                if (SetupFindFirstLineW(hInf,
                                        pComponentInfo->pszInfSection,
                                        L"Characteristics",
                                        &MiscContext))
                {
                    SetupGetIntField(&MiscContext,
                                     1,
                                     (PINT)&pComponentInfo->dwCharacteristics);
                }

                SetupCloseInfFile(hInf);
                return TRUE;
            }

            if (!SetupFindNextLine(&DevContext, &DevContext))
                break;
        }

        if (!SetupFindNextLine(&MfgContext, &MfgContext))
            break;
    }

done:
    if (hInf != INVALID_HANDLE_VALUE)
        SetupCloseInfFile(hInf);

    return FALSE;
}


static
BOOL
ScanForInfFile(
    _In_ PWSTR pszComponentId,
    _In_ PCOMPONENT_INFO pComponentInfo)
{
    WCHAR szInfPath[MAX_PATH];
    WCHAR szFullInfName[MAX_PATH];
    WCHAR szPathBuffer[MAX_PATH];
    WIN32_FIND_DATAW fdw;
    HANDLE hFindFile = INVALID_HANDLE_VALUE;
    BOOL bFound = FALSE;

    GetWindowsDirectoryW(szInfPath, MAX_PATH);
    wcscat(szInfPath, L"\\inf");

    wcscpy(szPathBuffer, szInfPath);
    wcscat(szPathBuffer, L"\\*.inf");

    hFindFile = FindFirstFileW(szPathBuffer, &fdw);
    if (hFindFile == INVALID_HANDLE_VALUE)
        return FALSE;

    for (;;)
    {
        if (wcscmp(fdw.cFileName, L".") == 0 ||
            wcscmp(fdw.cFileName, L"..") == 0)
            continue;

        DPRINT("FileName: %S\n", fdw.cFileName);

        wcscpy(szFullInfName, szInfPath);
        wcscat(szFullInfName, L"\\");
        wcscat(szFullInfName, fdw.cFileName);

        DPRINT("Full Inf Name: %S\n", szFullInfName);
        if (CheckInfFile(szFullInfName,
                         pszComponentId,
                         pComponentInfo))
        {
            pComponentInfo->pszInfPath = HeapAlloc(GetProcessHeap(),
                                                   0,
                                                   (wcslen(fdw.cFileName) + 1) * sizeof(WCHAR));
            if (pComponentInfo->pszInfPath)
                wcscpy(pComponentInfo->pszInfPath, fdw.cFileName);

            pComponentInfo->pszComponentId = HeapAlloc(GetProcessHeap(),
                                                       0,
                                                       (wcslen(pszComponentId) + 1) * sizeof(WCHAR));
            if (pComponentInfo->pszComponentId)
                wcscpy(pComponentInfo->pszComponentId, pszComponentId);

            bFound = TRUE;
            break;
        }

        if (!FindNextFileW(hFindFile, &fdw))
            break;
    }

    if (hFindFile != INVALID_HANDLE_VALUE)
        FindClose(hFindFile);

    return bFound;
}


BOOL
InstallNetworkComponent(
    _In_ PWSTR pszComponentId,
    _In_ BOOL bOffline)
{
    COMPONENT_INFO ComponentInfo;
    HKEY hInstanceKey = NULL;
    BOOL bResult = FALSE;

    DPRINT("InstallNetworkComponent(%S)\n", pszComponentId);

    ZeroMemory(&ComponentInfo, sizeof(COMPONENT_INFO));

    if (!ScanForInfFile(pszComponentId, &ComponentInfo))
        goto done;

    DPRINT("Characteristics: 0x%lx\n", ComponentInfo.dwCharacteristics);
    DPRINT("ComponentId: %S\n", ComponentInfo.pszComponentId);
    DPRINT("Description: %S\n", ComponentInfo.pszDescription);
    DPRINT("InfPath: %S\n", ComponentInfo.pszInfPath);
    DPRINT("InfSection: %S\n", ComponentInfo.pszInfSection);
    DPRINT("ClassGuid: %S\n", ComponentInfo.pszClassGuid);

    if (!CreateInstanceKey(&ComponentInfo,
                           &hInstanceKey))
    {
        DPRINT1("CreateInstanceKey() failed (Error %lx)\n", GetLastError());
        goto done;
    }

    if (!InstallInfSections(NULL,
                            hInstanceKey,
                            ComponentInfo.pszInfPath,
                            ComponentInfo.pszInfSection,
                            bOffline))
    {
        DPRINT1("InstallInfSections() failed (Error %lx)\n", GetLastError());
        goto done;
    }

    bResult = TRUE;

done:
    if (hInstanceKey != NULL)
        RegCloseKey(hInstanceKey);

    if (ComponentInfo.pszInfPath)
        HeapFree(GetProcessHeap(), 0, ComponentInfo.pszInfPath);

    if (ComponentInfo.pszInfSection)
        HeapFree(GetProcessHeap(), 0, ComponentInfo.pszInfSection);

    if (ComponentInfo.pszComponentId)
        HeapFree(GetProcessHeap(), 0, ComponentInfo.pszComponentId);

    if (ComponentInfo.pszDescription)
        HeapFree(GetProcessHeap(), 0, ComponentInfo.pszDescription);

    if (ComponentInfo.pszClassGuid)
        HeapFree(GetProcessHeap(), 0, ComponentInfo.pszClassGuid);

    return bResult;
}

/* EOF */
