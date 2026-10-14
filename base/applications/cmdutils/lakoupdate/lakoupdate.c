/*
 * PROJECT:     LakoOS Update
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Checks for, downloads and installs system and driver updates
 */

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <newdev.h>
#include <stdio.h>
#include <wchar.h>

#define WU_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate"
#define DEFAULT_FEED L"https://github.com/LakoMoor/LakoOS/releases/latest/download/update.ini"
#define MAX_PKG 64
#define MAX_FILES 32

typedef struct
{
    WCHAR Url[512];
    WCHAR Name[128];
    WCHAR Sha256[80];
    WCHAR Dest[MAX_PATH];
} UFILE;

typedef struct
{
    WCHAR Id[64];
    WCHAR Title[160];
    WCHAR Kind[16];
    BOOL Reboot;
    BOOL Installed;
    INT Count;
    UFILE Files[MAX_FILES];
} UPKG;

static WCHAR g_Feed[512];
static WCHAR g_Manifest[MAX_PATH];
static WCHAR g_Work[MAX_PATH];
static UPKG g_Pkg[MAX_PKG];
static INT g_nPkg;
static WCHAR g_Version[64];

static void Log(const WCHAR *fmt, ...)
{
    WCHAR line[600], path[MAX_PATH];
    SYSTEMTIME st;
    va_list ap;
    HANDLE h;
    DWORD w;
    int n;

    va_start(ap, fmt);
    vswprintf(line, 500, fmt, ap);
    va_end(ap);
    wprintf(L"%ls\n", line);
    wsprintfW(path, L"%ls\\history.log", g_Work);
    h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        char out[1400];
        GetLocalTime(&st);
        n = wsprintfW(path, L"%04u-%02u-%02u %02u:%02u  %ls\r\n", st.wYear, st.wMonth, st.wDay,
                      st.wHour, st.wMinute, line);
        n = WideCharToMultiByte(CP_UTF8, 0, path, n, out, sizeof(out), NULL, NULL);
        WriteFile(h, out, n, &w, NULL);
        CloseHandle(h);
    }
}

static void Init(void)
{
    DWORD size = sizeof(g_Feed);
    HKEY hKey;
    WCHAR root[MAX_PATH];

    wcscpy(g_Feed, DEFAULT_FEED);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, WU_KEY, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"FeedUrl", NULL, NULL, (BYTE *)g_Feed, &size);
        RegCloseKey(hKey);
    }
    GetWindowsDirectoryW(root, MAX_PATH);
    wsprintfW(g_Work, L"%ls\\SoftwareDistribution", root);
    CreateDirectoryW(g_Work, NULL);
    wsprintfW(root, L"%ls\\Download", g_Work);
    CreateDirectoryW(root, NULL);
    wsprintfW(root, L"%ls\\Backup", g_Work);
    CreateDirectoryW(root, NULL);
    wsprintfW(g_Manifest, L"%ls\\update.ini", g_Work);
}

static BOOL Fetch(const WCHAR *url, const WCHAR *dest)
{
    URL_COMPONENTS uc = { sizeof(uc) };
    WCHAR host[256], path[1024];
    HINTERNET hS = NULL, hC = NULL, hR = NULL;
    HANDLE hFile;
    BOOL ok = FALSE;
    DWORD got, wr, status = 0, len = sizeof(status);
    BYTE buf[16384];

    if (_wcsnicmp(url, L"http", 4))
        return CopyFileW(url, dest, FALSE);

    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url, 0, 0, &uc))
        return FALSE;
    hFile = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    hS = WinHttpOpen(L"LakoOS-Update/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (hS)
    {
        DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hS, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
        hC = WinHttpConnect(hS, host, uc.nPort, 0);
    }
    if (hC)
        hR = WinHttpOpenRequest(hC, L"GET", path, NULL, NULL, NULL,
                                uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (hR && WinHttpSendRequest(hR, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(hR, NULL))
    {
        WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &len, NULL);
        if (status == 200)
        {
            ok = TRUE;
            while (WinHttpReadData(hR, buf, sizeof(buf), &got) && got)
                WriteFile(hFile, buf, got, &wr, NULL);
        }
    }
    if (hR) WinHttpCloseHandle(hR);
    if (hC) WinHttpCloseHandle(hC);
    if (hS) WinHttpCloseHandle(hS);
    CloseHandle(hFile);
    if (!ok)
        DeleteFileW(dest);
    return ok;
}

static BOOL FileSha256(const WCHAR *path, WCHAR *hex)
{
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    HANDLE hFile;
    BYTE buf[16384], digest[32];
    DWORD got, i;
    BOOL ok = FALSE;

    hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
        BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0)
    {
        while (ReadFile(hFile, buf, sizeof(buf), &got, NULL) && got)
            BCryptHashData(hHash, buf, got, 0);
        if (BCryptFinishHash(hHash, digest, 32, 0) == 0)
        {
            for (i = 0; i < 32; i++)
                wsprintfW(hex + i * 2, L"%02x", digest[i]);
            ok = TRUE;
        }
    }
    if (hHash) BCryptDestroyHash(hHash);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    CloseHandle(hFile);
    return ok;
}

static BOOL IsInstalled(const WCHAR *id)
{
    HKEY hKey;
    DWORD v = 0, size = sizeof(v);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, WU_KEY L"\\Installed", 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return FALSE;
    RegQueryValueExW(hKey, id, NULL, NULL, (BYTE *)&v, &size);
    RegCloseKey(hKey);
    return v != 0;
}

static void MarkInstalled(const WCHAR *id)
{
    HKEY hKey;
    DWORD v = 1;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, WU_KEY L"\\Installed", 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, id, 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        RegCloseKey(hKey);
    }
}

static BOOL ExpandDest(const WCHAR *in, WCHAR *out)
{
    return ExpandEnvironmentStringsW(in, out, MAX_PATH) != 0;
}

static BOOL Scan(void)
{
    WCHAR sect[32], key[48], val[600];
    INT count, i, f;

    Log(L"Checking %ls", g_Feed);
    if (!Fetch(g_Feed, g_Manifest))
    {
        Log(L"Could not download the update list.");
        return FALSE;
    }
    GetPrivateProfileStringW(L"Update", L"Version", L"", g_Version, 64, g_Manifest);
    count = GetPrivateProfileIntW(L"Update", L"Packages", 0, g_Manifest);
    g_nPkg = 0;
    for (i = 1; i <= count && g_nPkg < MAX_PKG; i++)
    {
        UPKG *p = &g_Pkg[g_nPkg];

        ZeroMemory(p, sizeof(*p));
        wsprintfW(sect, L"Package%d", i);
        GetPrivateProfileStringW(sect, L"Id", L"", p->Id, 64, g_Manifest);
        GetPrivateProfileStringW(sect, L"Title", p->Id, p->Title, 160, g_Manifest);
        GetPrivateProfileStringW(sect, L"Kind", L"file", p->Kind, 16, g_Manifest);
        p->Reboot = GetPrivateProfileIntW(sect, L"Reboot", 0, g_Manifest) != 0;
        p->Count = min(GetPrivateProfileIntW(sect, L"Files", 0, g_Manifest), MAX_FILES);
        if (!p->Id[0] || !p->Count)
            continue;
        for (f = 1; f <= p->Count; f++)
        {
            UFILE *u = &p->Files[f - 1];

            wsprintfW(key, L"File%dUrl", f);
            GetPrivateProfileStringW(sect, key, L"", u->Url, 512, g_Manifest);
            wsprintfW(key, L"File%dName", f);
            GetPrivateProfileStringW(sect, key, L"", u->Name, 128, g_Manifest);
            wsprintfW(key, L"File%dSha256", f);
            GetPrivateProfileStringW(sect, key, L"", u->Sha256, 80, g_Manifest);
            wsprintfW(key, L"File%dDest", f);
            GetPrivateProfileStringW(sect, key, L"", val, 600, g_Manifest);
            ExpandDest(val, u->Dest);
        }
        p->Installed = IsInstalled(p->Id);
        g_nPkg++;
    }
    return TRUE;
}

static void Status(void)
{
    INT i, pending = 0;

    for (i = 0; i < g_nPkg; i++)
    {
        if (g_Pkg[i].Installed)
            continue;
        pending++;
        wprintf(L"  [%ls] %ls  (%ls)%ls\n", g_Pkg[i].Id, g_Pkg[i].Title, g_Pkg[i].Kind,
                g_Pkg[i].Reboot ? L"  restart required" : L"");
    }
    if (!pending)
        wprintf(L"You're up to date.\n");
    else
        wprintf(L"%d update(s) available.\n", pending);
}

static BOOL Download(UPKG *p)
{
    WCHAR dir[MAX_PATH], path[MAX_PATH], hash[80];
    INT f;

    wsprintfW(dir, L"%ls\\Download\\%ls", g_Work, p->Id);
    CreateDirectoryW(dir, NULL);
    for (f = 0; f < p->Count; f++)
    {
        UFILE *u = &p->Files[f];

        wsprintfW(path, L"%ls\\%ls", dir, u->Name);
        Log(L"Downloading %ls", u->Name);
        if (!Fetch(u->Url, path))
        {
            Log(L"Download failed: %ls", u->Url);
            return FALSE;
        }
        if (u->Sha256[0])
        {
            if (!FileSha256(path, hash) || _wcsicmp(hash, u->Sha256))
            {
                Log(L"Hash mismatch for %ls, the file was discarded.", u->Name);
                DeleteFileW(path);
                return FALSE;
            }
        }
        else
        {
            Log(L"Refusing %ls: the package has no hash.", u->Name);
            DeleteFileW(path);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL PutFile(const WCHAR *src, const WCHAR *dest, BOOL *pbReboot)
{
    WCHAR bak[MAX_PATH], tmp[MAX_PATH];
    const WCHAR *name = wcsrchr(dest, L'\\');

    wsprintfW(bak, L"%ls\\Backup\\%ls", g_Work, name ? name + 1 : dest);
    CopyFileW(dest, bak, FALSE);
    if (CopyFileW(src, dest, FALSE))
        return TRUE;
    if (GetLastError() != ERROR_SHARING_VIOLATION && GetLastError() != ERROR_ACCESS_DENIED)
        return FALSE;
    wsprintfW(tmp, L"%ls.new", dest);
    if (!CopyFileW(src, tmp, FALSE))
        return FALSE;
    *pbReboot = TRUE;
    return MoveFileExW(tmp, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_DELAY_UNTIL_REBOOT);
}

static BOOL Install(UPKG *p, BOOL *pbReboot)
{
    WCHAR src[MAX_PATH], inf[MAX_PATH] = L"";
    INT f;

    for (f = 0; f < p->Count; f++)
    {
        UFILE *u = &p->Files[f];

        wsprintfW(src, L"%ls\\Download\\%ls\\%ls", g_Work, p->Id, u->Name);
        if (!_wcsicmp(p->Kind, L"driver"))
        {
            if (wcslen(u->Name) > 4 && !_wcsicmp(u->Name + wcslen(u->Name) - 4, L".inf"))
                wcscpy(inf, src);
            continue;
        }
        if (!u->Dest[0] || !PutFile(src, u->Dest, pbReboot))
        {
            Log(L"Could not install %ls (error %lu)", u->Name, GetLastError());
            return FALSE;
        }
    }
    if (!_wcsicmp(p->Kind, L"driver"))
    {
        BOOL needReboot = FALSE;

        if (!inf[0] || !DiInstallDriverW(NULL, inf, DIIRFLAG_FORCE_INF, &needReboot))
        {
            Log(L"Driver install failed (error %lu)", GetLastError());
            return FALSE;
        }
        if (needReboot)
            *pbReboot = TRUE;
    }
    return TRUE;
}

static int Run(BOOL doInstall)
{
    INT i, ok = 0, fail = 0;
    BOOL reboot = FALSE;

    if (!Scan())
        return 2;
    Status();
    if (!doInstall)
        return 0;
    for (i = 0; i < g_nPkg; i++)
    {
        UPKG *p = &g_Pkg[i];

        if (p->Installed)
            continue;
        if (Download(p) && Install(p, &reboot))
        {
            MarkInstalled(p->Id);
            Log(L"Installed: %ls", p->Title);
            if (p->Reboot)
                reboot = TRUE;
            ok++;
        }
        else
        {
            Log(L"Failed: %ls", p->Title);
            fail++;
        }
    }
    Log(L"Done: %d installed, %d failed.%ls", ok, fail, reboot ? L" Restart to finish." : L"");
    return fail ? 1 : 0;
}

static void History(void)
{
    WCHAR path[MAX_PATH], line[1024];
    FILE *f;

    wsprintfW(path, L"%ls\\history.log", g_Work);
    f = _wfopen(path, L"r");
    if (!f)
    {
        wprintf(L"No update history.\n");
        return;
    }
    while (fgetws(line, 1024, f))
        wprintf(L"%ls", line);
    fclose(f);
}

int wmain(int argc, WCHAR **argv)
{
    Init();
    if (argc >= 3 && !_wcsicmp(argv[1], L"/feed"))
    {
        HKEY hKey;

        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, WU_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKey, L"FeedUrl", 0, REG_SZ, (BYTE *)argv[2], (wcslen(argv[2]) + 1) * sizeof(WCHAR));
            RegCloseKey(hKey);
        }
        wcscpy(g_Feed, argv[2]);
        argv += 2;
        argc -= 2;
    }
    if (argc < 2 || !_wcsicmp(argv[1], L"scan"))
        return Run(FALSE);
    if (!_wcsicmp(argv[1], L"install"))
        return Run(TRUE);
    if (!_wcsicmp(argv[1], L"history"))
    {
        History();
        return 0;
    }
    wprintf(L"Usage: lakoupdate [/feed <url|path>] [scan | install | history]\n");
    return 1;
}
