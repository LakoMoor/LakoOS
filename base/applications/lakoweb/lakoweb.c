/*
 * PROJECT:     LakoOS Lako Wave setup
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Downloads Chromium for Windows, unpacks it, creates the shortcuts and starts it
 */

#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <zlib.h>

#define IDC_TEXT 100
#define IDC_BAR 101
#define WM_STEP (WM_APP + 1)
#define WM_DONE (WM_APP + 2)

static const WCHAR *LAST_URL = L"commondatastorage.googleapis.com";
static HWND g_hwnd;
static WCHAR g_status[260];
static int g_percent;
static WCHAR g_dir[MAX_PATH], g_zip[MAX_PATH];

static void Status(int pct, const WCHAR *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vswprintf(g_status, 250, fmt, ap);
    va_end(ap);
    g_percent = pct;
    PostMessageW(g_hwnd, WM_STEP, 0, 0);
}

static BOOL HttpGetText(const WCHAR *host, const WCHAR *path, char *out, DWORD cap)
{
    HINTERNET s, c, r;
    DWORD got = 0, n;
    BOOL ok = FALSE;

    s = WinHttpOpen(L"LakoOS-WaveSetup/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (s)
        WinHttpSetTimeouts(s, 10000, 10000, 15000, 15000);
    c = s ? WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL))
    {
        while (got < cap - 1 && WinHttpReadData(r, out + got, cap - 1 - got, &n) && n)
            got += n;
        out[got] = 0;
        ok = got > 0;
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return ok;
}

static BOOL DownloadPart(const WCHAR *host, const WCHAR *path, HANDLE f, ULONGLONG *pdone, ULONGLONG *ptotal)
{
    HINTERNET s, c, r;
    DWORD got, wr, len, status = 0;
    WCHAR hdr[64], cl[48];
    BYTE *buf = HeapAlloc(GetProcessHeap(), 0, 65536);
    BOOL ok = FALSE;

    s = WinHttpOpen(L"LakoOS-WaveSetup/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
    if (s)
        WinHttpSetTimeouts(s, 15000, 15000, 20000, 20000);
    c = s ? WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    if (r)
    {
        if (*pdone)
        {
            wsprintfW(hdr, L"Range: bytes=%I64u-", *pdone);
            WinHttpAddRequestHeaders(r, hdr, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
        }
        if (WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL))
        {
            len = sizeof(status);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &len, NULL);
            if (!*ptotal)
            {
                len = sizeof(cl);
                if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, NULL, cl, &len, NULL))
                    *ptotal = _wtoi64(cl);
            }
            if (status == 200 || status == 206)
            {
                if (status == 200 && *pdone)
                {
                    LARGE_INTEGER z = { 0 };
                    SetFilePointerEx(f, z, NULL, FILE_BEGIN);
                    *pdone = 0;
                }
                while (WinHttpReadData(r, buf, 65536, &got) && got)
                {
                    if (!WriteFile(f, buf, got, &wr, NULL) || wr != got)
                        break;
                    *pdone += got;
                    if (*ptotal)
                        Status((int)(*pdone * 60 / *ptotal), L"Downloading Chromium: %lu of %lu MB",
                               (ULONG)(*pdone >> 20), (ULONG)(*ptotal >> 20));
                }
                ok = TRUE;
            }
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    HeapFree(GetProcessHeap(), 0, buf);
    return ok;
}

static BOOL Download(const WCHAR *host, const WCHAR *path, const WCHAR *dest)
{
    HANDLE f = CreateFileW(dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ULONGLONG done = 0, total = 0;
    int tries;
    BOOL ok = FALSE;

    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    for (tries = 0; tries < 40 && !ok; tries++)
    {
        if (tries)
            Status((int)(total ? done * 60 / total : 0), L"Connection lost, resuming (%d)...", tries);
        DownloadPart(host, path, f, &done, &total);
        ok = total ? done >= total : done > 0;
    }
    CloseHandle(f);
    if (!ok)
        Status(0, L"The download failed at %lu MB of %lu MB.", (ULONG)(done >> 20), (ULONG)(total >> 20));
    return ok;
}

#pragma pack(push, 1)
typedef struct { DWORD sig; WORD ver, flags, method, time, date; DWORD crc, csize, usize; WORD nlen, elen; } LFH;
typedef struct { DWORD sig; WORD vmade, ver, flags, method, time, date; DWORD crc, csize, usize;
                 WORD nlen, elen, clen, disk, iattr; DWORD eattr, off; } CDH;
typedef struct { DWORD sig; WORD disk, cdisk, n, total; DWORD size, off; WORD clen; } EOCD;
#pragma pack(pop)

static void MakeDirs(WCHAR *path)
{
    WCHAR *p;

    for (p = path + 3; *p; p++)
    {
        if (*p == L'\\')
        {
            *p = 0;
            CreateDirectoryW(path, NULL);
            *p = L'\\';
        }
    }
}

static BOOL Unzip(const WCHAR *zip, const WCHAR *dest)
{
    HANDLE h = CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size, pos;
    BYTE *tail, *cd, *in, *out;
    DWORD tailLen, rd, i, n;
    EOCD *e = NULL;
    BOOL ok = FALSE;

    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    GetFileSizeEx(h, &size);
    tailLen = (DWORD)min(size.QuadPart, 70000);
    tail = HeapAlloc(GetProcessHeap(), 0, tailLen);
    pos.QuadPart = size.QuadPart - tailLen;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    ReadFile(h, tail, tailLen, &rd, NULL);
    for (i = tailLen - sizeof(EOCD); (int)i >= 0; i--)
        if (*(DWORD *)(tail + i) == 0x06054b50) { e = (EOCD *)(tail + i); break; }
    if (!e)
        goto done;
    cd = HeapAlloc(GetProcessHeap(), 0, e->size);
    pos.QuadPart = e->off;
    SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
    ReadFile(h, cd, e->size, &rd, NULL);
    in = HeapAlloc(GetProcessHeap(), 0, 4 << 20);
    out = HeapAlloc(GetProcessHeap(), 0, 4 << 20);
    {
        BYTE *p = cd;
        for (n = 0; n < e->total; n++)
        {
            CDH *ch = (CDH *)p;
            char name[1024];
            WCHAR wpath[MAX_PATH * 2], *q;
            LFH lfh;
            HANDLE o;
            DWORD w;
            ULONGLONG left;

            if (ch->sig != 0x02014b50)
                break;
            memcpy(name, p + sizeof(CDH), min(ch->nlen, 1023));
            name[min(ch->nlen, 1023)] = 0;
            p += sizeof(CDH) + ch->nlen + ch->elen + ch->clen;
            if (!(n % 40))
                Status(60 + (int)(n * 38 / e->total), L"Unpacking: %lu of %lu files", n, (ULONG)e->total);
            {
                const char *s = strchr(name, '/');
                if (!s) continue;       /* skip the leading chrome-win folder */
                s++;
                if (!*s) continue;
                lstrcpyW(wpath, dest);
                lstrcatW(wpath, L"\\");
                q = wpath + lstrlenW(wpath);
                MultiByteToWideChar(CP_UTF8, 0, s, -1, q, MAX_PATH);
                for (; *q; q++) if (*q == L'/') *q = L'\\';
                if (q[-1] == L'\\') continue;
            }
            MakeDirs(wpath);
            pos.QuadPart = ch->off;
            SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
            ReadFile(h, &lfh, sizeof(lfh), &rd, NULL);
            pos.QuadPart = (LONGLONG)ch->off + sizeof(lfh) + lfh.nlen + lfh.elen;
            SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
            o = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (o == INVALID_HANDLE_VALUE)
                continue;
            if (ch->method == 0)
            {
                left = ch->csize;
                while (left)
                {
                    DWORD want = (DWORD)min(left, 4 << 20);
                    ReadFile(h, in, want, &rd, NULL);
                    if (!rd) break;
                    WriteFile(o, in, rd, &w, NULL);
                    left -= rd;
                }
            }
            else if (ch->method == 8)
            {
                z_stream z;
                int zr = Z_OK;

                memset(&z, 0, sizeof(z));
                inflateInit2(&z, -15);
                left = ch->csize;
                while (zr != Z_STREAM_END)
                {
                    if (!z.avail_in)
                    {
                        DWORD want = (DWORD)min(left, 4 << 20);
                        if (!want) break;
                        ReadFile(h, in, want, &rd, NULL);
                        if (!rd) break;
                        left -= rd;
                        z.next_in = in;
                        z.avail_in = rd;
                    }
                    z.next_out = out;
                    z.avail_out = 4 << 20;
                    zr = inflate(&z, Z_NO_FLUSH);
                    if (zr < 0 && zr != Z_BUF_ERROR)
                        break;
                    WriteFile(o, out, (4 << 20) - z.avail_out, &w, NULL);
                }
                inflateEnd(&z);
            }
            CloseHandle(o);
        }
    }
    ok = TRUE;
done:
    CloseHandle(h);
    return ok;
}

static void MakeShortcut(const WCHAR *lnk, const WCHAR *target, const WCHAR *args, const WCHAR *dir)
{
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;

    if (SUCCEEDED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl)))
    {
        sl->lpVtbl->SetPath(sl, target);
        sl->lpVtbl->SetArguments(sl, args);
        sl->lpVtbl->SetWorkingDirectory(sl, dir);
        sl->lpVtbl->SetDescription(sl, L"Lako Wave");
        if (SUCCEEDED(sl->lpVtbl->QueryInterface(sl, &IID_IPersistFile, (void **)&pf)))
        {
            pf->lpVtbl->Save(pf, lnk, TRUE);
            pf->lpVtbl->Release(pf);
        }
        sl->lpVtbl->Release(sl);
    }
}

static BOOL FindSpace(WCHAR *root)
{
    DWORD mask = GetLogicalDrives();
    int i;

    for (i = 2; i < 26; i++)
    {
        WCHAR r[4] = { (WCHAR)(L'A' + i), L':', L'\\', 0 };
        ULARGE_INTEGER freeb, tot, tf;

        if (!(mask & (1u << i)) || GetDriveTypeW(r) != DRIVE_FIXED)
            continue;
        if (GetDiskFreeSpaceExW(r, &freeb, &tot, &tf) && freeb.QuadPart > (1800ULL << 20))
        {
            lstrcpyW(root, r);
            return TRUE;
        }
    }
    return FALSE;
}

static DWORD WINAPI Worker(LPVOID arg)
{
    char rev[64];
    WCHAR path[256], pf[MAX_PATH], exe[MAX_PATH], lnk[MAX_PATH], args[MAX_PATH];
    int ok = 0;

    CoInitialize(NULL);
    {
        WCHAR root[4];
        ULARGE_INTEGER fb, tb, tf;

        SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, pf);
        root[0] = pf[0]; root[1] = L':'; root[2] = L'\\'; root[3] = 0;
        if (!(GetDiskFreeSpaceExW(root, &fb, &tb, &tf) && fb.QuadPart > (1800ULL << 20)) && !FindSpace(root))
        {
            Status(0, L"Not enough free disk space (about 2 GB needed). Install LakoOS to a disk first.");
            goto end;
        }
        wsprintfW(g_dir, L"%sLako Wave", root);
        wsprintfW(g_zip, L"%schrome-win.zip", root);
    }
    Status(0, L"Finding the latest Chromium build...");
    {
        int t;
        for (t = 0; t < 12; t++)
        {
            if (HttpGetText(LAST_URL, L"/chromium-browser-snapshots/Win_x64/LAST_CHANGE", rev, sizeof(rev)))
                break;
            Status(0, L"Waiting for the network (%d)...", t + 1);
            Sleep(5000);
        }
        if (t == 12)
            rev[0] = 0;
    }
    if (!rev[0])
    {
        Status(0, L"Could not reach the download server. Check your network.");
        goto end;
    }
    {
        char *e = rev; while (*e >= '0' && *e <= '9') e++; *e = 0;
    }
    wsprintfW(path, L"/chromium-browser-snapshots/Win_x64/%S/chrome-win.zip", rev);
    CreateDirectoryW(g_dir, NULL);
    if (!Download(LAST_URL, path, g_zip))
    {
        goto end;
    }
    if (!Unzip(g_zip, g_dir))
    {
        Status(60, L"Could not unpack the download.");
        goto end;
    }
    DeleteFileW(g_zip);
    wsprintfW(exe, L"%s\\chrome.exe", g_dir);
    lstrcpyW(args, L"--no-sandbox --disable-gpu --no-first-run --user-data-dir=%LOCALAPPDATA%\\LakoWave");
    SHGetFolderPathW(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, pf);
    wsprintfW(lnk, L"%s\\Lako Wave.lnk", pf);
    MakeShortcut(lnk, exe, args, g_dir);
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, pf);
    wsprintfW(lnk, L"%s\\Lako Wave.lnk", pf);
    MakeShortcut(lnk, exe, args, g_dir);
    Status(100, L"Done. Starting Lako Wave...");
    ok = 1;
    ShellExecuteW(NULL, L"open", exe, args, g_dir, SW_SHOWNORMAL);
end:
    PostMessageW(g_hwnd, WM_DONE, ok, 0);
    return 0;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CREATE:
        g_hwnd = h;
        CreateWindowW(L"STATIC", L"Lako Wave downloads Chromium and installs it for you.", WS_CHILD | WS_VISIBLE,
                      20, 16, 420, 20, h, NULL, NULL, NULL);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 20, 46, 420, 20, h, (HMENU)IDC_TEXT, NULL, NULL);
        CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 20, 76, 420, 18, h, (HMENU)IDC_BAR, NULL, NULL);
        SendDlgItemMessageW(h, IDC_BAR, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        return 0;
    case WM_STEP:
        SetDlgItemTextW(h, IDC_TEXT, g_status);
        SendDlgItemMessageW(h, IDC_BAR, PBM_SETPOS, g_percent, 0);
        return 0;
    case WM_DONE:
        if (w)
            DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR cmd, int show)
{
    WNDCLASSW wc = { 0 };
    MSG msg;
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };

    InitCommonControlsEx(&icc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LakoWaveSetup";
    RegisterClassW(&wc);
    CreateWindowW(L"LakoWaveSetup", L"Get Lako Wave", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                  CW_USEDEFAULT, CW_USEDEFAULT, 470, 150, NULL, NULL, hInst, NULL);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
