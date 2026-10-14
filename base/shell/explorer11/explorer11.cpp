#include "frame.h"

HINSTANCE g_hInstance = NULL;

static BOOL
E11IsDark(VOID)
{
    DWORD dwValue = 1, cbData = sizeof(dwValue), dwType;
    if (SHGetValueW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"AppsUseLightTheme", &dwType, &dwValue, &cbData) == ERROR_SUCCESS)
    {
        return dwValue == 0;
    }
    return FALSE;
}

VOID E11GetPalette(E11_PALETTE *pPal)
{
    if (E11IsDark())
    {
        pPal->FrameBg = RGB(32, 32, 32);
        pPal->BarBg = RGB(43, 43, 43);
        pPal->Text = RGB(235, 235, 235);
        pPal->DimText = RGB(150, 150, 150);
        pPal->HotFill = RGB(61, 61, 61);
        pPal->Accent = GetSysColor(COLOR_HIGHLIGHT);
        pPal->Border = RGB(24, 24, 24);
        pPal->EditBg = RGB(25, 25, 25);
    }
    else
    {
        pPal->FrameBg = RGB(255, 255, 255);
        pPal->BarBg = RGB(245, 246, 247);
        pPal->Text = RGB(34, 34, 34);
        pPal->DimText = RGB(112, 112, 112);
        pPal->HotFill = RGB(229, 243, 255);
        pPal->Accent = GetSysColor(COLOR_HIGHLIGHT);
        pPal->Border = RGB(216, 216, 216);
        pPal->EditBg = RGB(255, 255, 255);
    }
}

enum E11_TABID
{
    T_FILE = 0,
    T_HOME,
    T_COMPUTER,
    T_SHARE,
    T_VIEW
};

enum E11_CONTENT
{
    CONTENT_SHELL = 0,
    CONTENT_THISPC,
    CONTENT_SEARCH
};

enum E11_HOTMISC
{
    HM_NONE = 0,
    HM_REFRESH,
    HM_HISTORY,
    HM_COLLAPSE,
    HM_QAT1,
    HM_QAT2,
    HM_SEARCH,
    HM_ST_DETAILS,
    HM_ST_ICONS
};

typedef struct _E11_RIBBONGROUP
{
    LPCWSTR pszLabel;
    int xStart;
    int xEnd;
} E11_RIBBONGROUP;

static LRESULT CALLBACK
E11SearchEditProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                  UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_KEYDOWN)
    {
        if (wParam == VK_RETURN)
        {
            ::SendMessageW((HWND)dwRefData, E11M_SEARCHENTER, 0, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE)
        {
            ::SendMessageW((HWND)dwRefData, E11M_SEARCHESC, 0, 0);
            return 0;
        }
    }
    if (uMsg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0;
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

class CExplorerFrame :
    public CWindowImpl<CExplorerFrame, CWindow, CFrameWinTraits>,
    public IShellBrowser,
    public IServiceProvider
{
public:
    DECLARE_WND_CLASS_EX(L"Explorer11Frame", CS_HREDRAW | CS_VREDRAW, COLOR_WINDOW)

    LONG m_cRef;
    E11_PALETTE m_Pal;
    CNavPane m_NavPane;
    CComPtr<IShellView> m_pShellView;
    HWND m_hwndView;
    LPITEMIDLIST m_pidlCurrent;
    LPITEMIDLIST m_pidlThisPC;
    CAtlArray<LPITEMIDLIST> m_BackStack;
    CAtlArray<LPITEMIDLIST> m_FwdStack;
    CAtlArray<E11_CRUMB> m_Crumbs;
    HFONT m_hFont;
    HFONT m_hFontSmall;
    HFONT m_hFontHeader;
    HBRUSH m_hbrEdit;
    HICON m_hIconBig;
    HICON m_hIconSmall;
    WCHAR m_szTitle[MAX_PATH];
    WCHAR m_szItems[64];
    int m_iHotNav;
    int m_iHotCrumb;
    int m_nHotMisc;
    RECT m_rcBack, m_rcFwd, m_rcHistory, m_rcUp;
    RECT m_rcCrumbBar, m_rcRefresh, m_rcSearchBox;
    RECT m_rcPane, m_rcViewArea, m_rcStatus;
    RECT m_rcCollapse, m_rcQat1, m_rcQat2;
    RECT m_rcStDetails, m_rcStIcons;
    int m_iActiveTab;
    int m_iHotTab;
    int m_iHotBtn;
    UINT m_uViewMode;
    BOOL m_bRibbonCollapsed;
    int m_nContent;
    CThisPCView *m_pThisPC;
    HWND m_hwndSearchEdit;
    CAtlArray<E11_RIBBONBTN> m_RibbonBtns;
    CAtlArray<E11_RIBBONGROUP> m_RibbonGroups;
    RECT m_rcTabs, m_rcRibbon, m_rcTab;
    struct { LPCWSTR pszName; int nId; } m_aTabs[4];
    RECT m_aTabRects[4];
    int m_nTabCount;

    CExplorerFrame() : m_cRef(1), m_hwndView(NULL), m_pidlCurrent(NULL), m_pidlThisPC(NULL),
                       m_hFont(NULL), m_hFontSmall(NULL), m_hFontHeader(NULL),
                       m_hbrEdit(NULL), m_hIconBig(NULL), m_hIconSmall(NULL),
                       m_iHotNav(0), m_iHotCrumb(-1), m_nHotMisc(HM_NONE),
                       m_iActiveTab(T_HOME), m_iHotTab(-1), m_iHotBtn(-1),
                       m_uViewMode(FVM_DETAILS), m_bRibbonCollapsed(FALSE),
                       m_nContent(CONTENT_SHELL), m_pThisPC(NULL),
                       m_hwndSearchEdit(NULL), m_nTabCount(0)
    {
        ZeroMemory(&m_Pal, sizeof(m_Pal));
        ZeroMemory(m_aTabRects, sizeof(m_aTabRects));
        ZeroMemory(&m_rcTab, sizeof(m_rcTab));
        ZeroMemory(m_aTabs, sizeof(m_aTabs));
        m_szTitle[0] = 0;
        m_szItems[0] = 0;
        m_NavPane.m_pFrame = this;
        E11GetPalette(&m_Pal);
        SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &m_pidlThisPC);
    }

    int Sc(int v) const { return E11Scale(v); }
    int TabsH() const { return Sc(40); }
    int RibbonH() const { return Sc(48); }
    int NavRowH() const { return Sc(44); }
    int BarH() const { return TabsH() + RibbonH() + NavRowH(); }
    int PaneW() const { return Sc(224); }
    int StatusH() const { return Sc(24); }

    // *** IUnknown ***
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IOleWindow || riid == IID_IShellBrowser)
            *ppv = static_cast<IShellBrowser *>(this);
        else if (riid == IID_IServiceProvider)
            *ppv = static_cast<IServiceProvider *>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_cRef); }

    // *** IOleWindow ***
    STDMETHODIMP GetWindow(HWND *phwnd) override
    {
        *phwnd = m_hWnd;
        return S_OK;
    }

    STDMETHODIMP ContextSensitiveHelp(BOOL fEnterMode) override { return E_NOTIMPL; }

    // *** IShellBrowser ***
    STDMETHODIMP InsertMenusSB(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) override { return E_NOTIMPL; }
    STDMETHODIMP SetMenuSB(HMENU hmenuShared, HOLEMENU holemenuRes, HWND hwndActiveObject) override { return S_OK; }
    STDMETHODIMP RemoveMenusSB(HMENU hmenuShared) override { return S_OK; }
    STDMETHODIMP SetStatusTextSB(LPCOLESTR pszStatusText) override { return S_OK; }
    STDMETHODIMP EnableModelessSB(BOOL fEnable) override { return S_OK; }
    STDMETHODIMP TranslateAcceleratorSB(MSG *pmsg, WORD wID) override { return S_FALSE; }

    STDMETHODIMP BrowseObject(PCUIDLIST_RELATIVE pidl, UINT wFlags) override
    {
        if (wFlags & SBSP_NAVIGATEBACK)
            return NavigateBack();
        if (wFlags & SBSP_NAVIGATEFORWARD)
            return NavigateForward();
        if (wFlags & SBSP_PARENT)
            return NavigateUp();

        if (wFlags & SBSP_RELATIVE)
        {
            LPITEMIDLIST pidlNew = ILCombine(m_pidlCurrent, (PCUIDLIST_ABSOLUTE)pidl);
            if (!pidlNew)
                return E_OUTOFMEMORY;
            HRESULT hr = BrowseTo(pidlNew, TRUE);
            ILFree(pidlNew);
            return hr;
        }

        return BrowseTo((PCIDLIST_ABSOLUTE)pidl, TRUE);
    }

    STDMETHODIMP GetViewStateStream(DWORD grfMode, IStream **ppStrm) override
    {
        *ppStrm = NULL;
        return E_NOTIMPL;
    }

    STDMETHODIMP GetControlWindow(UINT id, HWND *phwnd) override
    {
        *phwnd = NULL;
        return S_FALSE;
    }

    STDMETHODIMP SendControlMsg(UINT id, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pret) override
    {
        if (pret)
            *pret = 0;
        return S_FALSE;
    }

    STDMETHODIMP QueryActiveShellView(IShellView **ppshv) override
    {
        *ppshv = m_pShellView;
        if (m_pShellView)
        {
            m_pShellView.p->AddRef();
            return S_OK;
        }
        return E_FAIL;
    }

    STDMETHODIMP OnViewWindowActive(IShellView *pshv) override { return S_OK; }
    STDMETHODIMP SetToolbarItems(LPTBBUTTONSB lpButtons, UINT nButtons, UINT uFlags) override { return S_OK; }

    // *** IServiceProvider ***
    STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void **ppv) override
    {
        if (guidService == SID_SShellBrowser || guidService == SID_STopLevelBrowser)
            return QueryInterface(riid, ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    // *** Breadcrumbs / title ***
    VOID FreeCrumbs()
    {
        for (SIZE_T i = 0; i < m_Crumbs.GetCount(); i++)
        {
            if (m_Crumbs[i].pidl)
                ILFree(m_Crumbs[i].pidl);
        }
        m_Crumbs.SetCount(0);
    }

    VOID GetPidlName(LPITEMIDLIST pidl, LPWSTR pszName, UINT cchName)
    {
        SHFILEINFOW sfi;
        ZeroMemory(&sfi, sizeof(sfi));
        pszName[0] = 0;
        if (SHGetFileInfoW((LPCWSTR)pidl, 0, &sfi, sizeof(sfi),
                           SHGFI_PIDL | SHGFI_DISPLAYNAME) && sfi.szDisplayName[0])
            StringCchCopyW(pszName, cchName, sfi.szDisplayName);
        if (!_wcsicmp(pszName, L"My Computer"))
            StringCchCopyW(pszName, cchName, L"This PC");
    }

    VOID BuildCrumbs()
    {
        FreeCrumbs();

        if (!m_pidlCurrent)
            return;

        int nCount = 0;
        for (LPCITEMIDLIST p = m_pidlCurrent; p && !ILIsEmpty(p); p = ILGetNext(p))
            nCount++;
        if (!nCount)
            nCount = 1;

        for (int i = 1; i <= nCount; i++)
        {
            E11_CRUMB crumb;
            ZeroMemory(&crumb, sizeof(crumb));
            crumb.pidl = ILClone(m_pidlCurrent);
            if (!crumb.pidl)
                continue;
            for (int k = 0; k < nCount - i; k++)
                ILRemoveLastID(crumb.pidl);
            GetPidlName(crumb.pidl, crumb.szName, _countof(crumb.szName));
            if (!crumb.szName[0])
                StringCchCopyW(crumb.szName, _countof(crumb.szName), L"?");
            m_Crumbs.Add(crumb);
        }

        StringCchCopyW(m_szTitle, _countof(m_szTitle),
                       m_Crumbs.GetCount() ? m_Crumbs[m_Crumbs.GetCount() - 1].szName : L"Explorer");
        SetWindowTextW(m_szTitle);
        LayoutCrumbs();
    }

    VOID LayoutCrumbs()
    {
        HDC hdc = GetDC();
        HGDIOBJ hOld = SelectObject(hdc, m_hFont);
        int xRight = m_rcRefresh.left - Sc(4);
        int xLeft = m_rcCrumbBar.left + Sc(30);
        int cyTop = m_rcCrumbBar.top;
        int cyBottom = m_rcCrumbBar.bottom;

        int nFirst = 0;
        while (TRUE)
        {
            int x = xLeft;
            BOOL bFits = TRUE;
            for (SIZE_T i = nFirst; i < m_Crumbs.GetCount(); i++)
            {
                SIZE sz;
                GetTextExtentPoint32W(hdc, m_Crumbs[i].szName,
                                      (int)wcslen(m_Crumbs[i].szName), &sz);
                x += sz.cx + Sc(16);
                if (i + 1 < m_Crumbs.GetCount())
                    x += Sc(12);
            }
            if (x <= xRight || nFirst >= (int)m_Crumbs.GetCount() - 1)
                break;
            nFirst++;
            bFits = FALSE;
            (void)bFits;
        }

        int x = xLeft;
        for (SIZE_T i = 0; i < m_Crumbs.GetCount(); i++)
        {
            if ((int)i < nFirst)
            {
                SetRectEmpty(&m_Crumbs[i].rc);
                continue;
            }
            SIZE sz;
            GetTextExtentPoint32W(hdc, m_Crumbs[i].szName,
                                  (int)wcslen(m_Crumbs[i].szName), &sz);
            SetRect(&m_Crumbs[i].rc, x, cyTop + Sc(2), x + sz.cx + Sc(16), cyBottom - Sc(2));
            x += sz.cx + Sc(16);
            if (i + 1 < m_Crumbs.GetCount())
                x += Sc(12);
        }

        SelectObject(hdc, hOld);
        ReleaseDC(hdc);
    }

    VOID UpdateItemCount()
    {
        m_szItems[0] = 0;

        if (m_nContent != CONTENT_SHELL && m_pThisPC)
        {
            int nItems = m_pThisPC->ItemCount();
            StringCchPrintfW(m_szItems, _countof(m_szItems),
                             nItems == 1 ? L"%d item" : L"%d items", nItems);
        }
        else if (m_pShellView)
        {
            CComPtr<IFolderView> pFolderView;
            int nItems = 0;
            if (SUCCEEDED(m_pShellView->QueryInterface(IID_PPV_ARG(IFolderView, &pFolderView))) &&
                SUCCEEDED(pFolderView->ItemCount(SVGIO_ALLVIEW, &nItems)))
            {
                StringCchPrintfW(m_szItems, _countof(m_szItems),
                                 nItems == 1 ? L"%d item" : L"%d items", nItems);
            }
        }
        InvalidateRect(&m_rcStatus, FALSE);
    }

    VOID SyncNavSelection()
    {
        m_NavPane.m_iSelected = -1;
        if (!m_pidlCurrent)
            return;
        for (SIZE_T i = 0; i < m_NavPane.m_Items.GetCount(); i++)
        {
            if (m_NavPane.m_Items[i].nKind == NAV_ITEM &&
                m_NavPane.m_Items[i].pidl &&
                ILIsEqual(m_NavPane.m_Items[i].pidl, m_pidlCurrent))
            {
                m_NavPane.m_iSelected = (int)i;
                break;
            }
        }
        InvalidateRect(&m_rcPane, FALSE);
    }

    VOID EnsureThisPCChild()
    {
        if (m_pThisPC && m_pThisPC->IsWindow())
            return;

        if (!m_pThisPC)
            m_pThisPC = new CThisPCView();
        if (!m_pThisPC)
            return;

        m_pThisPC->m_pFrame = this;
        m_pThisPC->m_Pal = m_Pal;
        m_pThisPC->SetFonts(m_hFont, m_hFontHeader);
        m_pThisPC->Create(m_hWnd, CWindow::rcDefault, NULL,
                          WS_CHILD | WS_CLIPSIBLINGS);
    }

    VOID DestroyShellView()
    {
        if (m_pShellView)
        {
            m_pShellView->UIActivate(SVUIA_DEACTIVATE);
            m_pShellView->DestroyViewWindow();
            m_pShellView.Release();
            m_hwndView = NULL;
        }
    }

    VOID BuildTabs()
    {
        BOOL bPC = (m_nContent != CONTENT_SHELL);

        m_nTabCount = 0;
        m_aTabs[m_nTabCount].pszName = L"File";
        m_aTabs[m_nTabCount++].nId = T_FILE;
        if (bPC)
        {
            m_aTabs[m_nTabCount].pszName = L"Computer";
            m_aTabs[m_nTabCount++].nId = T_COMPUTER;
        }
        else
        {
            m_aTabs[m_nTabCount].pszName = L"Home";
            m_aTabs[m_nTabCount++].nId = T_HOME;
            m_aTabs[m_nTabCount].pszName = L"Share";
            m_aTabs[m_nTabCount++].nId = T_SHARE;
        }
        m_aTabs[m_nTabCount].pszName = L"View";
        m_aTabs[m_nTabCount++].nId = T_VIEW;

        BOOL bFound = FALSE;
        for (int t = 0; t < m_nTabCount; t++)
            if (m_aTabs[t].nId == m_iActiveTab)
                bFound = TRUE;
        if (!bFound)
            m_iActiveTab = bPC ? T_COMPUTER : T_HOME;
    }

    HRESULT BrowseTo(PCIDLIST_ABSOLUTE pidl, BOOL bPushHistory)
    {
        HRESULT hr;
        BOOL bThisPC;

        if (!pidl)
            return E_INVALIDARG;

        bThisPC = m_pidlThisPC && ILIsEqual(pidl, m_pidlThisPC);

        if (bThisPC)
        {
            EnsureThisPCChild();
            DestroyShellView();

            m_pThisPC->StopSearch();
            m_pThisPC->m_Pal = m_Pal;
            m_pThisPC->BuildThisPC();
            m_nContent = CONTENT_THISPC;

            ::SetWindowPos(m_pThisPC->m_hWnd, NULL,
                           m_rcViewArea.left, m_rcViewArea.top,
                           m_rcViewArea.right - m_rcViewArea.left,
                           m_rcViewArea.bottom - m_rcViewArea.top,
                           SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        else
        {
            CComPtr<IShellFolder> pDesktop;
            CComPtr<IShellFolder> pFolder;
            CComPtr<IShellView> pNewView;
            FOLDERSETTINGS fs;
            RECT rcView;
            HWND hwndNewView = NULL;

            hr = SHGetDesktopFolder(&pDesktop);
            if (FAILED(hr))
                return hr;

            if (ILIsEmpty(pidl))
            {
                pFolder = pDesktop;
            }
            else
            {
                hr = pDesktop->BindToObject(pidl, NULL, IID_PPV_ARG(IShellFolder, &pFolder));
                if (FAILED(hr))
                    return hr;
            }

            hr = pFolder->CreateViewObject(m_hWnd, IID_PPV_ARG(IShellView, &pNewView));
            if (FAILED(hr))
                return hr;

            fs.ViewMode = m_uViewMode;
            fs.fFlags = FWF_SHOWSELALWAYS | FWF_NOWEBVIEW;

            rcView = m_rcViewArea;

            hr = pNewView->CreateViewWindow(m_pShellView, &fs,
                                            static_cast<IShellBrowser *>(this),
                                            &rcView, &hwndNewView);
            if (FAILED(hr) || !hwndNewView)
                return FAILED(hr) ? hr : E_FAIL;

            DestroyShellView();
            if (m_pThisPC && m_pThisPC->IsWindow())
            {
                m_pThisPC->StopSearch();
                m_pThisPC->ShowWindow(SW_HIDE);
            }
            m_nContent = CONTENT_SHELL;

            m_pShellView = pNewView;
            m_hwndView = hwndNewView;

            m_pShellView->UIActivate(SVUIA_ACTIVATE_NOFOCUS);
            ::SetWindowPos(m_hwndView, NULL,
                           m_rcViewArea.left, m_rcViewArea.top,
                           m_rcViewArea.right - m_rcViewArea.left,
                           m_rcViewArea.bottom - m_rcViewArea.top,
                           SWP_NOZORDER | SWP_SHOWWINDOW);
        }

        if (bPushHistory && m_pidlCurrent)
        {
            m_BackStack.Add(m_pidlCurrent);
            for (SIZE_T i = 0; i < m_FwdStack.GetCount(); i++)
                ILFree(m_FwdStack[i]);
            m_FwdStack.SetCount(0);
            m_pidlCurrent = NULL;
        }
        else if (m_pidlCurrent)
        {
            ILFree(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }

        m_pidlCurrent = ILClone(pidl);

        BuildTabs();
        Layout();
        BuildCrumbs();
        UpdateItemCount();
        SyncNavSelection();
        InvalidateRect(NULL, TRUE);
        return S_OK;
    }

    HRESULT NavigateBack()
    {
        if (m_BackStack.GetCount() == 0)
            return S_FALSE;

        LPITEMIDLIST pidlTarget = m_BackStack[m_BackStack.GetCount() - 1];
        m_BackStack.SetCount(m_BackStack.GetCount() - 1);

        if (m_pidlCurrent)
        {
            m_FwdStack.Add(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }

        HRESULT hr = BrowseTo(pidlTarget, FALSE);
        ILFree(pidlTarget);
        return hr;
    }

    HRESULT NavigateForward()
    {
        if (m_FwdStack.GetCount() == 0)
            return S_FALSE;

        LPITEMIDLIST pidlTarget = m_FwdStack[m_FwdStack.GetCount() - 1];
        m_FwdStack.SetCount(m_FwdStack.GetCount() - 1);

        if (m_pidlCurrent)
        {
            m_BackStack.Add(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }

        HRESULT hr = BrowseTo(pidlTarget, FALSE);
        ILFree(pidlTarget);
        return hr;
    }

    HRESULT NavigateUp()
    {
        LPITEMIDLIST pidlParent;

        if (!m_pidlCurrent || ILIsEmpty(m_pidlCurrent))
            return S_FALSE;

        pidlParent = ILClone(m_pidlCurrent);
        if (!pidlParent)
            return E_OUTOFMEMORY;

        ILRemoveLastID(pidlParent);
        HRESULT hr = BrowseTo(pidlParent, TRUE);
        ILFree(pidlParent);
        return hr;
    }

    // *** Ribbon ***
    VOID BuildRibbon()
    {
        struct CMDITEM { LPCWSTR pszLabel; int nCmd; int nIcon; UINT nFluent; BOOL bDisabled; int w; };
        BOOL bPC = (m_nContent != CONTENT_SHELL);
        const CMDITEM c_Items[] =
        {
            { L"New",  E11CMD_NEWMENU,  EI_NONE,   IDI_FLU_ADD,     bPC,   84 },
            { NULL,    E11CMD_NONE,     EI_NONE,   0,               TRUE,  14 },
            { NULL,    E11CMD_CUT,      EI_CUT,    0,               bPC,   40 },
            { NULL,    E11CMD_COPY,     EI_COPY,   0,               bPC,   40 },
            { NULL,    E11CMD_PASTE,    EI_PASTE,  0,               bPC,   40 },
            { NULL,    E11CMD_RENAME,   EI_RENAME, 0,               bPC,   40 },
            { NULL,    E11CMD_DELETE,   EI_DELETE, 0,               bPC,   40 },
            { NULL,    E11CMD_NONE,     EI_NONE,   0,               TRUE,  14 },
            { L"Sort", E11CMD_SORTMENU, EI_NONE,   IDI_FLU_ARROWUP, bPC,   84 },
            { L"View", E11CMD_VIEWMENU, EI_NONE,   IDI_FLU_GRID,    FALSE, 84 },
            { L"...",  E11CMD_MOREMENU, EI_NONE,   0,               FALSE, 40 },
        };

        m_RibbonBtns.SetCount(0);
        m_RibbonGroups.SetCount(0);

        int x = Sc(12);
        int yTop = m_rcRibbon.top + Sc(6);
        int yBottom = m_rcRibbon.bottom - Sc(6);
        for (UINT i = 0; i < _countof(c_Items); i++)
        {
            E11_RIBBONBTN btn;
            ZeroMemory(&btn, sizeof(btn));
            btn.pszLabel = c_Items[i].pszLabel;
            btn.pszGroup = L"";
            btn.nCmd = c_Items[i].nCmd;
            btn.nIcon = c_Items[i].nIcon;
            btn.nFluent = c_Items[i].nFluent;
            btn.bBig = FALSE;
            btn.bDisabled = c_Items[i].bDisabled;
            int w = Sc(c_Items[i].w);
            SetRect(&btn.rc, x, yTop, x + w, yBottom);
            x += w + Sc(2);
            m_RibbonBtns.Add(btn);
        }
    }

    // *** Layout ***
    VOID Layout()
    {
        RECT rcClient;
        GetClientRect(&rcClient);

        SetRect(&m_rcTabs, 0, 0, rcClient.right, TabsH());
        SetRect(&m_rcRibbon, 0, TabsH() + NavRowH(), rcClient.right, TabsH() + NavRowH() + RibbonH());

        SetRect(&m_rcTab, Sc(8), Sc(6), Sc(8) + Sc(230), TabsH());
        SetRect(&m_rcCollapse, m_rcTab.right - Sc(32), m_rcTab.top + Sc(8),
                m_rcTab.right - Sc(8), m_rcTab.top + Sc(30));
        SetRect(&m_rcQat1, m_rcTab.right + Sc(6), m_rcTab.top + Sc(8),
                m_rcTab.right + Sc(30), m_rcTab.top + Sc(30));
        SetRectEmpty(&m_rcQat2);
        for (int t = 0; t < 4; t++)
            SetRectEmpty(&m_aTabRects[t]);

        int nNavTop = TabsH();
        int nNavMid = nNavTop + NavRowH() / 2;
        SetRect(&m_rcBack, Sc(8), nNavMid - Sc(14), Sc(8) + Sc(30), nNavMid + Sc(14));
        SetRect(&m_rcFwd, m_rcBack.right + Sc(2), m_rcBack.top,
                m_rcBack.right + Sc(2) + Sc(30), m_rcBack.bottom);
        SetRect(&m_rcHistory, m_rcFwd.right + Sc(2), m_rcBack.top,
                m_rcFwd.right + Sc(2) + Sc(18), m_rcBack.bottom);
        SetRect(&m_rcUp, m_rcHistory.right + Sc(2), m_rcBack.top,
                m_rcHistory.right + Sc(2) + Sc(30), m_rcBack.bottom);

        int nSearchW = Sc(190);
        SetRect(&m_rcSearchBox, rcClient.right - Sc(12) - nSearchW, nNavMid - Sc(13),
                rcClient.right - Sc(12), nNavMid + Sc(13));

        SetRect(&m_rcCrumbBar, m_rcUp.right + Sc(8), nNavMid - Sc(13),
                m_rcSearchBox.left - Sc(10), nNavMid + Sc(13));
        SetRect(&m_rcRefresh, m_rcCrumbBar.right - Sc(26), m_rcCrumbBar.top + Sc(2),
                m_rcCrumbBar.right - Sc(4), m_rcCrumbBar.bottom - Sc(2));

        SetRect(&m_rcPane, 0, BarH(), PaneW(), rcClient.bottom - StatusH());
        SetRect(&m_rcViewArea, PaneW() + 1, BarH(),
                rcClient.right, rcClient.bottom - StatusH());
        SetRect(&m_rcStatus, 0, rcClient.bottom - StatusH(), rcClient.right, rcClient.bottom);

        SetRect(&m_rcStIcons, rcClient.right - Sc(30), m_rcStatus.top + Sc(2),
                rcClient.right - Sc(8), m_rcStatus.bottom - Sc(2));
        SetRect(&m_rcStDetails, m_rcStIcons.left - Sc(24), m_rcStIcons.top,
                m_rcStIcons.left - Sc(2), m_rcStIcons.bottom);

        BuildRibbon();
        m_NavPane.Layout(&m_rcPane);
        LayoutCrumbs();

        if (m_hwndSearchEdit)
            ::SetWindowPos(m_hwndSearchEdit, NULL,
                           m_rcSearchBox.left + Sc(26), m_rcSearchBox.top + Sc(5),
                           m_rcSearchBox.right - m_rcSearchBox.left - Sc(32),
                           m_rcSearchBox.bottom - m_rcSearchBox.top - Sc(9),
                           SWP_NOZORDER);

        if (m_hwndView)
        {
            ::SetWindowPos(m_hwndView, NULL,
                           m_rcViewArea.left, m_rcViewArea.top,
                           m_rcViewArea.right - m_rcViewArea.left,
                           m_rcViewArea.bottom - m_rcViewArea.top,
                           SWP_NOZORDER);
        }
        if (m_pThisPC && m_pThisPC->IsWindow() && m_nContent != CONTENT_SHELL)
        {
            ::SetWindowPos(m_pThisPC->m_hWnd, NULL,
                           m_rcViewArea.left, m_rcViewArea.top,
                           m_rcViewArea.right - m_rcViewArea.left,
                           m_rcViewArea.bottom - m_rcViewArea.top,
                           SWP_NOZORDER);
        }
    }

    // *** Painting ***
    VOID DrawNavButton(HDC hdc, const RECT *prc, int nGlyph, BOOL bEnabled, BOOL bHot)
    {
        static const UINT c_aGlyphRes[4] =
        {
            IDI_FLU_ARROWLEFT, IDI_FLU_ARROWRIGHT, IDI_FLU_ARROWUP, IDI_FLU_CHEVDOWN
        };

        if (bHot && bEnabled)
        {
            HBRUSH hbrHot = CreateSolidBrush(m_Pal.HotFill);
            FillRect(hdc, prc, hbrHot);
            DeleteObject(hbrHot);
        }

        int cx = (prc->left + prc->right) / 2;
        int cy = (prc->top + prc->bottom) / 2;
        int n = (nGlyph == 3) ? Sc(6) : Sc(8);
        RECT rcIcon = { cx - n, cy - n, cx + n, cy + n };
        E11DrawFluentRes(hdc, &rcIcon, c_aGlyphRes[nGlyph & 3],
                         bEnabled ? m_Pal.Text : m_Pal.DimText);
    }

    VOID DrawChevronGlyph(HDC hdc, int cx, int cy, BOOL bUp, COLORREF cr)
    {
        int a = Sc(6);
        RECT rcIcon = { cx - a, cy - a, cx + a, cy + a };
        E11DrawFluentRes(hdc, &rcIcon, bUp ? IDI_FLU_CHEVUP : IDI_FLU_CHEVDOWN, cr);
    }

    LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        PAINTSTRUCT ps;
        HDC hdcWin = BeginPaint(&ps);
        RECT rcClient;
        GetClientRect(&rcClient);

        HDC hdc = CreateCompatibleDC(hdcWin);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdcWin, rcClient.right, rcClient.bottom);
        HGDIOBJ hbmMemOld = SelectObject(hdc, hbmMem);

        RECT rcBar = { 0, 0, rcClient.right, BarH() };
        HBRUSH hbrBar = CreateSolidBrush(m_Pal.BarBg);
        FillRect(hdc, &rcBar, hbrBar);
        DeleteObject(hbrBar);

        SetBkMode(hdc, TRANSPARENT);

        {
            RECT rcRest = { 0, TabsH(), rcClient.right, BarH() };
            HBRUSH hbrRest = CreateSolidBrush(m_Pal.FrameBg);
            FillRect(hdc, &rcRest, hbrRest);
            DeleteObject(hbrRest);

            HBRUSH hbrTab = CreateSolidBrush(m_Pal.FrameBg);
            HGDIOBJ hOldBr = SelectObject(hdc, hbrTab);
            HGDIOBJ hOldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
            RoundRect(hdc, m_rcTab.left, m_rcTab.top, m_rcTab.right + 1, m_rcTab.bottom + Sc(10), Sc(10), Sc(10));
            SelectObject(hdc, hOldPen);
            SelectObject(hdc, hOldBr);
            DeleteObject(hbrTab);

            RECT rcTabIcon = { m_rcTab.left + Sc(12), m_rcTab.top + Sc(9),
                               m_rcTab.left + Sc(28), m_rcTab.top + Sc(25) };
            E11DrawIcon(hdc, &rcTabIcon, (m_nContent != CONTENT_SHELL) ? EI_PC : EI_FOLDER, &m_Pal);

            SelectObject(hdc, m_hFont);
            SetTextColor(hdc, m_Pal.Text);
            RECT rcTabText = { m_rcTab.left + Sc(36), m_rcTab.top, m_rcCollapse.left - Sc(4), m_rcTab.bottom - Sc(10) };
            DrawTextW(hdc, m_szTitle[0] ? m_szTitle : L"This PC", -1, &rcTabText,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

            if (m_nHotMisc == HM_COLLAPSE)
            {
                HBRUSH hbrHot = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdc, &m_rcCollapse, hbrHot);
                DeleteObject(hbrHot);
            }
            RECT rcClose = { m_rcCollapse.left + Sc(5), m_rcCollapse.top + Sc(3),
                             m_rcCollapse.right - Sc(5), m_rcCollapse.bottom - Sc(3) };
            E11DrawFluentRes(hdc, &rcClose, IDI_FLU_TRAYDISMISS, m_Pal.Text);

            if (m_nHotMisc == HM_QAT1)
            {
                HBRUSH hbrHot = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdc, &m_rcQat1, hbrHot);
                DeleteObject(hbrHot);
            }
            RECT rcPlus = { m_rcQat1.left + Sc(4), m_rcQat1.top + Sc(3),
                            m_rcQat1.right - Sc(4), m_rcQat1.bottom - Sc(3) };
            E11DrawFluentRes(hdc, &rcPlus, IDI_FLU_ADD, m_Pal.Text);

            for (SIZE_T b = 0; b < m_RibbonBtns.GetCount(); b++)
            {
                E11_RIBBONBTN &btn = m_RibbonBtns[b];
                BOOL bSeparator = (btn.nCmd == E11CMD_NONE);
                if (bSeparator)
                {
                    int xs = (btn.rc.left + btn.rc.right) / 2;
                    RECT rcLine = { xs, btn.rc.top + Sc(6), xs + 1, btn.rc.bottom - Sc(6) };
                    HBRUSH hbrLine2 = CreateSolidBrush(m_Pal.Border);
                    FillRect(hdc, &rcLine, hbrLine2);
                    DeleteObject(hbrLine2);
                    continue;
                }

                if ((int)b == m_iHotBtn && !btn.bDisabled)
                {
                    HBRUSH hbrHotBtn = CreateSolidBrush(m_Pal.HotFill);
                    FillRect(hdc, &btn.rc, hbrHotBtn);
                    DeleteObject(hbrHotBtn);
                }

                COLORREF crText = btn.bDisabled ? m_Pal.DimText : m_Pal.Text;
                SetTextColor(hdc, crText);
                SelectObject(hdc, m_hFont);

                int cyMid = (btn.rc.top + btn.rc.bottom) / 2;
                if (btn.pszLabel && btn.nIcon == EI_NONE && btn.nFluent == 0)
                {
                    DrawTextW(hdc, btn.pszLabel, -1, &btn.rc,
                              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
                    continue;
                }

                BOOL bIconOnly = (btn.pszLabel == NULL);
                int xIcon = bIconOnly ? (btn.rc.left + btn.rc.right) / 2 - Sc(9) : btn.rc.left + Sc(10);
                RECT rcIcon = { xIcon, cyMid - Sc(9), xIcon + Sc(18), cyMid + Sc(9) };
                if (btn.nFluent)
                    E11DrawFluentRes(hdc, &rcIcon, btn.nFluent, crText);
                else
                    E11DrawIconDim(hdc, &rcIcon, btn.nIcon, &m_Pal, btn.bDisabled);

                if (btn.pszLabel)
                {
                    RECT rcLabel = btn.rc;
                    rcLabel.left += Sc(36);
                    DrawTextW(hdc, btn.pszLabel, -1, &rcLabel,
                              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                }
            }
        }

        HBRUSH hbrPane = CreateSolidBrush(m_Pal.BarBg);
        FillRect(hdc, &m_rcPane, hbrPane);
        DeleteObject(hbrPane);

        HBRUSH hbrStatus = CreateSolidBrush(m_Pal.BarBg);
        FillRect(hdc, &m_rcStatus, hbrStatus);
        DeleteObject(hbrStatus);

        HBRUSH hbrLine = CreateSolidBrush(m_Pal.Border);
        RECT rcSep = { 0, BarH() - 1, rcClient.right, BarH() };
        FillRect(hdc, &rcSep, hbrLine);
        RECT rcSep2 = { m_rcPane.right, m_rcPane.top, m_rcPane.right + 1, m_rcPane.bottom };
        FillRect(hdc, &rcSep2, hbrLine);
        RECT rcSep3 = { 0, m_rcStatus.top, rcClient.right, m_rcStatus.top + 1 };
        FillRect(hdc, &rcSep3, hbrLine);
        DeleteObject(hbrLine);

        DrawNavButton(hdc, &m_rcBack, 0, m_BackStack.GetCount() > 0, m_iHotNav == 1);
        DrawNavButton(hdc, &m_rcFwd, 1, m_FwdStack.GetCount() > 0, m_iHotNav == 2);
        DrawNavButton(hdc, &m_rcHistory, 3,
                      m_BackStack.GetCount() > 0 || m_FwdStack.GetCount() > 0,
                      m_iHotNav == 4);
        DrawNavButton(hdc, &m_rcUp, 2, m_pidlCurrent && !ILIsEmpty(m_pidlCurrent), m_iHotNav == 3);

        HBRUSH hbrEdit = CreateSolidBrush(m_Pal.EditBg);
        FillRect(hdc, &m_rcCrumbBar, hbrEdit);
        DeleteObject(hbrEdit);
        HBRUSH hbrEditEdge = CreateSolidBrush(m_Pal.Border);
        FrameRect(hdc, &m_rcCrumbBar, hbrEditEdge);
        DeleteObject(hbrEditEdge);

        {
            int cyMid = (m_rcCrumbBar.top + m_rcCrumbBar.bottom) / 2;
            RECT rcAddrIcon = { m_rcCrumbBar.left + Sc(7), cyMid - Sc(8),
                                m_rcCrumbBar.left + Sc(23), cyMid + Sc(8) };
            E11DrawIcon(hdc, &rcAddrIcon,
                        (m_nContent != CONTENT_SHELL) ? EI_PC : EI_FOLDER, &m_Pal);

            SelectObject(hdc, m_hFont);
            for (SIZE_T i = 0; i < m_Crumbs.GetCount(); i++)
            {
                E11_CRUMB &crumb = m_Crumbs[i];
                if (IsRectEmpty(&crumb.rc))
                    continue;

                if ((int)i == m_iHotCrumb)
                {
                    HBRUSH hbrHotCrumb = CreateSolidBrush(m_Pal.HotFill);
                    FillRect(hdc, &crumb.rc, hbrHotCrumb);
                    DeleteObject(hbrHotCrumb);
                }

                SetTextColor(hdc, m_Pal.Text);
                DrawTextW(hdc, crumb.szName, -1, (RECT *)&crumb.rc,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

                if (i + 1 < m_Crumbs.GetCount() && !IsRectEmpty(&m_Crumbs[i + 1].rc))
                {
                    int sx = crumb.rc.right + Sc(1);
                    int a = Sc(5);
                    RECT rcSepIcon = { sx, cyMid - a, sx + a * 2, cyMid + a };
                    E11DrawFluentRes(hdc, &rcSepIcon, IDI_FLU_CHEVRIGHT, m_Pal.Text);
                }
            }

            if (m_nHotMisc == HM_REFRESH)
            {
                HBRUSH hbrHotR = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdc, &m_rcRefresh, hbrHotR);
                DeleteObject(hbrHotR);
            }
            RECT rcRefIcon = { m_rcRefresh.left + Sc(3), m_rcRefresh.top + Sc(2),
                               m_rcRefresh.right - Sc(3), m_rcRefresh.bottom - Sc(2) };
            E11DrawIconDim(hdc, &rcRefIcon, EI_REFRESH, &m_Pal, TRUE);
        }

        {
            HBRUSH hbrSearch = CreateSolidBrush(m_Pal.EditBg);
            FillRect(hdc, &m_rcSearchBox, hbrSearch);
            DeleteObject(hbrSearch);
            HBRUSH hbrSearchEdge = CreateSolidBrush(
                m_nHotMisc == HM_SEARCH ? m_Pal.Accent : m_Pal.Border);
            FrameRect(hdc, &m_rcSearchBox, hbrSearchEdge);
            DeleteObject(hbrSearchEdge);

            int cyMid = (m_rcSearchBox.top + m_rcSearchBox.bottom) / 2;
            RECT rcMag = { m_rcSearchBox.left + Sc(5), cyMid - Sc(8),
                           m_rcSearchBox.left + Sc(21), cyMid + Sc(8) };
            E11DrawIconDim(hdc, &rcMag, EI_SEARCH, &m_Pal, TRUE);

            if (!m_hwndSearchEdit)
            {
                WCHAR szCue[96];
                StringCchPrintfW(szCue, _countof(szCue), L"Search %s",
                                 m_szTitle[0] ? m_szTitle : L"This PC");
                RECT rcCue = m_rcSearchBox;
                rcCue.left += Sc(26);
                SelectObject(hdc, m_hFont);
                SetTextColor(hdc, m_Pal.DimText);
                DrawTextW(hdc, szCue, -1, &rcCue,
                          DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }

        m_NavPane.Paint(hdc, &m_rcPane, &m_Pal, m_hFont, m_hFontHeader);

        SelectObject(hdc, m_hFont);
        SetTextColor(hdc, m_Pal.DimText);
        RECT rcStatusText = m_rcStatus;
        rcStatusText.left += Sc(12);
        DrawTextW(hdc, m_szItems, -1, &rcStatusText,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        {
            BOOL bDetails = (m_uViewMode == FVM_DETAILS);
            if (m_nHotMisc == HM_ST_DETAILS || bDetails)
            {
                HBRUSH hbrT = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdc, &m_rcStDetails, hbrT);
                DeleteObject(hbrT);
            }
            if (m_nHotMisc == HM_ST_ICONS || !bDetails)
            {
                HBRUSH hbrT = CreateSolidBrush(m_Pal.HotFill);
                FillRect(hdc, &m_rcStIcons, hbrT);
                DeleteObject(hbrT);
            }
            RECT rcT1 = { m_rcStDetails.left + Sc(3), m_rcStDetails.top + Sc(2),
                          m_rcStDetails.right - Sc(3), m_rcStDetails.bottom - Sc(2) };
            RECT rcT2 = { m_rcStIcons.left + Sc(3), m_rcStIcons.top + Sc(2),
                          m_rcStIcons.right - Sc(3), m_rcStIcons.bottom - Sc(2) };
            E11DrawIconDim(hdc, &rcT1, EI_VIEW_DETAILS, &m_Pal, !bDetails);
            E11DrawIconDim(hdc, &rcT2, EI_VIEW_MEDIUM, &m_Pal, bDetails);
        }

        BitBlt(hdcWin, 0, 0, rcClient.right, rcClient.bottom, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, hbmMemOld);
        DeleteObject(hbmMem);
        DeleteDC(hdc);
        EndPaint(&ps);
        return 0;
    }

    LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        return 1;
    }

    LRESULT OnCtlColorEdit(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        HDC hdcEdit = (HDC)wParam;
        SetTextColor(hdcEdit, m_Pal.Text);
        SetBkColor(hdcEdit, m_Pal.EditBg);
        if (!m_hbrEdit)
            m_hbrEdit = CreateSolidBrush(m_Pal.EditBg);
        return (LRESULT)m_hbrEdit;
    }

    LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        m_hFont = CreateFontW(-Sc(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_hFontSmall = CreateFontW(-Sc(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        m_hFontHeader = CreateFontW(-Sc(12), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        m_hIconBig = E11CreateAppIcon(32);
        m_hIconSmall = E11CreateAppIcon(16);
        if (m_hIconBig)
            SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)m_hIconBig);
        if (m_hIconSmall)
            SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hIconSmall);

        m_NavPane.Build();
        BuildTabs();
        Layout();
        return 0;
    }

    LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        DestroyShellView();
        if (m_pThisPC)
        {
            if (m_pThisPC->IsWindow())
                m_pThisPC->DestroyWindow();
            delete m_pThisPC;
            m_pThisPC = NULL;
        }
        if (m_pidlCurrent)
        {
            ILFree(m_pidlCurrent);
            m_pidlCurrent = NULL;
        }
        if (m_pidlThisPC)
        {
            ILFree(m_pidlThisPC);
            m_pidlThisPC = NULL;
        }
        FreeCrumbs();
        for (SIZE_T i = 0; i < m_BackStack.GetCount(); i++)
            ILFree(m_BackStack[i]);
        m_BackStack.SetCount(0);
        for (SIZE_T i = 0; i < m_FwdStack.GetCount(); i++)
            ILFree(m_FwdStack[i]);
        m_FwdStack.SetCount(0);

        if (m_hFont) { DeleteObject(m_hFont); m_hFont = NULL; }
        if (m_hFontSmall) { DeleteObject(m_hFontSmall); m_hFontSmall = NULL; }
        if (m_hFontHeader) { DeleteObject(m_hFontHeader); m_hFontHeader = NULL; }
        if (m_hbrEdit) { DeleteObject(m_hbrEdit); m_hbrEdit = NULL; }
        if (m_hIconBig) { DestroyIcon(m_hIconBig); m_hIconBig = NULL; }
        if (m_hIconSmall) { DestroyIcon(m_hIconSmall); m_hIconSmall = NULL; }

        PostQuitMessage(0);
        return 0;
    }

    LRESULT OnSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        Layout();
        InvalidateRect(NULL, TRUE);
        return 0;
    }

    // *** Commands ***
    HRESULT GetSelectionObject(REFIID riid, void **ppv)
    {
        *ppv = NULL;
        if (!m_pShellView)
            return E_FAIL;
        return m_pShellView->GetItemObject(SVGIO_SELECTION, riid, ppv);
    }

    VOID DoClipboard(BOOL bCut)
    {
        CComPtr<IDataObject> pDataObject;
        if (FAILED(GetSelectionObject(IID_PPV_ARG(IDataObject, &pDataObject))))
            return;

        if (bCut)
        {
            FORMATETC fmt;
            STGMEDIUM medium;
            fmt.cfFormat = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            fmt.ptd = NULL;
            fmt.dwAspect = DVASPECT_CONTENT;
            fmt.lindex = -1;
            fmt.tymed = TYMED_HGLOBAL;
            medium.tymed = TYMED_HGLOBAL;
            medium.pUnkForRelease = NULL;
            medium.hGlobal = GlobalAlloc(GHND, sizeof(DWORD));
            if (medium.hGlobal)
            {
                DWORD *pdwEffect = (DWORD *)GlobalLock(medium.hGlobal);
                if (pdwEffect)
                {
                    *pdwEffect = DROPEFFECT_MOVE;
                    GlobalUnlock(medium.hGlobal);
                }
                if (FAILED(pDataObject->SetData(&fmt, &medium, TRUE)))
                    GlobalFree(medium.hGlobal);
            }
        }

        OleSetClipboard(pDataObject);
    }

    VOID DoPaste()
    {
        CComPtr<IShellFolder> pDesktop;
        CComPtr<IShellFolder> pFolder;
        CComPtr<IDropTarget> pDropTarget;
        CComPtr<IDataObject> pDataObject;
        POINTL ptl = { 0, 0 };
        DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;

        if (!m_pidlCurrent || FAILED(OleGetClipboard(&pDataObject)) || !pDataObject)
            return;

        if (FAILED(SHGetDesktopFolder(&pDesktop)))
            return;

        if (ILIsEmpty(m_pidlCurrent))
            pFolder = pDesktop;
        else if (FAILED(pDesktop->BindToObject(m_pidlCurrent, NULL, IID_PPV_ARG(IShellFolder, &pFolder))))
            return;

        if (FAILED(pFolder->CreateViewObject(m_hWnd, IID_PPV_ARG(IDropTarget, &pDropTarget))))
            return;

        if (SUCCEEDED(pDropTarget->DragEnter(pDataObject, MK_LBUTTON, ptl, &dwEffect)))
        {
            dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
            pDropTarget->Drop(pDataObject, MK_LBUTTON, ptl, &dwEffect);
        }
    }

    UINT GetSelectedPaths(WCHAR **ppszPaths)
    {
        CComPtr<IDataObject> pDataObject;
        FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM medium;
        UINT cFiles, cchTotal = 0, i;
        WCHAR *pszPaths;

        *ppszPaths = NULL;

        if (FAILED(GetSelectionObject(IID_PPV_ARG(IDataObject, &pDataObject))))
            return 0;
        if (FAILED(pDataObject->GetData(&fmt, &medium)))
            return 0;

        HDROP hDrop = (HDROP)GlobalLock(medium.hGlobal);
        if (!hDrop)
        {
            ReleaseStgMedium(&medium);
            return 0;
        }

        cFiles = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        for (i = 0; i < cFiles; i++)
            cchTotal += DragQueryFileW(hDrop, i, NULL, 0) + 1;
        cchTotal += 1;

        pszPaths = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cchTotal * sizeof(WCHAR));
        if (pszPaths)
        {
            UINT cchUsed = 0;
            for (i = 0; i < cFiles; i++)
            {
                cchUsed += DragQueryFileW(hDrop, i, pszPaths + cchUsed, cchTotal - cchUsed) + 1;
            }
            *ppszPaths = pszPaths;
        }

        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        return pszPaths ? cFiles : 0;
    }

    VOID DoDelete()
    {
        WCHAR *pszPaths = NULL;
        SHFILEOPSTRUCTW fileOp;

        if (!GetSelectedPaths(&pszPaths) || !pszPaths)
            return;

        ZeroMemory(&fileOp, sizeof(fileOp));
        fileOp.hwnd = m_hWnd;
        fileOp.wFunc = FO_DELETE;
        fileOp.pFrom = pszPaths;
        fileOp.fFlags = FOF_ALLOWUNDO;
        SHFileOperationW(&fileOp);

        HeapFree(GetProcessHeap(), 0, pszPaths);
    }

    VOID DoRename()
    {
        CComPtr<IFolderView> pFolderView;
        LPITEMIDLIST pidlChild = NULL;
        int iItem = -1;

        if (!m_pShellView)
            return;
        if (FAILED(m_pShellView->QueryInterface(IID_PPV_ARG(IFolderView, &pFolderView))))
            return;
        if (FAILED(pFolderView->GetFocusedItem(&iItem)) || iItem < 0)
            return;
        if (FAILED(pFolderView->Item(iItem, &pidlChild)) || !pidlChild)
            return;

        m_pShellView->SelectItem(pidlChild, SVSI_SELECT | SVSI_EDIT | SVSI_ENSUREVISIBLE);
        ILFree(pidlChild);
    }

    VOID DoNewFolder()
    {
        WCHAR szPath[MAX_PATH];
        WCHAR szNew[MAX_PATH];
        int n;

        if (!m_pidlCurrent || !SHGetPathFromIDListW(m_pidlCurrent, szPath) || !szPath[0])
            return;

        StringCchPrintfW(szNew, _countof(szNew), L"%s\\New folder", szPath);
        for (n = 2; n < 100 && PathFileExistsW(szNew); n++)
            StringCchPrintfW(szNew, _countof(szNew), L"%s\\New folder (%d)", szPath, n);

        if (CreateDirectoryW(szNew, NULL))
            SHChangeNotify(SHCNE_MKDIR, SHCNF_PATHW, szNew, NULL);
    }

    VOID DoProperties()
    {
        CComPtr<IContextMenu> pContextMenu;
        CMINVOKECOMMANDINFO ici;

        if (FAILED(GetSelectionObject(IID_PPV_ARG(IContextMenu, &pContextMenu))))
            return;

        HMENU hMenu = CreatePopupMenu();
        pContextMenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);

        ZeroMemory(&ici, sizeof(ici));
        ici.cbSize = sizeof(ici);
        ici.hwnd = m_hWnd;
        ici.lpVerb = "properties";
        ici.nShow = SW_SHOWNORMAL;
        pContextMenu->InvokeCommand(&ici);

        DestroyMenu(hMenu);
    }

    VOID DoCopyPath()
    {
        WCHAR *pszPaths = NULL;
        UINT cFiles = GetSelectedPaths(&pszPaths);
        LPCWSTR pszText;
        WCHAR szCurrent[MAX_PATH];

        if (cFiles && pszPaths)
        {
            pszText = pszPaths;
        }
        else
        {
            if (!m_pidlCurrent || !SHGetPathFromIDListW(m_pidlCurrent, szCurrent))
                return;
            pszText = szCurrent;
        }

        SIZE_T cb = (wcslen(pszText) + 1) * sizeof(WCHAR);
        HGLOBAL hGlobal = GlobalAlloc(GHND, cb);
        if (hGlobal)
        {
            void *p = GlobalLock(hGlobal);
            if (p)
            {
                memcpy(p, pszText, cb);
                GlobalUnlock(hGlobal);
                if (OpenClipboard())
                {
                    EmptyClipboard();
                    SetClipboardData(CF_UNICODETEXT, hGlobal);
                    CloseClipboard();
                    hGlobal = NULL;
                }
            }
            if (hGlobal)
                GlobalFree(hGlobal);
        }

        if (pszPaths)
            HeapFree(GetProcessHeap(), 0, pszPaths);
    }

    VOID DoPinCurrent()
    {
        WCHAR szPath[MAX_PATH];
        WCHAR szValue[16];
        HKEY hKey;

        if (!m_pidlCurrent || !SHGetPathFromIDListW(m_pidlCurrent, szPath) || !szPath[0])
            return;

        if (RegCreateKeyExW(HKEY_CURRENT_USER, E11_QUICKACCESS_KEY, 0, NULL, 0,
                            KEY_READ | KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
            return;

        for (int i = 0; i < 32; i++)
        {
            WCHAR szExisting[MAX_PATH];
            DWORD cbData = sizeof(szExisting), dwType;
            StringCchPrintfW(szValue, _countof(szValue), L"Pin%d", i);
            if (RegQueryValueExW(hKey, szValue, NULL, &dwType, (LPBYTE)szExisting, &cbData) != ERROR_SUCCESS)
            {
                RegSetValueExW(hKey, szValue, 0, REG_SZ, (const BYTE *)szPath,
                               (DWORD)((wcslen(szPath) + 1) * sizeof(WCHAR)));
                break;
            }
            if (_wcsicmp(szExisting, szPath) == 0)
                break;
        }

        RegCloseKey(hKey);
        m_NavPane.Build();
        Layout();
        SyncNavSelection();
        InvalidateRect(NULL, TRUE);
    }

    VOID SetViewMode(UINT uMode)
    {
        CComPtr<IFolderView> pFolderView;
        m_uViewMode = uMode;
        if (m_pShellView &&
            SUCCEEDED(m_pShellView->QueryInterface(IID_PPV_ARG(IFolderView, &pFolderView))))
        {
            pFolderView->SetCurrentViewMode(uMode);
        }
        InvalidateRect(&m_rcRibbon, FALSE);
        InvalidateRect(&m_rcStatus, FALSE);
    }

    VOID DispatchRibbonCmd(int nCmd)
    {
        switch (nCmd)
        {
            case E11CMD_COPY:       DoClipboard(FALSE); break;
            case E11CMD_CUT:        DoClipboard(TRUE); break;
            case E11CMD_PASTE:      DoPaste(); break;
            case E11CMD_DELETE:     DoDelete(); break;
            case E11CMD_RENAME:     DoRename(); break;
            case E11CMD_NEWFOLDER:  DoNewFolder(); break;
            case E11CMD_PROPERTIES: DoProperties(); break;
            case E11CMD_PIN:        DoPinCurrent(); break;
            case E11CMD_COPYPATH:   DoCopyPath(); break;
            case E11CMD_SETTINGS:
                ShellExecuteW(NULL, NULL, L"control.exe", NULL, NULL, SW_SHOWNORMAL);
                break;
            case E11CMD_UNINSTALL:
                ShellExecuteW(NULL, NULL, L"control.exe", L"appwiz.cpl", NULL, SW_SHOWNORMAL);
                break;
            case E11CMD_SYSPROPS:
                ShellExecuteW(NULL, NULL, L"control.exe", L"sysdm.cpl", NULL, SW_SHOWNORMAL);
                break;
            case E11CMD_VIEW_EXTRALARGE: SetViewMode(FVM_THUMBNAIL); break;
            case E11CMD_VIEW_LARGE:      SetViewMode(FVM_THUMBSTRIP); break;
            case E11CMD_VIEW_MEDIUM:     SetViewMode(FVM_ICON); break;
            case E11CMD_VIEW_SMALL:      SetViewMode(FVM_SMALLICON); break;
            case E11CMD_VIEW_LIST:       SetViewMode(FVM_LIST); break;
            case E11CMD_VIEW_DETAILS:    SetViewMode(FVM_DETAILS); break;
            case E11CMD_VIEW_TILES:      SetViewMode(FVM_TILE); break;
            case E11CMD_REFRESH:         DoRefresh(); break;
        }
    }

    VOID DoSortBy(int nColumn)
    {
        if (!m_hwndView)
            return;
        HWND hList = FindWindowExW(m_hwndView, NULL, L"SysListView32", NULL);
        if (!hList)
            return;
        NMLISTVIEW nm;
        ZeroMemory(&nm, sizeof(nm));
        nm.hdr.hwndFrom = hList;
        nm.hdr.idFrom = (UINT_PTR)::GetDlgCtrlID(hList);
        nm.hdr.code = LVN_COLUMNCLICK;
        nm.iSubItem = nColumn;
        ::SendMessageW(m_hwndView, WM_NOTIFY, nm.hdr.idFrom, (LPARAM)&nm);
    }

    VOID ShowCommandMenu(int nCmd, const RECT *prcAnchor)
    {
        HMENU hMenu = CreatePopupMenu();
        if (!hMenu)
            return;

        switch (nCmd)
        {
            case E11CMD_NEWMENU:
                AppendMenuW(hMenu, MF_STRING, 1, L"Folder");
                break;
            case E11CMD_SORTMENU:
                AppendMenuW(hMenu, MF_STRING, 10, L"Name");
                AppendMenuW(hMenu, MF_STRING, 11, L"Size");
                AppendMenuW(hMenu, MF_STRING, 12, L"Type");
                AppendMenuW(hMenu, MF_STRING, 13, L"Date modified");
                break;
            case E11CMD_VIEWMENU:
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_THUMBNAIL ? MF_CHECKED : 0), 20, L"Extra large icons");
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_THUMBSTRIP ? MF_CHECKED : 0), 21, L"Large icons");
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_ICON ? MF_CHECKED : 0), 22, L"Medium icons");
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_SMALLICON ? MF_CHECKED : 0), 23, L"Small icons");
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_LIST ? MF_CHECKED : 0), 24, L"List");
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_DETAILS ? MF_CHECKED : 0), 25, L"Details");
                AppendMenuW(hMenu, MF_STRING | (m_uViewMode == FVM_TILE ? MF_CHECKED : 0), 26, L"Tiles");
                break;
            case E11CMD_MOREMENU:
                AppendMenuW(hMenu, MF_STRING, 30, L"Properties");
                AppendMenuW(hMenu, MF_STRING, 31, L"Copy as path");
                AppendMenuW(hMenu, MF_STRING, 32, L"Refresh");
                AppendMenuW(hMenu, MF_STRING, 33, L"Pin to Quick access");
                break;
        }

        POINT pt = { prcAnchor->left, prcAnchor->bottom };
        ClientToScreen(&pt);
        UINT uCmd = (UINT)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                         pt.x, pt.y, 0, m_hWnd, NULL);
        DestroyMenu(hMenu);

        switch (uCmd)
        {
            case 1:  DoNewFolder(); break;
            case 10: DoSortBy(0); break;
            case 11: DoSortBy(1); break;
            case 12: DoSortBy(2); break;
            case 13: DoSortBy(3); break;
            case 20: SetViewMode(FVM_THUMBNAIL); break;
            case 21: SetViewMode(FVM_THUMBSTRIP); break;
            case 22: SetViewMode(FVM_ICON); break;
            case 23: SetViewMode(FVM_SMALLICON); break;
            case 24: SetViewMode(FVM_LIST); break;
            case 25: SetViewMode(FVM_DETAILS); break;
            case 26: SetViewMode(FVM_TILE); break;
            case 30: DoProperties(); break;
            case 31: DoCopyPath(); break;
            case 32: DoRefresh(); break;
            case 33: DoPinCurrent(); break;
        }
    }

    VOID DoRefresh()
    {
        if (m_nContent != CONTENT_SHELL && m_pThisPC)
        {
            m_pThisPC->BuildThisPC();
            m_pThisPC->InvalidateRect(NULL, TRUE);
        }
        else if (m_pShellView)
        {
            m_pShellView->Refresh();
        }
        UpdateItemCount();
    }

    VOID ShowFileMenu()
    {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1, L"New window");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, 2, L"Close");

        POINT pt = { m_aTabRects[0].left, m_aTabRects[0].bottom };
        ClientToScreen(&pt);
        int nCmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, NULL);
        DestroyMenu(hMenu);

        if (nCmd == 1)
        {
            WCHAR szExe[MAX_PATH];
            WCHAR szPath[MAX_PATH];
            szPath[0] = 0;
            if (m_pidlCurrent)
                SHGetPathFromIDListW(m_pidlCurrent, szPath);
            GetModuleFileNameW(NULL, szExe, _countof(szExe));
            ShellExecuteW(NULL, NULL, szExe, szPath, NULL, SW_SHOWNORMAL);
        }
        else if (nCmd == 2)
        {
            PostMessage(WM_CLOSE);
        }
    }

    VOID ShowHistoryMenu()
    {
        HMENU hMenu = CreatePopupMenu();
        int nId = 1;

        for (int i = (int)m_BackStack.GetCount() - 1; i >= 0 && nId <= 8; i--, nId++)
        {
            WCHAR szName[64];
            GetPidlName(m_BackStack[i], szName, _countof(szName));
            AppendMenuW(hMenu, MF_STRING, nId, szName[0] ? szName : L"?");
        }

        if (m_pidlCurrent)
        {
            WCHAR szName[64];
            GetPidlName(m_pidlCurrent, szName, _countof(szName));
            AppendMenuW(hMenu, MF_STRING | MF_CHECKED, 100, szName[0] ? szName : L"?");
        }

        int nFwdId = 101;
        for (int i = (int)m_FwdStack.GetCount() - 1; i >= 0 && nFwdId <= 108; i--, nFwdId++)
        {
            WCHAR szName[64];
            GetPidlName(m_FwdStack[i], szName, _countof(szName));
            AppendMenuW(hMenu, MF_STRING, nFwdId, szName[0] ? szName : L"?");
        }

        POINT pt = { m_rcHistory.left, m_rcHistory.bottom };
        ClientToScreen(&pt);
        int nSel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN, pt.x, pt.y, 0, m_hWnd, NULL);
        DestroyMenu(hMenu);

        if (nSel >= 1 && nSel <= 8)
        {
            for (int i = 0; i < nSel; i++)
                NavigateBack();
        }
        else if (nSel >= 101 && nSel <= 108)
        {
            for (int i = 0; i <= nSel - 101; i++)
                NavigateForward();
        }
    }

    // *** Search ***
    VOID ActivateSearch()
    {
        if (m_hwndSearchEdit)
        {
            ::SetFocus(m_hwndSearchEdit);
            return;
        }

        m_hwndSearchEdit = CreateWindowExW(0, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                           m_rcSearchBox.left + Sc(26), m_rcSearchBox.top + Sc(5),
                                           m_rcSearchBox.right - m_rcSearchBox.left - Sc(32),
                                           m_rcSearchBox.bottom - m_rcSearchBox.top - Sc(9),
                                           m_hWnd, (HMENU)(INT_PTR)500, g_hInstance, NULL);
        if (m_hwndSearchEdit)
        {
            ::SendMessageW(m_hwndSearchEdit, WM_SETFONT, (WPARAM)m_hFont, TRUE);
            SetWindowSubclass(m_hwndSearchEdit, E11SearchEditProc, 1, (DWORD_PTR)m_hWnd);
            ::SetFocus(m_hwndSearchEdit);
        }
        InvalidateRect(&m_rcSearchBox, FALSE);
    }

    VOID CloseSearchEdit()
    {
        if (m_hwndSearchEdit)
        {
            RemoveWindowSubclass(m_hwndSearchEdit, E11SearchEditProc, 1);
            ::DestroyWindow(m_hwndSearchEdit);
            m_hwndSearchEdit = NULL;
        }
        InvalidateRect(&m_rcSearchBox, FALSE);
    }

    LRESULT OnSearchEnter(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        WCHAR szQuery[128];
        WCHAR szRoot[MAX_PATH];

        szQuery[0] = 0;
        if (m_hwndSearchEdit)
            ::GetWindowTextW(m_hwndSearchEdit, szQuery, _countof(szQuery));
        if (!szQuery[0])
            return 0;

        szRoot[0] = 0;
        if (m_pidlCurrent)
            SHGetPathFromIDListW(m_pidlCurrent, szRoot);
        if (!szRoot[0])
        {
            DWORD dwDrives = GetLogicalDrives();
            for (int i = 2; i < 26; i++)
            {
                WCHAR szTest[8];
                if (!(dwDrives & (1 << i)))
                    continue;
                StringCchPrintfW(szTest, _countof(szTest), L"%c:\\", L'A' + i);
                if (GetDriveTypeW(szTest) == DRIVE_FIXED)
                {
                    StringCchCopyW(szRoot, _countof(szRoot), szTest);
                    break;
                }
            }
        }
        if (!szRoot[0])
            StringCchCopyW(szRoot, _countof(szRoot), L"C:\\");

        EnsureThisPCChild();
        if (m_hwndView)
            ::ShowWindow(m_hwndView, SW_HIDE);
        m_nContent = CONTENT_SEARCH;
        m_pThisPC->m_Pal = m_Pal;

        ::SetWindowPos(m_pThisPC->m_hWnd, NULL,
                       m_rcViewArea.left, m_rcViewArea.top,
                       m_rcViewArea.right - m_rcViewArea.left,
                       m_rcViewArea.bottom - m_rcViewArea.top,
                       SWP_NOZORDER | SWP_SHOWWINDOW);

        m_pThisPC->StartSearch(szRoot, szQuery);
        UpdateItemCount();
        return 0;
    }

    LRESULT OnSearchEsc(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        CloseSearchEdit();

        if (m_nContent == CONTENT_SEARCH)
        {
            if (m_pThisPC)
                m_pThisPC->StopSearch();
            if (m_pidlCurrent && m_pidlThisPC && ILIsEqual(m_pidlCurrent, m_pidlThisPC))
            {
                m_pThisPC->BuildThisPC();
                m_nContent = CONTENT_THISPC;
            }
            else
            {
                if (m_pThisPC && m_pThisPC->IsWindow())
                    m_pThisPC->ShowWindow(SW_HIDE);
                if (m_hwndView)
                    ::ShowWindow(m_hwndView, SW_SHOW);
                m_nContent = CONTENT_SHELL;
            }
            UpdateItemCount();
        }
        return 0;
    }

    LRESULT OnOpenTile(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int i = (int)wParam;
        if (!m_pThisPC || i < 0 || i >= (int)m_pThisPC->m_Tiles.GetCount())
            return 0;

        LPITEMIDLIST pidl = NULL;
        if (SUCCEEDED(SHParseDisplayName(m_pThisPC->m_Tiles[i].szPath, NULL, &pidl, 0, NULL)) && pidl)
        {
            BrowseTo(pidl, TRUE);
            ILFree(pidl);
        }
        return 0;
    }

    LRESULT OnOpenResult(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        int i = (int)wParam;
        if (!m_pThisPC || i < 0 || i >= (int)m_pThisPC->m_Results.GetCount())
            return 0;

        E11_SEARCHROW &row = m_pThisPC->m_Results[i];
        WCHAR szTarget[MAX_PATH];

        if (row.bFolder)
            StringCchPrintfW(szTarget, _countof(szTarget), L"%s\\%s", row.szDir, row.szName);
        else
            StringCchCopyW(szTarget, _countof(szTarget), row.szDir);

        LPITEMIDLIST pidl = NULL;
        if (SUCCEEDED(SHParseDisplayName(szTarget, NULL, &pidl, 0, NULL)) && pidl)
        {
            CloseSearchEdit();
            BrowseTo(pidl, TRUE);
            ILFree(pidl);
        }
        return 0;
    }

    // *** Mouse ***
    LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iHotNav = 0;
        if (PtInRect(&m_rcBack, pt)) iHotNav = 1;
        else if (PtInRect(&m_rcFwd, pt)) iHotNav = 2;
        else if (PtInRect(&m_rcUp, pt)) iHotNav = 3;
        else if (PtInRect(&m_rcHistory, pt)) iHotNav = 4;

        int iHotPane = PtInRect(&m_rcPane, pt) ? m_NavPane.HitTest(pt) : -1;
        if (iHotPane < 0 && PtInRect(&m_rcPane, pt))
        {
            int iRoot;
            if (m_NavPane.HitChevron(pt, &iRoot))
                iHotPane = iRoot;
        }

        int iHotTab = -1;
        for (int t = 0; t < m_nTabCount; t++)
        {
            if (PtInRect(&m_aTabRects[t], pt))
            {
                iHotTab = t;
                break;
            }
        }

        int iHotBtn = -1;
        for (SIZE_T b = 0; b < m_RibbonBtns.GetCount(); b++)
        {
            if (PtInRect(&m_RibbonBtns[b].rc, pt))
            {
                iHotBtn = (int)b;
                break;
            }
        }

        int iHotCrumb = -1;
        for (SIZE_T c = 0; c < m_Crumbs.GetCount(); c++)
        {
            if (PtInRect(&m_Crumbs[c].rc, pt))
            {
                iHotCrumb = (int)c;
                break;
            }
        }

        int nHotMisc = HM_NONE;
        if (PtInRect(&m_rcRefresh, pt)) nHotMisc = HM_REFRESH;
        else if (PtInRect(&m_rcCollapse, pt)) nHotMisc = HM_COLLAPSE;
        else if (PtInRect(&m_rcQat1, pt)) nHotMisc = HM_QAT1;
        else if (PtInRect(&m_rcQat2, pt)) nHotMisc = HM_QAT2;
        else if (PtInRect(&m_rcSearchBox, pt)) nHotMisc = HM_SEARCH;
        else if (PtInRect(&m_rcStDetails, pt)) nHotMisc = HM_ST_DETAILS;
        else if (PtInRect(&m_rcStIcons, pt)) nHotMisc = HM_ST_ICONS;

        if (iHotNav != m_iHotNav || iHotPane != m_NavPane.m_iHot ||
            iHotTab != m_iHotTab || iHotBtn != m_iHotBtn ||
            iHotCrumb != m_iHotCrumb || nHotMisc != m_nHotMisc)
        {
            m_iHotNav = iHotNav;
            m_NavPane.m_iHot = iHotPane;
            m_iHotTab = iHotTab;
            m_iHotBtn = iHotBtn;
            m_iHotCrumb = iHotCrumb;
            m_nHotMisc = nHotMisc;
            InvalidateRect(NULL, FALSE);
        }
        return 0;
    }

    LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

        if (PtInRect(&m_rcCollapse, pt))
        {
            PostMessageW(WM_CLOSE, 0, 0);
            return 0;
        }
        if (PtInRect(&m_rcQat1, pt))
        {
            WCHAR szPath[MAX_PATH] = L"";
            if (m_pidlCurrent && !SHGetPathFromIDListW(m_pidlCurrent, szPath))
                szPath[0] = 0;
            ShellExecuteW(NULL, NULL, L"explorer.exe", szPath[0] ? szPath : NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }

        for (SIZE_T b = 0; b < m_RibbonBtns.GetCount(); b++)
        {
            if (PtInRect(&m_RibbonBtns[b].rc, pt))
            {
                if (!m_RibbonBtns[b].bDisabled)
                {
                    int nCmd = m_RibbonBtns[b].nCmd;
                    if (nCmd == E11CMD_NEWMENU || nCmd == E11CMD_SORTMENU ||
                        nCmd == E11CMD_VIEWMENU || nCmd == E11CMD_MOREMENU)
                        ShowCommandMenu(nCmd, &m_RibbonBtns[b].rc);
                    else
                        DispatchRibbonCmd(nCmd);
                }
                return 0;
            }
        }

        if (PtInRect(&m_rcBack, pt))
        {
            NavigateBack();
            return 0;
        }
        if (PtInRect(&m_rcFwd, pt))
        {
            NavigateForward();
            return 0;
        }
        if (PtInRect(&m_rcHistory, pt))
        {
            ShowHistoryMenu();
            return 0;
        }
        if (PtInRect(&m_rcUp, pt))
        {
            NavigateUp();
            return 0;
        }
        if (PtInRect(&m_rcRefresh, pt))
        {
            DoRefresh();
            return 0;
        }

        for (SIZE_T c = 0; c < m_Crumbs.GetCount(); c++)
        {
            if (PtInRect(&m_Crumbs[c].rc, pt))
            {
                if (m_Crumbs[c].pidl)
                {
                    LPITEMIDLIST pidl = ILClone(m_Crumbs[c].pidl);
                    if (pidl)
                    {
                        BrowseTo(pidl, TRUE);
                        ILFree(pidl);
                    }
                }
                return 0;
            }
        }

        if (PtInRect(&m_rcSearchBox, pt))
        {
            ActivateSearch();
            return 0;
        }

        if (PtInRect(&m_rcStDetails, pt))
        {
            SetViewMode(FVM_DETAILS);
            return 0;
        }
        if (PtInRect(&m_rcStIcons, pt))
        {
            SetViewMode(FVM_ICON);
            return 0;
        }

        int iItemChev;
        if (m_NavPane.HitItemChevron(pt, &iItemChev))
        {
            m_NavPane.ToggleItem(iItemChev);
            m_NavPane.Layout(&m_rcPane);
            InvalidateRect(NULL, TRUE);
            return 0;
        }

        int iChevRoot;
        if (m_NavPane.HitChevron(pt, &iChevRoot))
        {
            E11_NAVITEM &root = m_NavPane.m_Items[iChevRoot];
            if (root.pidl && pt.x > root.rcChevron.right + Sc(2))
            {
                BrowseTo(root.pidl, TRUE);
            }
            else
            {
                root.bExpanded = !root.bExpanded;
                m_NavPane.Layout(&m_rcPane);
                InvalidateRect(NULL, TRUE);
            }
            return 0;
        }

        int iNav = m_NavPane.HitTest(pt);
        if (iNav >= 0 && m_NavPane.m_Items[iNav].pidl)
            BrowseTo(m_NavPane.m_Items[iNav].pidl, TRUE);

        return 0;
    }

    LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(&pt);
        if (!PtInRect(&m_rcPane, pt))
        {
            bHandled = FALSE;
            return 0;
        }
        int nDelta = -MulDiv(GET_WHEEL_DELTA_WPARAM(wParam), Sc(56), WHEEL_DELTA);
        m_NavPane.Scroll(nDelta, &m_rcPane);
        InvalidateRect(&m_rcPane, FALSE);
        return 0;
    }

    LRESULT OnSetFocus(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        if (m_nContent == CONTENT_SHELL && m_hwndView)
            ::SetFocus(m_hwndView);
        return 0;
    }

    LRESULT OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL &bHandled)
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int iNav = m_NavPane.HitTest(pt);

        if (iNav < 0 || !m_NavPane.m_Items[iNav].bPinned)
            return 0;

        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1, L"Unpin from Quick access");

        POINT ptScreen = pt;
        ClientToScreen(&ptScreen);
        int nCmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN,
                                  ptScreen.x, ptScreen.y, 0, m_hWnd, NULL);
        DestroyMenu(hMenu);

        if (nCmd == 1 && m_NavPane.m_Items[iNav].pidl)
        {
            WCHAR szTarget[MAX_PATH];
            if (SHGetPathFromIDListW(m_NavPane.m_Items[iNav].pidl, szTarget))
            {
                HKEY hKey;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, E11_QUICKACCESS_KEY, 0,
                                  KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS)
                {
                    for (int i = 0; i < 32; i++)
                    {
                        WCHAR szValue[16], szPath[MAX_PATH];
                        DWORD cbData = sizeof(szPath), dwType;
                        StringCchPrintfW(szValue, _countof(szValue), L"Pin%d", i);
                        if (RegQueryValueExW(hKey, szValue, NULL, &dwType,
                                             (LPBYTE)szPath, &cbData) == ERROR_SUCCESS &&
                            _wcsicmp(szPath, szTarget) == 0)
                        {
                            RegDeleteValueW(hKey, szValue);
                        }
                    }
                    RegCloseKey(hKey);
                }
            }
            m_NavPane.Build();
            Layout();
            SyncNavSelection();
            InvalidateRect(NULL, TRUE);
        }
        return 0;
    }

    BEGIN_MSG_MAP(CExplorerFrame)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
        MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
        MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
        MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
        MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
        MESSAGE_HANDLER(E11M_SEARCHENTER, OnSearchEnter)
        MESSAGE_HANDLER(E11M_SEARCHESC, OnSearchEsc)
        MESSAGE_HANDLER(E11M_OPENTILE, OnOpenTile)
        MESSAGE_HANDLER(E11M_OPENRESULT, OnOpenResult)
    END_MSG_MAP()
};

static HRESULT
E11GetStartPidl(LPCWSTR pszCmdLine, LPITEMIDLIST *ppidl)
{
    *ppidl = NULL;

    if (pszCmdLine && pszCmdLine[0])
    {
        WCHAR szPath[MAX_PATH];
        StringCchCopyW(szPath, _countof(szPath), pszCmdLine);
        PathUnquoteSpacesW(szPath);
        if (SUCCEEDED(SHParseDisplayName(szPath, NULL, ppidl, 0, NULL)) && *ppidl)
            return S_OK;
    }

    return SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, ppidl);
}

extern "C" INT WINAPI Explorer_Main(HINSTANCE hInstance, LPWSTR lpCmdLine);

int WINAPI
wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    INITCOMMONCONTROLSEX icc;
    LPITEMIDLIST pidlStart = NULL;
    MSG msg;

    g_hInstance = hInstance;

    if (GetShellWindow() == NULL && (!lpCmdLine || !lpCmdLine[0]))
        return Explorer_Main(hInstance, lpCmdLine);

    if (FAILED(OleInitialize(NULL)))
        return 1;

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES;
    InitCommonControlsEx(&icc);

    CExplorerFrame *pFrame = new CExplorerFrame();
    if (!pFrame)
    {
        OleUninitialize();
        return 1;
    }

    RECT rcInit = { CW_USEDEFAULT, CW_USEDEFAULT, 0, 0 };
    if (!pFrame->Create(NULL, rcInit, L"This PC",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN))
    {
        delete pFrame;
        OleUninitialize();
        return 1;
    }

    pFrame->SetWindowPos(NULL, 0, 0, E11Scale(1000), E11Scale(660),
                         SWP_NOMOVE | SWP_NOZORDER);
    pFrame->ShowWindow(nShowCmd == SW_HIDE ? SW_SHOW : nShowCmd);
    pFrame->UpdateWindow();

    if (SUCCEEDED(E11GetStartPidl(lpCmdLine, &pidlStart)) && pidlStart)
    {
        pFrame->BrowseTo(pidlStart, FALSE);
        ILFree(pidlStart);
    }

    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (pFrame->m_hwndSearchEdit && ::GetFocus() == pFrame->m_hwndSearchEdit)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        if (pFrame->m_pShellView &&
            pFrame->m_pShellView->TranslateAccelerator(&msg) == S_OK)
            continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete pFrame;
    OleUninitialize();
    return 0;
}
