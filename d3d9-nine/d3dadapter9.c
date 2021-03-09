/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Wine IDirect3D9 interface using ID3DAdapter9
 *
 * Copyright 2013 Joakim Sindholt
 *                Christoph Bumiller
 * Copyright 2014 David Heidelberger
 * Copyright 2014-2015 Axel Davy
 * Copyright 2015 Nick Sarnie
 *                Patrick Rudolph
 */

#include <d3dadapter/d3dadapter9.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "../common/debug.h"
#include "present.h"
#include "backend.h"

const GUID IID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_IDirect3D9 = { 0x81bdcbca, 0x64d4, 0x426d, { 0xae, 0x8d, 0xad, 0x1, 0x47, 0xf4, 0x27, 0x5c } };
const GUID IID_IDirect3D9Ex = { 0x02177241, 0x69fc, 0x400c, { 0x8f, 0xf1, 0x93, 0xa4, 0x4d, 0xf6, 0x86, 0x1d } };

/* this represents a snapshot taken at the moment of creation */
struct output
{
    SDL_DisplayMode *modes;
    unsigned nmodes;
    unsigned nmodesalloc;

    HMONITOR monitor;
};

struct adapter_group
{
    struct output *outputs;
    unsigned noutputs;
    unsigned noutputsalloc;

    /* driver stuff */
    ID3DAdapter9 *adapter;
    /* DRI backend */
    struct dri_backend *dri_backend;
};

struct adapter_map
{
    unsigned group;
    unsigned master;
};

struct d3dadapter9
{
    /* COM vtable */
    void *vtable;
    /* IUnknown reference count */
    LONG refs;

    /* adapter groups and mappings */
    struct adapter_group *groups;
    struct adapter_map *map;
    unsigned nadapters;
    unsigned ngroups;
    unsigned ngroupsalloc;

    /* true if it implements IDirect3D9Ex */
    BOOL ex;
    Display *gdi_display;
};

/* convenience wrapper for calls into ID3D9Adapter */
#define ADAPTER_GROUP \
    This->groups[This->map[Adapter].group]

#define ADAPTER_PROC(name, ...) \
    ID3DAdapter9_##name(ADAPTER_GROUP.adapter, ## __VA_ARGS__)

#define ADAPTER_OUTPUT \
    ADAPTER_GROUP.outputs[Adapter-This->map[Adapter].master]

static HRESULT WINAPI d3dadapter9_CheckDeviceFormat(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
        DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat);

static ULONG WINAPI d3dadapter9_AddRef(struct d3dadapter9 *This)
{
    ULONG refs = InterlockedIncrement(&This->refs);
    TRACE("%p increasing refcount to %u.\n", This, refs);
    return refs;
}

static ULONG WINAPI d3dadapter9_Release(struct d3dadapter9 *This)
{
    ULONG refs = InterlockedDecrement(&This->refs);
    TRACE("%p decreasing refcount to %u.\n", This, refs);
    if (refs == 0)
    {
        /* dtor */
        if (This->map)
        {
            free(This->map);
        }

        if (This->groups)
        {
            int i, j;
            for (i = 0; i < This->ngroups; ++i)
            {
                if (This->groups[i].outputs)
                {
                    for (j = 0; j < This->groups[i].noutputs; ++j)
                    {
                        if (This->groups[i].outputs[j].modes)
                        {
                            free(This->groups[i].outputs[j].modes);
                        }
                    }
                    free(This->groups[i].outputs);
                }

                if (This->groups[i].adapter)
                    ID3DAdapter9_Release(This->groups[i].adapter);

                backend_destroy(This->groups[i].dri_backend);
            }
            free(This->groups);
        }

        free(This);
    }
    return refs;
}

static HRESULT WINAPI d3dadapter9_QueryInterface(struct d3dadapter9 *This,
        REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if ((IsEqualGUID(&IID_IDirect3D9Ex, riid) && This->ex) ||
            IsEqualGUID(&IID_IDirect3D9, riid) ||
            IsEqualGUID(&IID_IUnknown, riid))
    {
        *ppvObject = This;
        d3dadapter9_AddRef(This);
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", nine_dbgstr_guid(riid));
    *ppvObject = NULL;

    return E_NOINTERFACE;
}

static HRESULT WINAPI d3dadapter9_RegisterSoftwareDevice(struct d3dadapter9 *This,
        void *pInitializeFunction)
{
    FIXME("(%p, %p), stub!\n", This, pInitializeFunction);
    return D3DERR_INVALIDCALL;
}

static UINT WINAPI d3dadapter9_GetAdapterCount(struct d3dadapter9 *This)
{
    return This->nadapters;
}

static HRESULT WINAPI d3dadapter9_GetAdapterIdentifier(struct d3dadapter9 *This,
        UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    return ADAPTER_PROC(GetAdapterIdentifier, Flags, pIdentifier);
}

static UINT WINAPI d3dadapter9_GetAdapterModeCount(struct d3dadapter9 *This,
        UINT Adapter, D3DFORMAT Format)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    if (FAILED(d3dadapter9_CheckDeviceFormat(This, Adapter, D3DDEVTYPE_HAL,
            Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, Format)))
    {
        WARN("DeviceFormat not available.\n");
        return 0;
    }

    TRACE("%u modes.\n", ADAPTER_OUTPUT.nmodes);
    return ADAPTER_OUTPUT.nmodes;
}

static HRESULT WINAPI d3dadapter9_EnumAdapterModes(struct d3dadapter9 *This,
        UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode)
{
    HRESULT hr;

    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    hr = d3dadapter9_CheckDeviceFormat(This, Adapter, D3DDEVTYPE_HAL,
            Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, Format);

    if (FAILED(hr))
    {
        TRACE("DeviceFormat not available.\n");
        return hr;
    }

    if (Mode >= ADAPTER_OUTPUT.nmodes)
    {
        WARN("Mode %u does not exist.\n", Mode);
        return D3DERR_INVALIDCALL;
    }

    pMode->Width = ADAPTER_OUTPUT.modes[Mode].w;
    pMode->Height = ADAPTER_OUTPUT.modes[Mode].h;
    pMode->RefreshRate = ADAPTER_OUTPUT.modes[Mode].refresh_rate;
    pMode->Format = Format;

    return D3D_OK;
}

static HRESULT WINAPI d3dadapter9_GetAdapterDisplayMode(struct d3dadapter9 *This,
        UINT Adapter, D3DDISPLAYMODE *pMode)
{
    SDL_DisplayMode Mode;

    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    ZeroMemory(&Mode, sizeof(Mode));
    if (SDL_GetCurrentDisplayMode(Adapter, &Mode) < 0)
        return D3DERR_INVALIDCALL;

    pMode->Width = Mode.w;
    pMode->Height = Mode.h;
    pMode->RefreshRate = Mode.refresh_rate;
    pMode->Format = to_d3d_format(Mode.format);

    return D3D_OK;
}

static HRESULT WINAPI d3dadapter9_CheckDeviceType(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat,
        D3DFORMAT BackBufferFormat, BOOL bWindowed)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    return ADAPTER_PROC(CheckDeviceType,
            DevType, AdapterFormat, BackBufferFormat, bWindowed);
}

static HRESULT WINAPI d3dadapter9_CheckDeviceFormat(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
        DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    return ADAPTER_PROC(CheckDeviceFormat,
             DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}

static HRESULT WINAPI d3dadapter9_CheckDeviceMultiSampleType(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat,
        BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    return ADAPTER_PROC(CheckDeviceMultiSampleType, DeviceType, SurfaceFormat,
            Windowed, MultiSampleType, pQualityLevels);
}

static HRESULT WINAPI d3dadapter9_CheckDepthStencilMatch(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
        D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    return ADAPTER_PROC(CheckDepthStencilMatch, DeviceType, AdapterFormat,
            RenderTargetFormat, DepthStencilFormat);
}

static HRESULT WINAPI d3dadapter9_CheckDeviceFormatConversion(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
{
    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    return ADAPTER_PROC(CheckDeviceFormatConversion,
            DeviceType, SourceFormat, TargetFormat);
}

static HRESULT WINAPI d3dadapter9_GetDeviceCaps(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps)
{
    HRESULT hr;

    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    hr = ADAPTER_PROC(GetDeviceCaps, DeviceType, pCaps);
    if (FAILED(hr))
        return hr;

    pCaps->MasterAdapterOrdinal = This->map[Adapter].master;
    pCaps->AdapterOrdinalInGroup = Adapter-This->map[Adapter].master;
    pCaps->NumberOfAdaptersInGroup = ADAPTER_GROUP.noutputs;

    return hr;
}

static HMONITOR WINAPI d3dadapter9_GetAdapterMonitor(struct d3dadapter9 *This,
        UINT Adapter)
{
    // TODO: stub
    return (HMONITOR)0;
}

static HRESULT WINAPI d3dadapter9_CreateDeviceEx(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        D3DDISPLAYMODEEX *pFullscreenDisplayMode,
        IDirect3DDevice9Ex **ppReturnedDeviceInterface);

static HRESULT WINAPI d3dadapter9_CreateDevice(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        IDirect3DDevice9 **ppReturnedDeviceInterface)
{
    HRESULT hr;
    hr = d3dadapter9_CreateDeviceEx(This, Adapter, DeviceType, hFocusWindow,
            BehaviorFlags, pPresentationParameters, NULL,
            (IDirect3DDevice9Ex **)ppReturnedDeviceInterface);
    if (FAILED(hr))
        return hr;

    return D3D_OK;
}

static UINT WINAPI d3dadapter9_GetAdapterModeCountEx(struct d3dadapter9 *This,
        UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter)
{
    FIXME("(%p, %u, %p), half stub!\n", This, Adapter, pFilter);
    return d3dadapter9_GetAdapterModeCount(This, Adapter, pFilter->Format);
}

static HRESULT WINAPI d3dadapter9_EnumAdapterModesEx(struct d3dadapter9 *This,
        UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode,
        D3DDISPLAYMODEEX *pMode)
{
    HRESULT hr;

    FIXME("(%p, %u, %p, %u, %p), half stub!\n", This, Adapter, pFilter, Mode, pMode);

    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    hr = d3dadapter9_CheckDeviceFormat(This, Adapter, D3DDEVTYPE_HAL,
            pFilter->Format, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, pFilter->Format);

    if (FAILED(hr))
    {
        TRACE("DeviceFormat not available.\n");
        return hr;
    }

    if (Mode >= ADAPTER_OUTPUT.nmodes)
    {
        WARN("Mode %u does not exist.\n", Mode);
        return D3DERR_INVALIDCALL;
    }

    pMode->Size = sizeof(D3DDISPLAYMODEEX);
    pMode->Width = ADAPTER_OUTPUT.modes[Mode].w;
    pMode->Height = ADAPTER_OUTPUT.modes[Mode].h;
    pMode->RefreshRate = ADAPTER_OUTPUT.modes[Mode].refresh_rate;
    pMode->Format = to_d3d_format(ADAPTER_OUTPUT.modes[Mode].format);
    pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;

    return D3D_OK;
}

static HRESULT WINAPI d3dadapter9_GetAdapterDisplayModeEx(struct d3dadapter9 *This,
        UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
    SDL_DisplayMode Mode;

    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    if (pMode)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        if (SDL_GetCurrentDisplayMode(Adapter, &Mode) < 0)
            return D3DERR_INVALIDCALL;

        pMode->Size = sizeof(D3DDISPLAYMODEEX);
        pMode->Width = Mode.w;
        pMode->Height = Mode.h;
        pMode->RefreshRate = Mode.refresh_rate;
        pMode->Format = to_d3d_format(Mode.format);
        pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    }
    if (pRotation)
        *pRotation = D3DDISPLAYROTATION_IDENTITY;

    return D3D_OK;
}

static HRESULT WINAPI d3dadapter9_CreateDeviceEx(struct d3dadapter9 *This,
        UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
        D3DPRESENT_PARAMETERS *pPresentationParameters,
        D3DDISPLAYMODEEX *pFullscreenDisplayMode,
        IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
    ID3DPresentGroup *present;
    HRESULT hr;

    if (Adapter >= d3dadapter9_GetAdapterCount(This))
        return D3DERR_INVALIDCALL;

    {
        struct adapter_group *group = &ADAPTER_GROUP;
        unsigned nparams;

        if (BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE)
            nparams = group->noutputs;
        else
            nparams = 1;

        hr = present_create_present_group(This->gdi_display, hFocusWindow,
                pPresentationParameters, pFullscreenDisplayMode, nparams,
                &present, This->ex, BehaviorFlags, group->dri_backend);
    }

    if (FAILED(hr))
    {
        WARN("Failed to create PresentGroup.\n");
        return hr;
    }

    if (This->ex)
    {
        hr = ADAPTER_PROC(CreateDeviceEx, Adapter, DeviceType, hFocusWindow,
                BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode,
                (IDirect3D9Ex *)This, present, ppReturnedDeviceInterface);
    }
    else
    {
        /* CreateDevice on non-ex */
        hr = ADAPTER_PROC(CreateDevice, Adapter, DeviceType, hFocusWindow,
                BehaviorFlags, pPresentationParameters, (IDirect3D9 *)This, present,
                (IDirect3DDevice9 **)ppReturnedDeviceInterface);
    }
    if (FAILED(hr))
    {
        WARN("ADAPTER_PROC failed.\n");
        ID3DPresentGroup_Release(present);
        return hr;
    }

    return hr;
}

static HRESULT WINAPI d3dadapter9_GetAdapterLUID(struct d3dadapter9 *This,
        UINT Adapter, LUID *pLUID)
{
    FIXME("(%p, %u, %p), stub!\n", This, Adapter, pLUID);
    return D3DERR_INVALIDCALL;
}

static struct adapter_group *add_group(struct d3dadapter9 *This)
{
    if (This->ngroups >= This->ngroupsalloc)
    {
        void *r;

        if (This->ngroupsalloc == 0)
        {
            This->ngroupsalloc = 2;
            r = calloc(This->ngroupsalloc, sizeof(struct adapter_group));
        }
        else
        {
            This->ngroupsalloc <<= 1;
            r = realloc(This->groups, This->ngroupsalloc*sizeof(struct adapter_group));
        }

        if (!r)
            return NULL;
        This->groups = r;
    }

    return &This->groups[This->ngroups++];
}

static void remove_group(struct d3dadapter9 *This)
{
    struct adapter_group *group = &This->groups[This->ngroups-1];
    int i;

    for (i = 0; i < group->noutputs; ++i)
    {
        free(group->outputs[i].modes);
    }
    free(group->outputs);

    backend_destroy(group->dri_backend);

    ZeroMemory(group, sizeof(struct adapter_group));
    This->ngroups--;
}

static struct output *add_output(struct d3dadapter9 *This)
{
    struct adapter_group *group = &This->groups[This->ngroups-1];

    if (group->noutputs >= group->noutputsalloc)
    {
        void *r;

        if (group->noutputsalloc == 0)
        {
            group->noutputsalloc = 2;
            r = calloc(group->noutputsalloc, sizeof(struct output));
        }
        else
        {
            group->noutputsalloc <<= 1;
            r = realloc(group->outputs, group->noutputsalloc*sizeof(struct output));
        }

        if (!r)
            return NULL;
        group->outputs = r;
    }

    return &group->outputs[group->noutputs++];
}

static void remove_output(struct d3dadapter9 *This)
{
    struct adapter_group *group = &This->groups[This->ngroups-1];
    struct output *out = &group->outputs[group->noutputs-1];

    free(out->modes);

    ZeroMemory(out, sizeof(struct output));
    group->noutputs--;
}

static SDL_DisplayMode *add_mode(struct d3dadapter9 *This)
{
    struct adapter_group *group = &This->groups[This->ngroups-1];
    struct output *out = &group->outputs[group->noutputs-1];

    if (out->nmodes >= out->nmodesalloc)
    {
        void *r;

        if (out->nmodesalloc == 0)
        {
            out->nmodesalloc = 8;
            r = calloc(out->nmodesalloc, sizeof(struct D3DDISPLAYMODEEX));
        }
        else
        {
            out->nmodesalloc <<= 1;
            r = realloc(out->modes, out->nmodesalloc*sizeof(struct D3DDISPLAYMODEEX));
        }

        if (!r)
            return NULL;
        out->modes = r;
    }

    return &out->modes[out->nmodes++];
}

static void remove_mode(struct d3dadapter9 *This)
{
    struct adapter_group *group = &This->groups[This->ngroups-1];
    struct output *out = &group->outputs[group->noutputs-1];
    out->nmodes--;
}

static HRESULT fill_groups(struct d3dadapter9 *This)
{
    HRESULT hr;
    int i, j, k;

    // TODO: Multiple adapters
    for (i = 0; i < 1; ++i)
    {
        struct adapter_group *group = add_group(This);
        if (!group)
        {
            ERR("Out of memory.\n");
            return E_OUTOFMEMORY;
        }

        group->dri_backend = backend_create(This->gdi_display, DefaultScreen(This->gdi_display));
        if (!group->dri_backend)
        {
            ERR("Unable to open backend for display %d.\n", i);
            continue;
        }

        hr = present_create_adapter9(group->dri_backend, &group->adapter);
        if (FAILED(hr))
        {
            remove_group(This);
            continue;
        }

        for (j = 0; j < SDL_GetNumVideoDisplays(); ++j)
        {
            struct output *out = add_output(This);
            if (!out)
            {
                ERR("Out of memory.\n");
                return E_OUTOFMEMORY;
            }

            for (k = 0; k < SDL_GetNumDisplayModes(j); ++k)
            {
                SDL_DisplayMode *mode = add_mode(This);
                if (!out)
                {
                    ERR("Out of memory.\n");
                    return E_OUTOFMEMORY;
                }

                if (SDL_GetDisplayMode(j, k, mode) < 0)
                {
                    remove_output(This);
                    WARN("Unable to get display mode for display %d, mode %d.\n", j, k);
                }

                TRACE("format=%s, w=%d, h=%d, refresh_rate=%d\n",
                        SDL_GetPixelFormatName(mode->format), mode->w, mode->h, mode->refresh_rate);
            }
        }
    }

    return D3D_OK;
}

static IDirect3D9ExVtbl d3dadapter9_vtable = {
    (void *)d3dadapter9_QueryInterface,
    (void *)d3dadapter9_AddRef,
    (void *)d3dadapter9_Release,
    (void *)d3dadapter9_RegisterSoftwareDevice,
    (void *)d3dadapter9_GetAdapterCount,
    (void *)d3dadapter9_GetAdapterIdentifier,
    (void *)d3dadapter9_GetAdapterModeCount,
    (void *)d3dadapter9_EnumAdapterModes,
    (void *)d3dadapter9_GetAdapterDisplayMode,
    (void *)d3dadapter9_CheckDeviceType,
    (void *)d3dadapter9_CheckDeviceFormat,
    (void *)d3dadapter9_CheckDeviceMultiSampleType,
    (void *)d3dadapter9_CheckDepthStencilMatch,
    (void *)d3dadapter9_CheckDeviceFormatConversion,
    (void *)d3dadapter9_GetDeviceCaps,
    (void *)d3dadapter9_GetAdapterMonitor,
    (void *)d3dadapter9_CreateDevice,
    (void *)d3dadapter9_GetAdapterModeCountEx,
    (void *)d3dadapter9_EnumAdapterModesEx,
    (void *)d3dadapter9_GetAdapterDisplayModeEx,
    (void *)d3dadapter9_CreateDeviceEx,
    (void *)d3dadapter9_GetAdapterLUID
};

HRESULT d3dadapter9_new(Display *gdi_display, BOOL ex, IDirect3D9Ex **ppOut)
{
    struct d3dadapter9 *This;
    HRESULT hr;
    unsigned i, j, k;

    This = calloc(1, sizeof(struct d3dadapter9));
    if (!This)
    {
        ERR("Out of memory.\n");
        return E_OUTOFMEMORY;
    }

    This->vtable = &d3dadapter9_vtable;
    This->refs = 1;
    This->ex = ex;
    This->gdi_display = gdi_display;

    if (!present_has_d3dadapter(gdi_display))
    {
        ERR("Your display driver doesn't support native D3D9 adapters.\n");
        d3dadapter9_Release(This);
        return D3DERR_NOTAVAILABLE;
    }

    if (FAILED(hr = fill_groups(This)))
    {
        d3dadapter9_Release(This);
        return hr;
    }

    /* map absolute adapter IDs with internal adapters */
    for (i = 0; i < This->ngroups; ++i)
    {
        for (j = 0; j < This->groups[i].noutputs; ++j)
        {
            This->nadapters++;
        }
    }
    if (This->nadapters == 0)
    {
        ERR("No available native adapters in system.\n");
        d3dadapter9_Release(This);
        return D3DERR_NOTAVAILABLE;
    }

    This->map = calloc(This->nadapters, sizeof(struct adapter_map));

    if (!This->map)
    {
        d3dadapter9_Release(This);
        ERR("Out of memory.\n");
        return E_OUTOFMEMORY;
    }
    for (i = k = 0; i < This->ngroups; ++i)
    {
        for (j = 0; j < This->groups[i].noutputs; ++j, ++k)
        {
            This->map[k].master = k-j;
            This->map[k].group = i;
        }
    }

    *ppOut = (IDirect3D9Ex *)This;

    return D3D_OK;
}
