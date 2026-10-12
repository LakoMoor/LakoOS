/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         System setup
 * FILE:            dll/win32/syssetup/wizard.c
 * PURPOSE:         GUI controls
 * PROGRAMMERS:     Eric Kohl
 *                  Pierre Schweitzer <heis_spiter@hotmail.com>
 *                  Ismael Ferreras Morezuelas <swyterzone+ros@gmail.com>
 *                  Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *                  Oleg Dubinskiy <oleg.dubinskij30@gmail.com>
 *                  Whindmar Saksit <whindsaks@proton.me>
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include <stdlib.h>
#include <time.h>
#include <winnls.h>
#include <windowsx.h>
#include <wincon.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tzlib.h>
#include <strsafe.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

static const WCHAR s_szProductOptions[] = L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions";
static const WCHAR s_szRosVersion[] = L"SYSTEM\\CurrentControlSet\\Control\\ReactOS\\Settings\\Version";
static const WCHAR s_szControlWindows[] = L"SYSTEM\\CurrentControlSet\\Control\\Windows";
static const WCHAR s_szWinlogon[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
static const WCHAR s_szDefaultSoundEvents[] = L"AppEvents\\Schemes\\Apps\\.Default";
static const WCHAR s_szExplorerSoundEvents[] = L"AppEvents\\Schemes\\Apps\\Explorer";
static const WCHAR s_szCurrentVersion[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";

typedef struct _PRODUCT_OPTION_DATA
{
    LPCWSTR ProductSuite;
    LPCWSTR ProductType;
    DWORD ReportAsWorkstation;
    DWORD CSDVersion;
    DWORD LogonType;
} PRODUCT_OPTION_DATA;

static const PRODUCT_OPTION_DATA s_ProductOptionData[INSTALLATION_TYPE_MAX] =
{
    { L"Terminal Server\0", L"ServerNT", 0, 0, 0 },
    { L"\0", L"WinNT", 1, 0, 1 },
    { L"Terminal Server\0", L"ServerNT", 0, 0, 0 }
    // { L"Terminal Server\0", L"ServerNT", 0, 0x200, 0 }
};

static const WCHAR* InstallationTypes[INSTALLATION_TYPE_MAX] =
{
    L"Server",
    L"Client",
    L"Server Core",
    // L"Nano Server"
};

static const WCHAR* s_DefaultSoundEvents[][2] = 
{
    { L".Default", L"%SystemRoot%\\Media\\ReactOS_Default.wav" },
    { L"AppGPFault", L"" },
    { L"Close", L"" },
    { L"CriticalBatteryAlarm", L"%SystemRoot%\\Media\\ReactOS_Battery_Critical.wav" },
    { L"DeviceConnect",  L"%SystemRoot%\\Media\\ReactOS_Hardware_Insert.wav" },
    { L"DeviceDisconnect", L"%SystemRoot%\\Media\\ReactOS_Hardware_Remove.wav" },
    { L"DeviceFail", L"%SystemRoot%\\Media\\ReactOS_Hardware_Fail.wav" },
    { L"LowBatteryAlarm", L"%SystemRoot%\\Media\\ReactOS_Battery_Low.wav" },
    { L"MailBeep", L"%SystemRoot%\\Media\\ReactOS_Notify.wav" },
    { L"Maximize", L"%SystemRoot%\\Media\\ReactOS_Restore.wav" },
    { L"MenuCommand", L"%SystemRoot%\\Media\\ReactOS_Menu_Command.wav" },
    { L"MenuPopup", L"" },
    { L"Minimize", L"%SystemRoot%\\Media\\ReactOS_Minimize.wav" },
    { L"Open", L"" },
    { L"PrintComplete", L"%SystemRoot%\\Media\\ReactOS_Print_Complete.wav" },
    { L"RestoreDown", L"" },
    { L"RestoreUp", L"" },
    { L"SystemAsterisk", L"%SystemRoot%\\Media\\ReactOS_Ding.wav" },
    { L"SystemExclamation", L"%SystemRoot%\\Media\\ReactOS_Exclamation.wav" },
    { L"SystemExit", L"%SystemRoot%\\Media\\ReactOS_Shutdown.wav" },
    { L"SystemHand", L"%SystemRoot%\\Media\\ReactOS_Critical_Stop.wav" },
    { L"SystemNotification", L"%SystemRoot%\\Media\\ReactOS_Balloon.wav" },
    { L"SystemQuestion", L"%SystemRoot%\\Media\\ReactOS_Ding.wav" },
    { L"SystemStart", L"%SystemRoot%\\Media\\ReactOS_Startup.wav" },
    { L"WindowsLogoff", L"%SystemRoot%\\Media\\ReactOS_LogOff.wav" }
/* Logon sound is already set by default for both Server and Workstation */
};

static const WCHAR* s_ExplorerSoundEvents[][2] = 
{
    { L"EmptyRecycleBin", L"%SystemRoot%\\Media\\ReactOS_Recycle.wav" },
    { L"Navigating", L"%SystemRoot%\\Media\\ReactOS_Start.wav" }
};

static BOOL
DoWriteSoundEvents(HKEY hKey,
                   LPCWSTR lpSubkey,
                   LPCWSTR lpEventsArray[][2],
                   DWORD dwSize)
{
    HKEY hRootKey, hEventKey, hDefaultKey;
    LONG error;
    ULONG i;
    WCHAR szDest[MAX_PATH];
    DWORD dwAttribs;
    DWORD cbData;

    /* Open the sound events key */
    error = RegOpenKeyExW(hKey, lpSubkey, 0, KEY_READ, &hRootKey);
    if (error)
    {
        DPRINT1("RegOpenKeyExW failed\n");
        goto Error;
    }

    /* Set each sound event */
    for (i = 0; i < dwSize; i++)
    {
        /*
         * Verify that the sound file exists and is an actual file.
         */

        /* Expand the sound file path */
        if (!ExpandEnvironmentStringsW(lpEventsArray[i][1], szDest, _countof(szDest)))
        {
            /* Failed to expand, continue with the next sound event */
            continue;
        }

        /* Check if the sound file exists and isn't a directory */
        dwAttribs = GetFileAttributesW(szDest);
        if ((dwAttribs == INVALID_FILE_ATTRIBUTES) ||
            (dwAttribs & FILE_ATTRIBUTE_DIRECTORY))
        {
            /* It does not, just continue with the next sound event */
            continue;
        }

        /*
         * Create the sound event entry.
         */

        /* Open the sound event subkey */
        error = RegOpenKeyExW(hRootKey, lpEventsArray[i][0], 0, KEY_READ, &hEventKey);
        if (error)
        {
            /* Failed to open, continue with next sound event */
            continue;
        }

        /* Open .Default subkey */
        error = RegOpenKeyExW(hEventKey, L".Default", 0, KEY_WRITE, &hDefaultKey);
        RegCloseKey(hEventKey);
        if (error)
        {
            /* Failed to open, continue with next sound event */
            continue;
        }

        /* Associate the sound file to this sound event */
        cbData = (lstrlenW(lpEventsArray[i][1]) + 1) * sizeof(WCHAR);
        error = RegSetValueExW(hDefaultKey, NULL, 0, REG_EXPAND_SZ, (const BYTE *)lpEventsArray[i][1], cbData);
        RegCloseKey(hDefaultKey);
        if (error)
        {
            /* Failed to set the value, continue with next sound event */
            continue;
        }
    }

Error:
    if (hRootKey)
        RegCloseKey(hRootKey);

    return error == ERROR_SUCCESS;
}

BOOL
DoWriteInstallationType(INSTALLATION_TYPE nOption)
{
    HKEY hKey;
    LONG error;
    LPCWSTR pszData;
    DWORD dwValue, cbData;
    const PRODUCT_OPTION_DATA *pData = &s_ProductOptionData[nOption];
    ASSERT(0 <= nOption && nOption < INSTALLATION_TYPE_MAX);

    /* open ProductOptions key */
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_szProductOptions, 0, KEY_WRITE, &hKey);
    if (error)
    {
        DPRINT1("RegOpenKeyExW failed\n");
        goto Error;
    }

    /* write ProductSuite */
    pszData = pData->ProductSuite;
    cbData = (lstrlenW(pszData) + 2) * sizeof(WCHAR);
    error = RegSetValueExW(hKey, L"ProductSuite", 0, REG_MULTI_SZ, (const BYTE *)pszData, cbData);
    if (error)
    {
        DPRINT1("RegSetValueExW failed\n");
        goto Error;
    }

    /* write ProductType */
    pszData = pData->ProductType;
    cbData = (lstrlenW(pszData) + 1) * sizeof(WCHAR);
    error = RegSetValueExW(hKey, L"ProductType", 0, REG_SZ, (const BYTE *)pszData, cbData);
    if (error)
    {
        DPRINT1("RegSetValueExW failed\n");
        goto Error;
    }

    RegCloseKey(hKey);

    /* open ReactOS version key */
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_szRosVersion, 0, KEY_WRITE, &hKey);
    if (error)
    {
        DPRINT1("RegOpenKeyExW failed\n");
        goto Error;
    }

    /* write ReportAsWorkstation */
    dwValue = pData->ReportAsWorkstation;
    cbData = sizeof(dwValue);
    error = RegSetValueExW(hKey, L"ReportAsWorkstation", 0, REG_DWORD, (const BYTE *)&dwValue, cbData);
    if (error)
    {
        DPRINT1("RegSetValueExW failed\n");
        goto Error;
    }

    RegCloseKey(hKey);

    /* open Control Windows key */
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_szControlWindows, 0, KEY_WRITE, &hKey);
    if (error)
    {
        DPRINT1("RegOpenKeyExW failed\n");
        goto Error;
    }

    /* write Control Windows CSDVersion */
    dwValue = pData->CSDVersion;
    cbData = sizeof(dwValue);
    error = RegSetValueExW(hKey, L"CSDVersion", 0, REG_DWORD, (const BYTE *)&dwValue, cbData);
    if (error)
    {
        DPRINT1("RegSetValueExW failed\n");
        goto Error;
    }

    RegCloseKey(hKey);

    /* open Winlogon key */
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_szWinlogon, 0, KEY_WRITE, &hKey);
    if (error)
    {
        DPRINT1("RegOpenKeyExW failed\n");
        goto Error;
    }

    /* write LogonType */
    dwValue = pData->LogonType;
    cbData = sizeof(dwValue);
    error = RegSetValueExW(hKey, L"LogonType", 0, REG_DWORD, (const BYTE *)&dwValue, cbData);
    if (error)
    {
        DPRINT1("RegSetValueExW failed\n");
        goto Error;
    }

    if (nOption == INSTALLATION_TYPE_WORKSTATION)
    {
        /* Write system sound events values for Workstation */
        DoWriteSoundEvents(HKEY_CURRENT_USER, s_szDefaultSoundEvents, s_DefaultSoundEvents, _countof(s_DefaultSoundEvents));
        DoWriteSoundEvents(HKEY_CURRENT_USER, s_szExplorerSoundEvents, s_ExplorerSoundEvents, _countof(s_ExplorerSoundEvents));
    }

    if (nOption == INSTALLATION_TYPE_SERVER_CORE)
    {
        /* Set the shell to command prompt */
        WCHAR szShell[] = L"cmd.exe";
        cbData = sizeof(szShell);
        error = RegSetValueExW(hKey, L"Shell", 0, REG_SZ, (const BYTE *)szShell, cbData);
        if (error)
        {
            DPRINT1("RegSetValueExW failed\n");
            goto Error;
        }
    }

    /* Open InstallationType key and write InstallationType value */
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_szCurrentVersion, 0, KEY_WRITE, &hKey);
    if (error)
    {
        DPRINT1("RegOpenKeyExW failed\n");
        goto Error;
    }

    cbData = (DWORD)((wcslen(InstallationTypes[nOption]) + 1) * sizeof(WCHAR));
    error = RegSetValueExW(hKey, L"InstallationType", 0, REG_SZ, (const BYTE *)InstallationTypes[nOption], cbData);
    if (error)
    {
        DPRINT1("RegSetValueExW failed\n");
        goto Error;
    }

Error:
    if (hKey)
        RegCloseKey(hKey);

    return error == ERROR_SUCCESS;
}

BOOL
WriteOwnerSettings(PCWSTR OwnerName,
                   PCWSTR OwnerOrganization)
{
    HKEY hKey;
    LONG res;

    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"Software\\Microsoft\\Windows NT\\CurrentVersion",
                        0,
                        KEY_ALL_ACCESS,
                        &hKey);

    if (res != ERROR_SUCCESS)
    {
        return FALSE;
    }

    res = RegSetValueExW(hKey,
                         L"RegisteredOwner",
                         0,
                         REG_SZ,
                         (LPBYTE)OwnerName,
                         (wcslen(OwnerName) + 1) * sizeof(WCHAR));

    if (res != ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    res = RegSetValueExW(hKey,
                         L"RegisteredOrganization",
                         0,
                         REG_SZ,
                         (LPBYTE)OwnerOrganization,
                         (wcslen(OwnerOrganization) + 1) * sizeof(WCHAR));

    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}

static BOOL
RunControlPanelApplet(HWND hwnd, PCWSTR pwszCPLParameters)
{
    MSG msg;
    HWND MainWindow = GetParent(hwnd);
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInformation;
    WCHAR CmdLine[MAX_PATH] = L"rundll32.exe shell32.dll,Control_RunDLL ";

    if (!pwszCPLParameters)
    {
        MessageBoxW(hwnd, L"Error: Failed to launch the Control Panel Applet.", NULL, MB_ICONERROR);
        return FALSE;
    }

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    ZeroMemory(&ProcessInformation, sizeof(ProcessInformation));

    ASSERT(_countof(CmdLine) > wcslen(CmdLine) + wcslen(pwszCPLParameters));
    wcscat(CmdLine, pwszCPLParameters);

    if (!CreateProcessW(NULL,
                        CmdLine,
                        NULL,
                        NULL,
                        FALSE,
                        0,
                        NULL,
                        NULL,
                        &StartupInfo,
                        &ProcessInformation))
    {
        MessageBoxW(hwnd, L"Error: Failed to launch the Control Panel Applet.", NULL, MB_ICONERROR);
        return FALSE;
    }

    /* Disable the Back and Next buttons and the main window
     * while we're interacting with the control panel applet */
    PropSheet_SetWizButtons(MainWindow, 0);
    EnableWindow(MainWindow, FALSE);

    while ((MsgWaitForMultipleObjects(1, &ProcessInformation.hProcess, FALSE, INFINITE, QS_ALLINPUT|QS_ALLPOSTMESSAGE )) != WAIT_OBJECT_0)
    {
       /* We still need to process main window messages to avoid freeze */
       while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
       {
           TranslateMessage(&msg);
           DispatchMessageW(&msg);
       }
    }
    CloseHandle(ProcessInformation.hThread);
    CloseHandle(ProcessInformation.hProcess);

    /* Enable the Back and Next buttons and the main window again */
    PropSheet_SetWizButtons(MainWindow, PSWIZB_BACK | PSWIZB_NEXT);
    EnableWindow(MainWindow, TRUE);

    return TRUE;
}


VOID
EnableVisualTheme(
    _In_opt_ HWND hwndParent,
    _In_opt_ PCWSTR ThemeFile)
{
    enum { THEME_FILE, STYLE_FILE, UNKNOWN } fType;
    WCHAR szPath[MAX_PATH]; // Expanded path of the file to use.
    WCHAR szStyleFile[MAX_PATH];

    fType = THEME_FILE; // Default to Classic theme.
    if (ThemeFile)
    {
        /* Expand the path if possible */
        if (ExpandEnvironmentStringsW(ThemeFile, szPath, _countof(szPath)) != 0)
            ThemeFile = szPath;

        /* Determine the file type from its extension */
        fType = UNKNOWN; {
        PCWSTR pszExt = wcsrchr(ThemeFile, L'.'); // PathFindExtensionW(ThemeFile);
        if (pszExt)
        {
            if (_wcsicmp(pszExt, L".theme") == 0)
                fType = THEME_FILE;
            else if (_wcsicmp(pszExt, L".msstyles") == 0)
                fType = STYLE_FILE;
        } }
        if (fType == UNKNOWN)
        {
            DPRINT1("EnableVisualTheme(): Unknown file '%S'\n", ThemeFile);
            return;
        }
    }

    DPRINT("Applying visual %s '%S'\n",
            (fType == THEME_FILE) ? "theme" : "style",
            ThemeFile ? ThemeFile : L"(Classic)");

//
// TODO: Use instead uxtheme!SetSystemVisualStyle() once it is implemented,
// https://stackoverflow.com/a/1036903
// https://pinvoke.net/default.aspx/uxtheme.SetSystemVisualStyle
// or ApplyTheme(NULL, 0, NULL) for restoring the classic theme.
//
// NOTE: The '/Action:ActivateMSTheme' is ReactOS-specific.
//

    if (ThemeFile && (fType == THEME_FILE))
    {
        /* Retrieve the visual style specified in the theme file.
         * If none, fall back to the classic theme. */
        if (GetPrivateProfileStringW(L"VisualStyles", L"Path", NULL,
                                     szStyleFile, _countof(szStyleFile), ThemeFile) && *szStyleFile)
        {
            /* Expand the path if possible */
            ThemeFile = szStyleFile;
            if (ExpandEnvironmentStringsW(ThemeFile, szPath, _countof(szPath)) != 0)
                ThemeFile = szPath;
        }
        else
        {
            ThemeFile = NULL;
        }

        DPRINT1("--> Applying visual style '%S'\n",
                ThemeFile ? ThemeFile : L"(Classic)");
    }

    if (ThemeFile)
    {
        WCHAR wszParams[1024];
        // FIXME: L"desk.cpl desk,@Appearance" regression, see commit 50d260a7f0
        PCWSTR format = L"desk.cpl,,2 /Action:ActivateMSTheme /file:\"%s\"";

        StringCchPrintfW(wszParams, _countof(wszParams), format, ThemeFile);
        RunControlPanelApplet(hwndParent, wszParams);
    }
    else
    {
        RunControlPanelApplet(hwndParent, L"desk.cpl,,2 /Action:ActivateMSTheme");
    }
}


/* EOF */
