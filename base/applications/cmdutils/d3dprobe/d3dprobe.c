/*
 * PROJECT:     LakoOS command line utilities
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Creates a Direct3D 11 device from the d3d11.dll found next to
 *              the program (or in System32) and prints the adapter and level
 */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>

static const GUID MY_IID_IDXGIDevice =
    { 0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c } };

typedef HRESULT (WINAPI *PFN_CREATE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                     const D3D_FEATURE_LEVEL *, UINT, UINT,
                                     ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

int wmain(void)
{
    WCHAR path[MAX_PATH];
    HMODULE hMod;
    PFN_CREATE create;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    D3D_FEATURE_LEVEL level = 0;
    HRESULT hr;
    IDXGIDevice *dxgi = NULL;
    DWORD t0, t1;

    hMod = LoadLibraryW(L"d3d11.dll");
    GetModuleFileNameW(hMod, path, MAX_PATH);
    wprintf(L"d3d11: %ls\n", path);
    create = hMod ? (PFN_CREATE)GetProcAddress(hMod, "D3D11CreateDevice") : NULL;
    if (!create)
    {
        wprintf(L"D3D11CreateDevice not found (error %lu)\n", GetLastError());
        return 1;
    }
    hr = create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &level, &ctx);
    wprintf(L"CreateDevice hr=0x%08lx level=0x%x\n", (ULONG)hr, (UINT)level);
    if (FAILED(hr))
        return 2;
    if (SUCCEEDED(ID3D11Device_QueryInterface(dev, &MY_IID_IDXGIDevice, (void **)&dxgi)))
    {
        IDXGIAdapter *ad = NULL;
        DXGI_ADAPTER_DESC d;

        if (SUCCEEDED(IDXGIDevice_GetAdapter(dxgi, &ad)) && SUCCEEDED(IDXGIAdapter_GetDesc(ad, &d)))
            wprintf(L"adapter: %ls vendor=%04x\n", d.Description, d.VendorId);
        if (ad) IDXGIAdapter_Release(ad);
        IDXGIDevice_Release(dxgi);
    }
    {
        D3D11_TEXTURE2D_DESC td = { 512, 512, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 },
                                    D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
        ID3D11Texture2D *tex = NULL;
        ID3D11RenderTargetView *rtv = NULL;
        FLOAT clr[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
        int i;

        hr = ID3D11Device_CreateTexture2D(dev, &td, NULL, &tex);
        wprintf(L"CreateTexture2D hr=0x%08lx\n", (ULONG)hr);
        if (SUCCEEDED(hr) && SUCCEEDED(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)tex, NULL, &rtv)))
        {
            t0 = GetTickCount();
            for (i = 0; i < 200; i++)
                ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clr);
            ID3D11DeviceContext_Flush(ctx);
            t1 = GetTickCount();
            wprintf(L"200 clears of 512x512: %lu ms\n", t1 - t0);
            ID3D11RenderTargetView_Release(rtv);
        }
        if (tex) ID3D11Texture2D_Release(tex);
    }
    ID3D11DeviceContext_Release(ctx);
    ID3D11Device_Release(dev);
    return 0;
}
