/*
 * PROJECT:     LakoOS Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows 10 and Windows 11 style Start menu
 * COPYRIGHT:   Copyright 2026 LakoMoor
 */

#include "precomp.h"
#include <commctrl.h>
#include <windowsx.h>
#include "startmnu2.h"

enum
{
    SM11_IDC_SEARCH = 2000,
    SM11_MAX_PINNED = 18,
    SM11_MAX_TILES = 12,
    SM11_MAX_RECOMMENDED = 6,
    SM11M_EDITKEY = WM_APP + 61
};

enum SM11VIEW { SM11V_HOME, SM11V_ALL, SM11V_SEARCH };

enum SM11KIND
{
    SM11K_HEADER,
    SM11K_LETTER,
    SM11K_BUTTON_ALLAPPS,
    SM11K_BUTTON_BACK,
    SM11K_PIN,
    SM11K_RECOMMENDED,
    SM11K_ROW,
    SM11K_TILE,
    SM11K_SEARCH,
    SM11K_USER,
    SM11K_POWER,
    SM11K_RAIL_MENU,
    SM11K_RAIL_DOCS,
    SM11K_RAIL_SETTINGS
};

enum SM11POWERCMD
{
    SM11P_SHUTDOWN = 1,
    SM11P_RESTART,
    SM11P_LOGOFF,
    SM11P_LOCK
};

struct SM11APP
{
    CStringW szName;
    CStringW szPath;
    FILETIME ft;
    HICON hIcon;
    BOOL bIconTried;

    SM11APP() : hIcon(NULL), bIconTried(FALSE)
    {
        ft.dwLowDateTime = ft.dwHighDateTime = 0;
    }

    ~SM11APP()
    {
        if (hIcon)
            DestroyIcon(hIcon);
    }
};

struct SM11CELL
{
    RECT rc;
    int kind;
    int app;
    BOOL bDoc;
    BOOL bScroll;
    CStringW szText;
    CStringW szSub;
};

struct SM11PALETTE
{
    COLORREF Bg, Footer, Field, Hot, Text, Dim, Border, Tile;
};

static int __cdecl
SM11CompareByName(const void *a, const void *b)
{
    const SM11APP *pa = *(SM11APP * const *)a;
    const SM11APP *pb = *(SM11APP * const *)b;
    return lstrcmpiW(pa->szName, pb->szName);
}

static int __cdecl
SM11CompareByTimeDesc(const void *a, const void *b)
{
    const SM11APP *pa = *(SM11APP * const *)a;
    const SM11APP *pb = *(SM11APP * const *)b;
    return CompareFileTime(&pb->ft, &pa->ft);
}

static VOID
SM11EnumLinks(LPCWSTR pszDir, int nDepth, CAtlArray<SM11APP*> &Out)
{
    WCHAR szPattern[MAX_PATH];
    WIN32_FIND_DATAW fd;

    if (!PathCombineW(szPattern, pszDir, L"*"))
        return;

    HANDLE hFind = FindFirstFileW(szPattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (fd.cFileName[0] == L'.')
            continue;

        WCHAR szFull[MAX_PATH];
        if (!PathCombineW(szFull, pszDir, fd.cFileName))
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (nDepth < 3 && lstrcmpiW(fd.cFileName, L"Startup"))
                SM11EnumLinks(szFull, nDepth + 1, Out);
            continue;
        }

        LPWSTR pszExt = PathFindExtensionW(fd.cFileName);
        if (lstrcmpiW(pszExt, L".lnk"))
            continue;
        *pszExt = 0;

        if (StrStrIW(fd.cFileName, L"uninstall"))
            continue;

        SM11APP *pApp = new SM11APP();
        pApp->szName = fd.cFileName;
        pApp->szPath = szFull;
        pApp->ft = fd.ftLastWriteTime;
        Out.Add(pApp);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

static VOID
SM11FreeApps(CAtlArray<SM11APP*> &Apps)
{
    for (SIZE_T i = 0; i < Apps.GetCount(); i++)
        delete Apps[i];
    Apps.SetCount(0);
}

static BOOL
SM11IsLightTheme(VOID)
{
    DWORD dwValue = 0, cbData = sizeof(dwValue);
    if (SHGetValueW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"SystemUsesLightTheme", NULL, &dwValue, &cbData) == ERROR_SUCCESS)
    {
        return dwValue != 0;
    }
    return TRUE;
}

typedef CWinTraits<WS_POPUP | WS_CLIPCHILDREN, WS_EX_TOOLWINDOW | WS_EX_TOPMOST> CStartMenu11Traits;

class CStartMenu11Wnd :
    public CWindowImpl<CStartMenu11Wnd, CWindow, CStartMenu11Traits>
{
public:
    DECLARE_WND_CLASS_EX(L"StartMenu11", 0, COLOR_WINDOW)

    ITrayWindow *m_Tray;
    HWND m_hwndTray;
    HWND m_hwndEdit;
    HFONT m_hFont;
    HFONT m_hFontBold;
    HFONT m_hFontSmall;
    HBRUSH m_hbrField;
    int m_iDpi;
    BOOL m_bWin10;
    BOOL m_bLight;
    BOOL m_bInMenu;
    BOOL m_bTracking;
    ULONGLONG m_LastDismiss;
    SM11PALETTE m_Pal;

    CAtlArray<SM11APP*> m_Apps;
    CAtlArray<SM11APP*> m_Docs;
    CAtlArray<int> m_Pinned;
    CAtlArray<int> m_Results;
    CAtlArray<SM11CELL> m_Cells;

    int m_nView;
    int m_nScroll;
    int m_nContentH;
    int m_iHot;
    int m_iPressed;
    SIZE m_size;
    RECT m_rcList;
    WCHAR m_szUser[128];

    CStartMenu11Wnd() :
        m_Tray(NULL), m_hwndTray(NULL), m_hwndEdit(NULL),
        m_hFont(NULL), m_hFontBold(NULL), m_hFontSmall(NULL), m_hbrField(NULL),
        m_iDpi(96), m_bWin10(FALSE), m_bLight(FALSE), m_bInMenu(FALSE), m_bTracking(FALSE),
        m_LastDismiss(0), m_nView(SM11V_HOME), m_nScroll(0), m_nContentH(0),
        m_iHot(-1), m_iPressed(-1)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(&m_size, sizeof(m_size));
        ZeroMemory(&m_rcList, sizeof(m_rcList));
        m_szUser[0] = 0;
    }

    ~CStartMenu11Wnd()
    {
        FreeData();
        FreeGdi();
    }

    int Sc(int v) const { return MulDiv(v, m_iDpi, 96); }

    BOOL Init(ITrayWindow *pTray, HWND hwndTray)
    {
        m_Tray = pTray;
        m_hwndTray = hwndTray;

        HDC hdc = ::GetDC(NULL);
        if (hdc)
        {
            m_iDpi = GetDeviceCaps(hdc, LOGPIXELSY);
            ::ReleaseDC(NULL, hdc);
        }
        if (m_iDpi <= 0)
            m_iDpi = 96;

        DWORD cch = _countof(m_szUser);
        if (!GetUserNameW(m_szUser, &cch))
            lstrcpyW(m_szUser, L"User");

        if (!Create(hwndTray, CWindow::rcDefault, NULL))
            return FALSE;

        m_hwndEdit = ::CreateWindowExW(0, L"EDIT", L"",
                                       WS_CHILD | ES_AUTOHSCROLL,
                                       0, 0, 10, 10, m_hWnd,
                                       (HMENU)(UINT_PTR)SM11_IDC_SEARCH,
                                       hExplorerInstance, NULL);
        if (m_hwndEdit)
            SetWindowSubclass(m_hwndEdit, EditSubclassProc, 1, (DWORD_PTR)m_hWnd);
        return TRUE;
    }

    VOID Term()
    {
        if (IsWindow())
            DestroyWindow();
    }

    BOOL IsMenuVisible()
    {
        return IsWindow() && IsWindowVisible();
    }

    static LRESULT CALLBACK
    EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef)
    {
        switch (uMsg)
        {
            case WM_KEYDOWN:
                if (wParam == VK_RETURN || wParam == VK_ESCAPE)
                {
                    ::PostMessageW((HWND)dwRef, SM11M_EDITKEY, wParam, 0);
                    return 0;
                }
                break;
            case WM_CHAR:
                if (wParam == VK_RETURN || wParam == VK_ESCAPE)
                    return 0;
                break;
            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, EditSubclassProc, uId);
                break;
        }
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    VOID FreeGdi()
    {
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontBold) { DeleteObject(m_hFontBold); m_hFontBold = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        if (m_hbrField) { DeleteObject(m_hbrField); m_hbrField = NULL; }
    }

    VOID FreeData()
    {
        SM11FreeApps(m_Apps);
        SM11FreeApps(m_Docs);
        m_Pinned.SetCount(0);
        m_Results.SetCount(0);
        m_Cells.SetCount(0);
    }

    VOID SelectPalette()
    {
        m_bLight = SM11IsLightTheme();
        if (m_bLight)
        {
            m_Pal.Bg = RGB(243, 243, 243);
            m_Pal.Footer = RGB(234, 234, 234);
            m_Pal.Field = RGB(255, 255, 255);
            m_Pal.Hot = RGB(226, 226, 226);
            m_Pal.Text = RGB(0, 0, 0);
            m_Pal.Dim = RGB(96, 96, 96);
            m_Pal.Border = RGB(208, 208, 208);
        }
        else
        {
            m_Pal.Bg = RGB(32, 32, 32);
            m_Pal.Footer = RGB(26, 26, 26);
            m_Pal.Field = RGB(48, 48, 48);
            m_Pal.Hot = RGB(62, 62, 62);
            m_Pal.Text = RGB(255, 255, 255);
            m_Pal.Dim = RGB(176, 176, 176);
            m_Pal.Border = RGB(70, 70, 70);
        }
        m_Pal.Tile = GetSysColor(COLOR_HIGHLIGHT);

        if (m_hbrField)
            DeleteObject(m_hbrField);
        m_hbrField = CreateSolidBrush(m_Pal.Field);
    }

    VOID CreateFonts()
    {
        FreeFontsOnly();
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

        LOGFONTW lf = ncm.lfMessageFont;
        lf.lfHeight = -Sc(12);
        lf.lfWeight = FW_NORMAL;
        m_hFont = CreateFontIndirectW(&lf);

        lf.lfHeight = -Sc(14);
        lf.lfWeight = FW_SEMIBOLD;
        m_hFontBold = CreateFontIndirectW(&lf);

        lf.lfHeight = -Sc(11);
        lf.lfWeight = FW_NORMAL;
        m_hFontSmall = CreateFontIndirectW(&lf);

        if (m_hwndEdit)
            ::SendMessageW(m_hwndEdit, WM_SETFONT, (WPARAM)m_hFontBold, FALSE);
    }

    VOID FreeFontsOnly()
    {
        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontBold) { DeleteObject(m_hFontBold); m_hFontBold = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
    }

    VOID RebuildData()
    {
        FreeData();

        WCHAR szPath[MAX_PATH];
        if (SHGetSpecialFolderPathW(NULL, szPath, CSIDL_PROGRAMS, FALSE))
            SM11EnumLinks(szPath, 0, m_Apps);
        if (SHGetSpecialFolderPathW(NULL, szPath, CSIDL_COMMON_PROGRAMS, FALSE))
            SM11EnumLinks(szPath, 0, m_Apps);

        SIZE_T count = m_Apps.GetCount();
        if (count > 1)
            qsort(m_Apps.GetData(), count, sizeof(SM11APP*), SM11CompareByName);

        CAtlArray<SM11APP*> Unique;
        for (SIZE_T i = 0; i < m_Apps.GetCount(); i++)
        {
            if (Unique.GetCount() &&
                !lstrcmpiW(m_Apps[i]->szName, Unique[Unique.GetCount() - 1]->szName))
            {
                delete m_Apps[i];
            }
            else
            {
                Unique.Add(m_Apps[i]);
            }
        }
        m_Apps.SetCount(0);
        for (SIZE_T i = 0; i < Unique.GetCount(); i++)
            m_Apps.Add(Unique[i]);

        for (SIZE_T i = 0; i < m_Apps.GetCount() && i < (SIZE_T)SM11_MAX_PINNED; i++)
            m_Pinned.Add((int)i);

        if (SHGetSpecialFolderPathW(NULL, szPath, CSIDL_RECENT, FALSE))
            SM11EnumLinks(szPath, 3, m_Docs);
        count = m_Docs.GetCount();
        if (count > 1)
            qsort(m_Docs.GetData(), count, sizeof(SM11APP*), SM11CompareByTimeDesc);
    }

    SM11APP* GetApp(const SM11CELL &cell)
    {
        CAtlArray<SM11APP*> &List = cell.bDoc ? m_Docs : m_Apps;
        if (cell.app < 0 || (SIZE_T)cell.app >= List.GetCount())
            return NULL;
        return List[cell.app];
    }

    VOID AddCell(int kind, int left, int top, int right, int bottom,
                 LPCWSTR pszText = NULL, int app = -1, BOOL bDoc = FALSE,
                 BOOL bScroll = FALSE, LPCWSTR pszSub = NULL)
    {
        SM11CELL cell;
        SetRect(&cell.rc, left, top, right, bottom);
        cell.kind = kind;
        cell.app = app;
        cell.bDoc = bDoc;
        cell.bScroll = bScroll;
        if (pszText)
            cell.szText = pszText;
        if (pszSub)
            cell.szSub = pszSub;
        m_Cells.Add(cell);
    }

    VOID GetSearchText(CStringW &szText)
    {
        WCHAR szBuf[256] = L"";
        if (m_hwndEdit)
            ::GetWindowTextW(m_hwndEdit, szBuf, _countof(szBuf));
        szText = szBuf;
        szText.Trim();
    }

    VOID UpdateSearch()
    {
        CStringW szText;
        GetSearchText(szText);

        m_Results.SetCount(0);
        if (szText.IsEmpty())
        {
            if (m_nView == SM11V_SEARCH)
                m_nView = SM11V_HOME;
        }
        else
        {
            m_nView = SM11V_SEARCH;
            for (SIZE_T i = 0; i < m_Apps.GetCount(); i++)
            {
                if (StrStrIW(m_Apps[i]->szName, szText))
                    m_Results.Add((int)i);
            }
        }
        m_nScroll = 0;
        m_iHot = -1;
        Layout();
        InvalidateRect(NULL, FALSE);
    }

    VOID LayoutWin11()
    {
        int W = m_size.cx, H = m_size.cy;
        int pad = Sc(32);
        int inner = W - 2 * pad;
        int footerTop = H - Sc(72);

        AddCell(SM11K_SEARCH, pad, Sc(24), W - pad, Sc(60));

        int listTop = Sc(116);
        SetRect(&m_rcList, pad, listTop, W - pad, footerTop - Sc(8));

        if (m_nView == SM11V_HOME)
        {
            AddCell(SM11K_HEADER, pad, Sc(82), pad + Sc(200), Sc(110), L"Pinned");
            AddCell(SM11K_BUTTON_ALLAPPS, W - pad - Sc(100), Sc(80), W - pad, Sc(110), L"All apps");

            int cw = inner / 6, ch = Sc(86);
            int pinRows = 3, recRows = 3;
            while (listTop + pinRows * ch + Sc(64) + recRows * Sc(52) > footerTop - Sc(8))
            {
                if (recRows > 1)
                    recRows--;
                else if (pinRows > 1)
                    pinRows--;
                else
                {
                    recRows = 0;
                    break;
                }
            }
            for (SIZE_T i = 0; i < m_Pinned.GetCount() && i < (SIZE_T)(pinRows * 6); i++)
            {
                int col = (int)(i % 6), row = (int)(i / 6);
                AddCell(SM11K_PIN, pad + col * cw, listTop + row * ch,
                        pad + (col + 1) * cw, listTop + (row + 1) * ch,
                        m_Apps[m_Pinned[i]]->szName, m_Pinned[i]);
            }

            int recTop = listTop + pinRows * ch + Sc(20);
            if (recRows > 0)
                AddCell(SM11K_HEADER, pad, recTop, pad + Sc(200), recTop + Sc(28), L"Recommended");

            BOOL bDocs = m_Docs.GetCount() > 0;
            CAtlArray<SM11APP*> &Src = bDocs ? m_Docs : m_Apps;
            int n = 0;
            int rw = inner / 2, rh = Sc(52);
            int ry = recTop + Sc(36);
            CAtlArray<int> Order;
            if (bDocs)
            {
                for (SIZE_T i = 0; i < Src.GetCount(); i++)
                    Order.Add((int)i);
            }
            else
            {
                CAtlArray<SM11APP*> ByTime;
                for (SIZE_T i = 0; i < Src.GetCount(); i++)
                    ByTime.Add(Src[i]);
                if (ByTime.GetCount() > 1)
                    qsort(ByTime.GetData(), ByTime.GetCount(), sizeof(SM11APP*), SM11CompareByTimeDesc);
                for (SIZE_T i = 0; i < ByTime.GetCount(); i++)
                {
                    for (SIZE_T j = 0; j < Src.GetCount(); j++)
                    {
                        if (Src[j] == ByTime[i])
                        {
                            Order.Add((int)j);
                            break;
                        }
                    }
                }
            }
            for (SIZE_T i = 0; i < Order.GetCount() && n < recRows * 2; i++, n++)
            {
                int col = n % 2, row = n / 2;
                AddCell(SM11K_RECOMMENDED, pad + col * rw, ry + row * rh,
                        pad + (col + 1) * rw, ry + (row + 1) * rh,
                        Src[Order[i]]->szName, Order[i], bDocs, FALSE,
                        bDocs ? L"Recent" : L"Recently added");
            }
        }
        else if (m_nView == SM11V_ALL)
        {
            AddCell(SM11K_BUTTON_BACK, pad, Sc(80), pad + Sc(100), Sc(110), L"Back");
            AddCell(SM11K_HEADER, pad + Sc(120), Sc(82), pad + Sc(320), Sc(110), L"All apps");
            LayoutRows(m_Apps.GetCount(), NULL, m_rcList.left, m_rcList.right, m_rcList.top, FALSE);
        }
        else
        {
            AddCell(SM11K_HEADER, pad, Sc(82), pad + Sc(300), Sc(110), L"Best match");
            LayoutRows(m_Results.GetCount(), m_Results.GetData(), m_rcList.left, m_rcList.right, m_rcList.top, FALSE);
        }

        AddCell(SM11K_USER, pad, footerTop + Sc(12), pad + Sc(240), H - Sc(12), m_szUser);
        AddCell(SM11K_POWER, W - pad - Sc(40), footerTop + Sc(16), W - pad, H - Sc(16), L"Power");
    }

    VOID LayoutRows(SIZE_T count, const int *pIndex, int left, int right, int top, BOOL bLetters)
    {
        int rowH = Sc(bLetters ? 40 : 44);
        int y = top - m_nScroll;
        WCHAR lastLetter = 0;

        for (SIZE_T i = 0; i < count; i++)
        {
            int idx = pIndex ? pIndex[i] : (int)i;
            SM11APP *pApp = m_Apps[idx];

            if (bLetters)
            {
                WCHAR ch = pApp->szName.GetLength() ? pApp->szName[0] : L'#';
                if (ch >= L'a' && ch <= L'z')
                    ch = (WCHAR)(ch - L'a' + L'A');
                if (!((ch >= L'A' && ch <= L'Z')))
                    ch = L'#';
                if (ch != lastLetter)
                {
                    WCHAR szLetter[2] = { ch, 0 };
                    AddCell(SM11K_LETTER, left, y, right, y + Sc(32), szLetter, -1, FALSE, TRUE);
                    y += Sc(32);
                    lastLetter = ch;
                }
            }

            AddCell(SM11K_ROW, left, y, right, y + rowH, pApp->szName, idx, FALSE, TRUE);
            y += rowH;
        }

        m_nContentH = (y + m_nScroll) - top;
        int viewH = m_rcList.bottom - m_rcList.top;
        int maxScroll = max(0, m_nContentH - viewH);
        if (m_nScroll > maxScroll)
            m_nScroll = maxScroll;
    }

    VOID LayoutWin10()
    {
        int W = m_size.cx, H = m_size.cy;
        int rail = Sc(48);
        int listW = Sc(284);
        int tilesLeft = rail + listW + Sc(16);
        int tilesRight = W - Sc(16);

        AddCell(SM11K_RAIL_MENU, 0, 0, rail, Sc(48));
        AddCell(SM11K_USER, 0, H - Sc(48) * 4, rail, H - Sc(48) * 3, m_szUser);
        AddCell(SM11K_RAIL_DOCS, 0, H - Sc(48) * 3, rail, H - Sc(48) * 2, L"Documents");
        AddCell(SM11K_RAIL_SETTINGS, 0, H - Sc(48) * 2, rail, H - Sc(48), L"Settings");
        AddCell(SM11K_POWER, 0, H - Sc(48), rail, H, L"Power");

        int searchH = Sc(44);
        SetRect(&m_rcList, rail, Sc(8), rail + listW, H - searchH - Sc(4));
        AddCell(SM11K_SEARCH, rail + Sc(8), H - searchH, rail + listW - Sc(8), H - Sc(8));

        if (m_nView == SM11V_SEARCH)
        {
            LayoutRows(m_Results.GetCount(), m_Results.GetData(), m_rcList.left, m_rcList.right, m_rcList.top, FALSE);
        }
        else
        {
            LayoutRows(m_Apps.GetCount(), NULL, m_rcList.left, m_rcList.right, m_rcList.top, TRUE);
        }

        AddCell(SM11K_HEADER, tilesLeft, Sc(16), tilesRight, Sc(44), L"Pinned");
        int gap = Sc(4);
        int tw = (tilesRight - tilesLeft - 2 * gap) / 3;
        int th = tw;
        for (SIZE_T i = 0; i < m_Pinned.GetCount() && i < (SIZE_T)SM11_MAX_TILES; i++)
        {
            int col = (int)(i % 3), row = (int)(i / 3);
            int x = tilesLeft + col * (tw + gap);
            int y = Sc(52) + row * (th + gap);
            if (y + th > H - Sc(8))
                break;
            AddCell(SM11K_TILE, x, y, x + tw, y + th, m_Apps[m_Pinned[i]]->szName, m_Pinned[i]);
        }
    }

    VOID Layout()
    {
        m_Cells.SetCount(0);
        if (m_bWin10)
            LayoutWin10();
        else
            LayoutWin11();
        PositionEdit();
    }

    VOID PositionEdit()
    {
        if (!m_hwndEdit)
            return;
        for (SIZE_T i = 0; i < m_Cells.GetCount(); i++)
        {
            if (m_Cells[i].kind == SM11K_SEARCH)
            {
                RECT rc = m_Cells[i].rc;
                int h = Sc(20);
                ::MoveWindow(m_hwndEdit, rc.left + Sc(14), (rc.top + rc.bottom - h) / 2,
                             rc.right - rc.left - Sc(52), h, TRUE);
                ::ShowWindow(m_hwndEdit, SW_SHOW);
                return;
            }
        }
    }

    HICON GetFluent(UINT nId, int cx)
    {
        static struct { UINT nId; int cx; HICON hIcon; } s_Cache[32];

        for (UINT i = 0; i < _countof(s_Cache); i++)
        {
            if (s_Cache[i].nId == nId && s_Cache[i].cx == cx)
                return s_Cache[i].hIcon;
        }

        HICON hIcon = (HICON)LoadImageW(hExplorerInstance, MAKEINTRESOURCEW(nId),
                                        IMAGE_ICON, cx, cx, 0);
        for (UINT i = 0; i < _countof(s_Cache); i++)
        {
            if (!s_Cache[i].nId)
            {
                s_Cache[i].nId = nId;
                s_Cache[i].cx = cx;
                s_Cache[i].hIcon = hIcon;
                break;
            }
        }
        return hIcon;
    }

    VOID DrawFluent(HDC hdc, const RECT *prc, UINT nId, int cx)
    {
        static const int s_Sizes[] = { 16, 20, 24, 28, 32, 40, 48 };
        int n = s_Sizes[0];
        for (UINT i = 0; i < _countof(s_Sizes); i++)
        {
            if (s_Sizes[i] <= cx)
                n = s_Sizes[i];
        }
        HICON hIcon = GetFluent(nId, n);
        if (hIcon)
        {
            int w = prc->right - prc->left, h = prc->bottom - prc->top;
            DrawIconEx(hdc, prc->left + (w - n) / 2, prc->top + (h - n) / 2,
                       hIcon, n, n, 0, NULL, DI_NORMAL);
        }
    }

    VOID DrawAppIcon(HDC hdc, SM11APP *pApp, int x, int y, int size)
    {
        if (!pApp->bIconTried)
        {
            pApp->bIconTried = TRUE;
            SHFILEINFOW sfi = { 0 };
            if (SHGetFileInfoW(pApp->szPath, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON))
                pApp->hIcon = sfi.hIcon;
        }
        if (pApp->hIcon)
            DrawIconEx(hdc, x, y, pApp->hIcon, size, size, 0, NULL, DI_NORMAL);
    }

    VOID FillRound(HDC hdc, const RECT *prc, COLORREF cr, int radius, COLORREF crBorder = CLR_NONE)
    {
        HBRUSH hbr = CreateSolidBrush(cr);
        HPEN hPen = crBorder == CLR_NONE ? (HPEN)GetStockObject(NULL_PEN) : CreatePen(PS_SOLID, 1, crBorder);
        HGDIOBJ hOldB = SelectObject(hdc, hbr);
        HGDIOBJ hOldP = SelectObject(hdc, hPen);
        RoundRect(hdc, prc->left, prc->top, prc->right + 1, prc->bottom + 1, radius, radius);
        SelectObject(hdc, hOldP);
        SelectObject(hdc, hOldB);
        if (crBorder != CLR_NONE)
            DeleteObject(hPen);
        DeleteObject(hbr);
    }

    VOID DrawText1(HDC hdc, LPCWSTR psz, RECT rc, HFONT hFont, COLORREF cr, UINT uFlags)
    {
        SelectObject(hdc, hFont);
        SetTextColor(hdc, cr);
        DrawTextW(hdc, psz, -1, &rc, uFlags | DT_NOPREFIX);
    }

    VOID DrawCell(HDC hdc, int index)
    {
        SM11CELL &cell = m_Cells[index];
        RECT rc = cell.rc;
        BOOL bHot = (index == m_iHot);
        int r = Sc(m_bWin10 ? 0 : 6) * 2;
        SM11APP *pApp = GetApp(cell);

        switch (cell.kind)
        {
            case SM11K_HEADER:
                DrawText1(hdc, cell.szText, rc, m_hFontBold, m_Pal.Text, DT_SINGLELINE | DT_VCENTER);
                break;

            case SM11K_LETTER:
            {
                RECT rcT = rc;
                rcT.left += Sc(12);
                DrawText1(hdc, cell.szText, rcT, m_hFontBold, m_Pal.Tile, DT_SINGLELINE | DT_VCENTER);
                break;
            }

            case SM11K_BUTTON_ALLAPPS:
            case SM11K_BUTTON_BACK:
            {
                if (bHot)
                    FillRound(hdc, &rc, m_Pal.Hot, r);
                else
                    FillRound(hdc, &rc, m_Pal.Field, r, m_Pal.Border);
                RECT rcT = rc;
                int ic = Sc(16);
                if (cell.kind == SM11K_BUTTON_ALLAPPS)
                {
                    if (m_bLight)
                    {
                        CStringW szLabel = cell.szText + L"  \x203A";
                        DrawText1(hdc, szLabel, rcT, m_hFont, m_Pal.Text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
                    }
                    else
                    {
                        rcT.right -= ic + Sc(8);
                        DrawText1(hdc, cell.szText, rcT, m_hFont, m_Pal.Text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
                        RECT rcI = { rc.right - ic - Sc(8), rc.top, rc.right - Sc(6), rc.bottom };
                        DrawFluent(hdc, &rcI, IDI_FLU_CHEVRIGHT, ic);
                    }
                }
                else if (m_bLight)
                {
                    CStringW szLabel = L"\x2039  " + cell.szText;
                    DrawText1(hdc, szLabel, rcT, m_hFont, m_Pal.Text, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
                }
                else
                {
                    RECT rcI = { rc.left + Sc(6), rc.top, rc.left + Sc(6) + ic + Sc(4), rc.bottom };
                    DrawFluent(hdc, &rcI, IDI_FLU_ARROWLEFT, ic);
                    rcT.left = rcI.right;
                    DrawText1(hdc, cell.szText, rcT, m_hFont, m_Pal.Text, DT_SINGLELINE | DT_VCENTER);
                }
                break;
            }

            case SM11K_PIN:
            {
                if (bHot)
                    FillRound(hdc, &rc, m_Pal.Hot, r);
                int isz = Sc(32);
                if (pApp)
                    DrawAppIcon(hdc, pApp, (rc.left + rc.right - isz) / 2, rc.top + Sc(10), isz);
                RECT rcT = { rc.left + Sc(4), rc.top + Sc(10) + isz + Sc(6), rc.right - Sc(4), rc.bottom - Sc(2) };
                DrawText1(hdc, cell.szText, rcT, m_hFontSmall, m_Pal.Text,
                          DT_CENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_EDITCONTROL);
                break;
            }

            case SM11K_RECOMMENDED:
            {
                if (bHot)
                    FillRound(hdc, &rc, m_Pal.Hot, r);
                int isz = Sc(32);
                if (pApp)
                    DrawAppIcon(hdc, pApp, rc.left + Sc(12), (rc.top + rc.bottom - isz) / 2, isz);
                RECT rcN = { rc.left + Sc(56), rc.top + Sc(6), rc.right - Sc(8), (rc.top + rc.bottom) / 2 + Sc(2) };
                DrawText1(hdc, cell.szText, rcN, m_hFont, m_Pal.Text,
                          DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS);
                RECT rcS = { rc.left + Sc(56), (rc.top + rc.bottom) / 2 + Sc(2), rc.right - Sc(8), rc.bottom - Sc(6) };
                DrawText1(hdc, cell.szSub, rcS, m_hFontSmall, m_Pal.Dim,
                          DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS);
                break;
            }

            case SM11K_ROW:
            {
                if (bHot)
                    FillRound(hdc, &rc, m_Pal.Hot, r);
                int isz = Sc(m_bWin10 ? 24 : 28);
                if (pApp)
                    DrawAppIcon(hdc, pApp, rc.left + Sc(12), (rc.top + rc.bottom - isz) / 2, isz);
                RECT rcT = { rc.left + Sc(12) + isz + Sc(12), rc.top, rc.right - Sc(8), rc.bottom };
                DrawText1(hdc, cell.szText, rcT, m_hFont, m_Pal.Text,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                break;
            }

            case SM11K_TILE:
            {
                COLORREF cr = bHot ? ShellLiftColor(m_Pal.Tile, 24) : m_Pal.Tile;
                HBRUSH hbr = CreateSolidBrush(cr);
                FillRect(hdc, &rc, hbr);
                DeleteObject(hbr);
                int isz = Sc(32);
                if (pApp)
                    DrawAppIcon(hdc, pApp, (rc.left + rc.right - isz) / 2, rc.top + Sc(14), isz);
                RECT rcT = { rc.left + Sc(8), rc.bottom - Sc(34), rc.right - Sc(6), rc.bottom - Sc(6) };
                DrawText1(hdc, cell.szText, rcT, m_hFontSmall, RGB(255, 255, 255),
                          DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);
                break;
            }

            case SM11K_SEARCH:
            {
                FillRound(hdc, &rc, m_Pal.Field, r, m_Pal.Border);
                int ic = Sc(16);
                RECT rcI = { rc.right - ic - Sc(16), rc.top, rc.right - Sc(8), rc.bottom };
                if (!m_bLight)
                    DrawFluent(hdc, &rcI, IDI_FLU_SEARCH, ic);
                break;
            }

            case SM11K_USER:
            {
                if (bHot)
                    FillRound(hdc, &rc, m_Pal.Hot, r);
                int d = Sc(32);
                RECT rcC;
                if (m_bWin10)
                    SetRect(&rcC, (rc.left + rc.right - d) / 2, (rc.top + rc.bottom - d) / 2,
                            (rc.left + rc.right + d) / 2, (rc.top + rc.bottom + d) / 2);
                else
                    SetRect(&rcC, rc.left + Sc(8), (rc.top + rc.bottom - d) / 2,
                            rc.left + Sc(8) + d, (rc.top + rc.bottom + d) / 2);
                HBRUSH hbr = CreateSolidBrush(m_Pal.Tile);
                HGDIOBJ hOldB = SelectObject(hdc, hbr);
                HGDIOBJ hOldP = SelectObject(hdc, GetStockObject(NULL_PEN));
                Ellipse(hdc, rcC.left, rcC.top, rcC.right + 1, rcC.bottom + 1);
                SelectObject(hdc, hOldP);
                SelectObject(hdc, hOldB);
                DeleteObject(hbr);
                WCHAR szInit[2] = { cell.szText.GetLength() ? cell.szText[0] : L'U', 0 };
                DrawText1(hdc, szInit, rcC, m_hFontBold, RGB(255, 255, 255),
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER);
                if (!m_bWin10)
                {
                    RECT rcT = { rcC.right + Sc(12), rc.top, rc.right, rc.bottom };
                    DrawText1(hdc, cell.szText, rcT, m_hFont, m_Pal.Text,
                              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                }
                break;
            }

            case SM11K_POWER:
            case SM11K_RAIL_MENU:
            case SM11K_RAIL_DOCS:
            case SM11K_RAIL_SETTINGS:
            {
                if (bHot)
                    FillRound(hdc, &rc, (m_bWin10 && m_bLight) ? RGB(62, 62, 62) : m_Pal.Hot, r);
                if (cell.kind == SM11K_POWER && m_bLight && !m_bWin10)
                {
                    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2, rr = Sc(7);
                    HPEN hPen = CreatePen(PS_SOLID, max(1, Sc(2)), m_Pal.Text);
                    HGDIOBJ hOld = SelectObject(hdc, hPen);
                    Arc(hdc, cx - rr, cy - rr, cx + rr, cy + rr,
                        cx - rr / 2, cy - rr, cx + rr / 2, cy - rr);
                    MoveToEx(hdc, cx, cy - rr - Sc(1), NULL);
                    LineTo(hdc, cx, cy - Sc(1));
                    SelectObject(hdc, hOld);
                    DeleteObject(hPen);
                    break;
                }
                UINT nId = IDI_FLU_PLUG;
                if (cell.kind == SM11K_RAIL_MENU)
                    nId = IDI_FLU_NAVIGATION;
                else if (cell.kind == SM11K_RAIL_DOCS)
                    nId = IDI_FLU_DOCUMENT;
                else if (cell.kind == SM11K_RAIL_SETTINGS)
                    nId = IDI_FLU_SETTINGS;
                DrawFluent(hdc, &rc, nId, Sc(20));
                break;
            }
        }
    }

    VOID PaintTo(HDC hdc)
    {
        RECT rcAll = { 0, 0, m_size.cx, m_size.cy };
        HBRUSH hbr = CreateSolidBrush(m_Pal.Bg);
        FillRect(hdc, &rcAll, hbr);
        DeleteObject(hbr);

        if (m_bWin10)
        {
            RECT rcRail = { 0, 0, Sc(48), m_size.cy };
            hbr = CreateSolidBrush(m_bLight ? RGB(32, 32, 32) : m_Pal.Footer);
            FillRect(hdc, &rcRail, hbr);
            DeleteObject(hbr);
        }
        else
        {
            RECT rcFooter = { 0, m_size.cy - Sc(72), m_size.cx, m_size.cy };
            hbr = CreateSolidBrush(m_Pal.Footer);
            FillRect(hdc, &rcFooter, hbr);
            DeleteObject(hbr);
            hbr = CreateSolidBrush(m_Pal.Border);
            RECT rcLine = { 0, rcFooter.top, m_size.cx, rcFooter.top + 1 };
            FillRect(hdc, &rcLine, hbr);
            DeleteObject(hbr);
        }

        SetBkMode(hdc, TRANSPARENT);
        for (SIZE_T i = 0; i < m_Cells.GetCount(); i++)
        {
            if (m_Cells[i].bScroll)
            {
                int saved = SaveDC(hdc);
                IntersectClipRect(hdc, m_rcList.left, m_rcList.top, m_rcList.right, m_rcList.bottom);
                DrawCell(hdc, (int)i);
                RestoreDC(hdc, saved);
            }
            else
            {
                DrawCell(hdc, (int)i);
            }
        }
    }

    int HitTest(int x, int y)
    {
        POINT pt = { x, y };
        for (INT i = (INT)m_Cells.GetCount() - 1; i >= 0; i--)
        {
            SM11CELL &cell = m_Cells[i];
            if (cell.kind == SM11K_HEADER || cell.kind == SM11K_LETTER || cell.kind == SM11K_SEARCH)
                continue;
            if (!PtInRect(&cell.rc, pt))
                continue;
            if (cell.bScroll && !PtInRect(&m_rcList, pt))
                continue;
            return i;
        }
        return -1;
    }

    VOID ShowPowerMenu(const RECT *prcCell)
    {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu)
            return;
        AppendMenuW(hMenu, MF_STRING, SM11P_LOCK, L"Lock");
        AppendMenuW(hMenu, MF_STRING, SM11P_LOGOFF, L"Sign out");
        AppendMenuW(hMenu, MF_STRING, SM11P_SHUTDOWN, L"Shut down");
        AppendMenuW(hMenu, MF_STRING, SM11P_RESTART, L"Restart");

        POINT pt = { prcCell->left, prcCell->top };
        ::ClientToScreen(m_hWnd, &pt);

        m_bInMenu = TRUE;
        UINT cmd = (UINT)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                                        pt.x, pt.y, 0, m_hWnd, NULL);
        m_bInMenu = FALSE;
        DestroyMenu(hMenu);

        if (cmd)
        {
            Hide();
            StartMenu2_ShutdownCmd(m_hwndTray, cmd);
        }
    }

    VOID LaunchPath(LPCWSTR pszPath)
    {
        Hide();
        ShellExecuteW(NULL, L"open", pszPath, NULL, NULL, SW_SHOWNORMAL);
    }

    VOID OpenFolder(int csidl)
    {
        WCHAR szPath[MAX_PATH];
        if (SHGetSpecialFolderPathW(NULL, szPath, csidl, FALSE))
            LaunchPath(szPath);
    }

    VOID ActivateCell(int index)
    {
        if (index < 0 || (SIZE_T)index >= m_Cells.GetCount())
            return;

        SM11CELL cell = m_Cells[index];
        SM11APP *pApp = GetApp(cell);

        switch (cell.kind)
        {
            case SM11K_PIN:
            case SM11K_RECOMMENDED:
            case SM11K_ROW:
            case SM11K_TILE:
                if (pApp)
                    LaunchPath(pApp->szPath);
                break;
            case SM11K_BUTTON_ALLAPPS:
                m_nView = SM11V_ALL;
                m_nScroll = 0;
                m_iHot = -1;
                Layout();
                InvalidateRect(NULL, FALSE);
                break;
            case SM11K_BUTTON_BACK:
                m_nView = SM11V_HOME;
                m_nScroll = 0;
                m_iHot = -1;
                Layout();
                InvalidateRect(NULL, FALSE);
                break;
            case SM11K_USER:
                OpenFolder(CSIDL_PROFILE);
                break;
            case SM11K_RAIL_DOCS:
                OpenFolder(CSIDL_PERSONAL);
                break;
            case SM11K_RAIL_SETTINGS:
                LaunchPath(L"control.exe");
                break;
            case SM11K_POWER:
                ShowPowerMenu(&cell.rc);
                break;
        }
    }

    HRESULT Popup(const RECT *prcBtn, UINT uPos)
    {
        if (!IsWindow())
            return E_FAIL;
        if (IsMenuVisible())
            return S_FALSE;

        ULONGLONG now = GetTickCount64();
        if (now - m_LastDismiss < 300)
            return S_FALSE;

        m_bWin10 = (g_TaskbarSettings.dwShellStyle == SHELLSTYLE_WIN10);
        SelectPalette();
        CreateFonts();
        RebuildData();

        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        POINT ptRef = { prcBtn->left, prcBtn->top };
        GetMonitorInfoW(MonitorFromPoint(ptRef, MONITOR_DEFAULTTONEAREST), &mi);

        int availH = mi.rcWork.bottom - mi.rcWork.top - Sc(24);
        int w = Sc(m_bWin10 ? 672 : 640);
        int h = min(Sc(m_bWin10 ? 720 : 700), availH);
        m_size.cx = w;
        m_size.cy = h;

        int gap = m_bWin10 ? 0 : Sc(12);
        int x, y;
        switch (uPos)
        {
            case ABE_TOP:
                x = m_bWin10 ? mi.rcWork.left : (mi.rcWork.left + mi.rcWork.right - w) / 2;
                y = prcBtn->bottom + gap;
                break;
            case ABE_LEFT:
                x = prcBtn->right + gap;
                y = mi.rcWork.bottom - h;
                break;
            case ABE_RIGHT:
                x = prcBtn->left - w - gap;
                y = mi.rcWork.bottom - h;
                break;
            default:
                x = m_bWin10 ? mi.rcWork.left : (mi.rcWork.left + mi.rcWork.right - w) / 2;
                y = prcBtn->top - h - gap;
                break;
        }
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;

        m_nView = SM11V_HOME;
        m_nScroll = 0;
        m_iHot = -1;
        m_iPressed = -1;
        m_Results.SetCount(0);
        ::SetWindowTextW(m_hwndEdit, L"");
        ::SendMessageW(m_hwndEdit, EM_SETCUEBANNER, TRUE,
                       (LPARAM)(m_bWin10 ? L"Type here to search" : L"Search for apps, settings, and documents"));

        Layout();

        if (!m_bWin10)
        {
            HRGN hRgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, Sc(16), Sc(16));
            SetWindowRgn(hRgn, TRUE);
        }
        else
        {
            SetWindowRgn(NULL, TRUE);
        }

        SetWindowPos(HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
        ::SetForegroundWindow(m_hWnd);
        ::SetFocus(m_hwndEdit);
        InvalidateRect(NULL, FALSE);
        return S_OK;
    }

    VOID Hide()
    {
        if (!IsWindow() || !IsWindowVisible())
            return;
        ShowWindow(SW_HIDE);
        Tray_OnStartMenuDismissed(m_Tray);
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(&ps);
        if (hdc && m_size.cx > 0 && m_size.cy > 0)
        {
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbm = CreateCompatibleBitmap(hdc, m_size.cx, m_size.cy);
            if (hdcMem && hbm)
            {
                HGDIOBJ hOld = SelectObject(hdcMem, hbm);
                PaintTo(hdcMem);
                BitBlt(hdc, 0, 0, m_size.cx, m_size.cy, hdcMem, 0, 0, SRCCOPY);
                SelectObject(hdcMem, hOld);
            }
            if (hbm)
                DeleteObject(hbm);
            if (hdcMem)
                DeleteDC(hdcMem);
        }
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int hot = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hot != m_iHot)
        {
            m_iHot = hot;
            InvalidateRect(NULL, FALSE);
        }
        if (!m_bTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
            m_bTracking = TrackMouseEvent(&tme);
        }
        return 0;
    }

    LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_bTracking = FALSE;
        if (m_iHot != -1)
        {
            m_iHot = -1;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_iPressed = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int hit = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        int pressed = m_iPressed;
        m_iPressed = -1;
        if (hit >= 0 && hit == pressed)
            ActivateCell(hit);
        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int viewH = m_rcList.bottom - m_rcList.top;
        int maxScroll = max(0, m_nContentH - viewH);
        if (maxScroll <= 0)
            return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int ns = m_nScroll - MulDiv(delta, Sc(60), WHEEL_DELTA);
        ns = max(0, min(ns, maxScroll));
        if (ns != m_nScroll)
        {
            m_nScroll = ns;
            Layout();
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(&pt);
            m_iHot = HitTest(pt.x, pt.y);
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == WA_INACTIVE && !m_bInMenu && IsMenuVisible())
        {
            m_LastDismiss = GetTickCount64();
            Hide();
        }
        return 0;
    }

    LRESULT OnCommand(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (LOWORD(wParam) == SM11_IDC_SEARCH && HIWORD(wParam) == EN_CHANGE)
            UpdateSearch();
        return 0;
    }

    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, m_Pal.Text);
        SetBkColor(hdc, m_Pal.Field);
        return (LRESULT)m_hbrField;
    }

    LRESULT OnEditKey(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
        {
            CStringW szText;
            GetSearchText(szText);
            if (szText.IsEmpty())
                Hide();
            else
                ::SetWindowTextW(m_hwndEdit, L"");
        }
        else if (wParam == VK_RETURN && m_nView == SM11V_SEARCH && m_Results.GetCount())
        {
            LaunchPath(m_Apps[m_Results[0]]->szPath);
        }
        return 0;
    }

    LRESULT OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (wParam == VK_ESCAPE)
            Hide();
        return 0;
    }

    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return MA_ACTIVATE;
    }

    BEGIN_MSG_MAP(CStartMenu11Wnd)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
        MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
        MESSAGE_HANDLER(WM_COMMAND, OnCommand)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(SM11M_EDITKEY, OnEditKey)
        MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
    END_MSG_MAP()
};

static CStartMenu11Wnd *g_pStartMenu11 = NULL;

HRESULT StartMenu11_Create(IN ITrayWindow *Tray, IN HWND hwndTray)
{
    if (g_pStartMenu11)
        return S_FALSE;

    CStartMenu11Wnd *pMenu = new CStartMenu11Wnd();
    if (!pMenu)
        return E_OUTOFMEMORY;

    if (!pMenu->Init(Tray, hwndTray))
    {
        delete pMenu;
        return E_FAIL;
    }

    g_pStartMenu11 = pMenu;
    return S_OK;
}

HRESULT StartMenu11_Popup(IN const RECT *prcStartBtn, IN UINT uPosition)
{
    if (!g_pStartMenu11)
        return E_FAIL;
    return g_pStartMenu11->Popup(prcStartBtn, uPosition);
}

VOID StartMenu11_Hide(VOID)
{
    if (g_pStartMenu11)
        g_pStartMenu11->Hide();
}

BOOL StartMenu11_IsVisible(VOID)
{
    return g_pStartMenu11 && g_pStartMenu11->IsMenuVisible();
}

VOID StartMenu11_Destroy(VOID)
{
    if (g_pStartMenu11)
    {
        g_pStartMenu11->Term();
        delete g_pStartMenu11;
        g_pStartMenu11 = NULL;
    }
}
