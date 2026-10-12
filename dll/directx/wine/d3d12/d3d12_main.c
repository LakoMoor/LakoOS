/*
 * Copyright 2018 Józef Kucia for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <d3d12.h>
#include <vkd3d_utils.h>

#ifdef REACTOS_D3D12_NATIVE_UMD
HRESULT d3d12_native_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid,
        void **device);
#endif

HRESULT WINAPI
d3d12_create_device(IUnknown *adapter, D3D_FEATURE_LEVEL minimum_feature_level, REFIID iid, void **device)
{
#ifdef REACTOS_D3D12_NATIVE_UMD
    HRESULT hr = d3d12_native_create_device(adapter, minimum_feature_level, iid, device);

    if (hr != DXGI_ERROR_UNSUPPORTED)
        return hr;
#endif
    return D3D12CreateDeviceVKD3D(adapter, minimum_feature_level, iid, device, VKD3D_API_VERSION_1_0);
}

HRESULT WINAPI
D3D12EnableExperimentalFeatures(UINT FeatureCount, const IID *Iids, void *Configurations, UINT *ConfigurationSizes)
{
    UNREFERENCED_PARAMETER(FeatureCount);
    UNREFERENCED_PARAMETER(Iids);
    UNREFERENCED_PARAMETER(Configurations);
    UNREFERENCED_PARAMETER(ConfigurationSizes);

    return E_NOINTERFACE;
}

HRESULT WINAPI
D3D12GetInterface(REFCLSID clsid, REFIID iid, void **debug)
{
    if (debug)
        *debug = NULL;

    if (IsEqualGUID(clsid, &CLSID_D3D12Debug))
        return D3D12GetDebugInterface(iid, debug);

    return E_NOINTERFACE;
}
