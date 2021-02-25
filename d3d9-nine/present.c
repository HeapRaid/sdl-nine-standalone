/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Wine ID3DAdapter9 support functions
 *
 * Copyright 2013 Joakim Sindholt
 *                Christoph Bumiller
 * Copyright 2014 Tiziano Bacocco
 *                David Heidelberger
 * Copyright 2014-2015 Axel Davy
 * Copyright 2015 Patrick Rudolph
 */

#include <d3dadapter/drm.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include "../common/debug.h"
#include "../common/library.h"
#include "backend.h"
#include "xcb_present.h"

#ifndef D3DPRESENT_DONOTWAIT
#define D3DPRESENT_DONOTWAIT      0x00000001
#endif

#ifndef D3DCREATE_NOWINDOWCHANGES
#define D3DCREATE_NOWINDOWCHANGES 0x00000800
#endif

#define D3DADAPTER_DRIVER_PRESENT_VERSION_MAJOR 1
#if defined (ID3DPresent_SetPresentParameters2)
/* version 1.4 doesn't introduce a new member, but expects
 * SetCursorPosition() calls for every position update
 */
#define D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR 4
#elif defined (ID3DPresent_ResolutionMismatch)
#define D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR 2
#elif defined (ID3DPresent_GetWindowOccluded)
#define D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR 1
#else
#define D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR 0
#endif

static const struct D3DAdapter9DRM *d3d9_drm = NULL;

/* Start section of x11drv.h */
#define X11DRV_ESCAPE 6789
enum x11drv_escape_codes
{
    X11DRV_SET_DRAWABLE,     /* set current drawable for a DC */
    X11DRV_GET_DRAWABLE,     /* get current drawable for a DC */
    X11DRV_START_EXPOSURES,  /* start graphics exposures */
    X11DRV_END_EXPOSURES,    /* end graphics exposures */
    X11DRV_FLUSH_GL_DRAWABLE /* flush changes made to the gl drawable */
};

struct x11drv_escape_get_drawable
{
    enum x11drv_escape_codes code;         /* escape code (X11DRV_GET_DRAWABLE) */
    Drawable                 drawable;     /* X drawable */
    Drawable                 gl_drawable;  /* GL drawable */
    int                      pixel_format; /* internal GL pixel format */
};
/* End section x11drv.h */

const GUID IID_ID3DPresent = { 0x77D60E80, 0xF1E6, 0x11DF, { 0x9E, 0x39, 0x95, 0x0C, 0xDF, 0xD7, 0x20, 0x85 } };
const GUID IID_ID3DPresentGroup = { 0xB9C3016E, 0xF32A, 0x11DF, { 0x9C, 0x18, 0x92, 0xEA, 0xDE, 0xD7, 0x20, 0x85 } };

struct d3d_drawable
{
    Drawable drawable; /* X11 drawable */
    HDC hdc;
    HWND wnd; /* HWND (for convenience) */
    RECT windowRect;
    POINT offset; /* offset of the client area compared to the X11 drawable */
    unsigned int width;
    unsigned int height;
    unsigned int depth;
};

struct DRIPresent
{
    /* COM vtable */
    void *vtable;
    /* IUnknown reference count */
    LONG refs;
    /* Active present version */
    int major, minor;

    D3DPRESENT_PARAMETERS params;
    HWND focus_wnd;
    PRESENTpriv *present_priv;

    SDL_Cursor* hCursor;

    BOOL ex;
    BOOL no_window_changes;

    UINT present_interval;
    BOOL present_async;
    BOOL present_swapeffectcopy;
    BOOL allow_discard_delayed_release;
    BOOL tear_free_discard;

    struct dri_backend *dri_backend;
};

struct DRIPresentGroup
{
    /* COM vtable */
    void *vtable;
    /* IUnknown reference count */
    LONG refs;
    /* Active present version */
    int major, minor;

    BOOL ex;
    BOOL no_window_changes;
    struct DRIPresent **present_backends;
    unsigned npresent_backends;
    Display *gdi_display;
    struct dri_backend *dri_backend;
};

static SDL_PixelFormatEnum to_sdl_format(D3DFORMAT d3d_format)
{
    switch(d3d_format)
    {
        default: WARN("No matching SDL pixel format for format %d\n", d3d_format);
        case D3DFMT_UNKNOWN:    return SDL_PIXELFORMAT_UNKNOWN;
        case D3DFMT_R3G3B2:     return SDL_PIXELFORMAT_RGB332;
        case D3DFMT_X4R4G4B4:   return SDL_PIXELFORMAT_RGB444;
        case D3DFMT_X1R5G5B5:   return SDL_PIXELFORMAT_RGB555;
        case D3DFMT_A4R4G4B4:   return SDL_PIXELFORMAT_ARGB4444;
        case D3DFMT_A1R5G5B5:   return SDL_PIXELFORMAT_ABGR1555;
        case D3DFMT_R5G6B5:     return SDL_PIXELFORMAT_RGB565;
        case D3DFMT_R8G8B8:     return SDL_PIXELFORMAT_RGB888;
        case D3DFMT_X8R8G8B8:   return SDL_PIXELFORMAT_RGBX8888;
        case D3DFMT_X8B8G8R8:   return SDL_PIXELFORMAT_BGRX8888;
        case D3DFMT_A8R8G8B8:   return SDL_PIXELFORMAT_ARGB8888;
        case D3DFMT_A8B8G8R8:   return SDL_PIXELFORMAT_ABGR8888;
        case D3DFMT_A2R10G10B10:return SDL_PIXELFORMAT_ARGB2101010;
        case D3DFMT_UYVY:       return SDL_PIXELFORMAT_UYVY;
        case D3DFMT_YUY2:       return SDL_PIXELFORMAT_YUY2;
        case D3DFMT_NV12:       return SDL_PIXELFORMAT_NV12;
    }
}

D3DFORMAT to_d3d_format(DWORD sdl_format)
{
    switch(sdl_format)
    {
        default: WARN("No matching D3D pixel format for format %s\n", SDL_GetPixelFormatName(sdl_format));
        case SDL_PIXELFORMAT_UNKNOWN:     return D3DFMT_UNKNOWN;
        case SDL_PIXELFORMAT_RGB332:      return D3DFMT_R3G3B2;
        case SDL_PIXELFORMAT_RGB444:      return D3DFMT_X4R4G4B4;
        case SDL_PIXELFORMAT_RGB555:      return D3DFMT_X1R5G5B5;
        case SDL_PIXELFORMAT_ARGB4444:    return D3DFMT_A4R4G4B4;
        case SDL_PIXELFORMAT_ABGR1555:    return D3DFMT_A1R5G5B5;
        case SDL_PIXELFORMAT_RGB565:      return D3DFMT_R5G6B5;
        case SDL_PIXELFORMAT_RGB888:      return D3DFMT_R8G8B8;
        case SDL_PIXELFORMAT_RGBX8888:    return D3DFMT_X8R8G8B8;
        case SDL_PIXELFORMAT_BGRX8888:    return D3DFMT_X8B8G8R8;
        case SDL_PIXELFORMAT_ARGB8888:    return D3DFMT_A8R8G8B8;
        case SDL_PIXELFORMAT_ABGR8888:    return D3DFMT_A8B8G8R8;
        case SDL_PIXELFORMAT_ARGB2101010: return D3DFMT_A2R10G10B10;
        case SDL_PIXELFORMAT_UYVY:        return D3DFMT_UYVY;
        case SDL_PIXELFORMAT_YUY2:        return D3DFMT_YUY2;
        case SDL_PIXELFORMAT_NV12:        return D3DFMT_NV12;
    }
}

static void update_presentation_interval(struct DRIPresent *This)
{
    switch(This->params.PresentationInterval)
    {
        case D3DPRESENT_INTERVAL_DEFAULT:
        case D3DPRESENT_INTERVAL_ONE:
            This->present_interval = 1;
            This->present_async = FALSE;
            break;
        case D3DPRESENT_INTERVAL_TWO:
            This->present_interval = 2;
            This->present_async = FALSE;
            break;
        case D3DPRESENT_INTERVAL_THREE:
            This->present_interval = 3;
            This->present_async = FALSE;
            break;
        case D3DPRESENT_INTERVAL_FOUR:
            This->present_interval = 4;
            This->present_async = FALSE;
            break;
        case D3DPRESENT_INTERVAL_IMMEDIATE:
        default:
            This->present_interval = 0;
            This->present_async =
                !(This->params.SwapEffect == D3DSWAPEFFECT_DISCARD &&
                  This->tear_free_discard);
            break;
    }

    /* D3DSWAPEFFECT_COPY: Force Copy.
     * This->present_interval == 0: Force Copy to have buffers
     * release as soon as possible (the display server/compositor
     * won't hold any buffer), unless DISCARD and
     * allow_discard_delayed_release */
    This->present_swapeffectcopy =
        This->params.SwapEffect == D3DSWAPEFFECT_COPY ||
        (This->present_interval == 0 &&
        !(This->params.SwapEffect == D3DSWAPEFFECT_DISCARD &&
          This->allow_discard_delayed_release));
}

/* ID3DPresentVtbl */

static ULONG WINAPI DRIPresent_AddRef(struct DRIPresent *This)
{
    ULONG refs = InterlockedIncrement(&This->refs);
    TRACE("%p increasing refcount to %u.\n", This, refs);
    return refs;
}

static ULONG WINAPI DRIPresent_Release(struct DRIPresent *This)
{
    ULONG refs = InterlockedDecrement(&This->refs);
    TRACE("%p decreasing refcount to %u.\n", This, refs);
    if (refs == 0)
    {
        /* dtor */
        SDL_SetWindowFullscreen(This->params.hDeviceWindow, 0);
        SDL_FreeCursor(This->hCursor);
        PRESENTDestroy(This->present_priv);
        This->dri_backend->funcs->deinit(This->dri_backend->priv);
        free(This);
    }
    return refs;
}

static HRESULT WINAPI DRIPresent_QueryInterface(struct DRIPresent *This,
        REFIID riid, void **ppvObject)
{
    if (!ppvObject)
        return E_POINTER;

    if (IsEqualGUID(&IID_ID3DPresent, riid) ||
            IsEqualGUID(&IID_IUnknown, riid))
    {
        *ppvObject = This;
        DRIPresent_AddRef(This);
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", nine_dbgstr_guid(riid));
    *ppvObject = NULL;

    return E_NOINTERFACE;
}

static HRESULT WINAPI DRIPresent_SetPresentParameters(struct DRIPresent *This,
        D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
    int w, h;

    TRACE("This=%p, params=%p, focus_window=%p, params->hDeviceWindow=%p\n",
          This, params, This->focus_wnd, params->hDeviceWindow);

    This->params.SwapEffect = params->SwapEffect;
    This->params.AutoDepthStencilFormat = params->AutoDepthStencilFormat;
    This->params.Flags = params->Flags;
    This->params.FullScreen_RefreshRateInHz = params->FullScreen_RefreshRateInHz;
    This->params.PresentationInterval = params->PresentationInterval;
    This->params.EnableAutoDepthStencil = params->EnableAutoDepthStencil;
    if (!params->hDeviceWindow)
        params->hDeviceWindow = This->params.hDeviceWindow;
    else
        This->params.hDeviceWindow = params->hDeviceWindow;

    if (pFullscreenDisplayMode)
    {
        SDL_DisplayMode mode = {
            to_sdl_format(pFullscreenDisplayMode->Format),
            pFullscreenDisplayMode->Width,
            pFullscreenDisplayMode->Height,
            pFullscreenDisplayMode->RefreshRate
        };
        
        if (SDL_SetWindowDisplayMode(params->hDeviceWindow, &mode) < 0)
        {
            ERR("Failed to set window display mode with error %s\n", SDL_GetError());
            return D3DERR_DRIVERINTERNALERROR;
        }
    }
    else if (!This->ex)
    {
        SDL_DisplayMode mode = {
            to_sdl_format(params->BackBufferFormat),
            params->BackBufferWidth,
            params->BackBufferHeight,
            params->FullScreen_RefreshRateInHz
        };
        
        if (SDL_SetWindowDisplayMode(params->hDeviceWindow, &mode) < 0)
        {
            ERR("Failed to set window display mode with error %s\n", SDL_GetError());
            return D3DERR_DRIVERINTERNALERROR;
        }
    }

    if (SDL_SetWindowFullscreen(params->hDeviceWindow, params->Windowed ? 0 : SDL_WINDOW_FULLSCREEN) < 0)
    {
        ERR("Failed to switch window to fullscreen with error %s\n", SDL_GetError());
        return D3DERR_DRIVERINTERNALERROR;
    }

    if (!params->BackBufferWidth || !params->BackBufferHeight) {
        if (!params->Windowed)
            return D3DERR_INVALIDCALL;

        SDL_GetWindowSize(params->hDeviceWindow, &w, &h);

        if (params->BackBufferWidth == 0)
            params->BackBufferWidth = w;

        if (params->BackBufferHeight == 0)
            params->BackBufferHeight = h;
    }
    else if (params->Windowed && !This->no_window_changes)
    {
        SDL_SetWindowSize(params->hDeviceWindow, params->BackBufferWidth, params->BackBufferHeight);
    }

    /* Set as last in case of failed reset those aren't updated */
    This->params = *params;

    update_presentation_interval(This);

    if (!params->Windowed)
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (!SDL_GetWindowWMInfo(params->hDeviceWindow, &wm))
        {
            WARN("Failed to get SDL wm info with error %s", SDL_GetError());
            return D3DERR_DRIVERINTERNALERROR;
        }
        
        if (wm.subsystem != SDL_SYSWM_X11)
            return D3D_OK;

        Atom _NET_WM_BYPASS_COMPOSITOR = XInternAtom(wm.info.x11.display,
                                                     "_NET_WM_BYPASS_COMPOSITOR",
                                                     False);

        Atom _VARIABLE_REFRESH = XInternAtom(wm.info.x11.display,
                                             "_VARIABLE_REFRESH",
                                             False);

        /* Disable compositing for fullscreen windows */
        int bypass_value = 1;
        XChangeProperty(wm.info.x11.display, wm.info.x11.window,
                        _NET_WM_BYPASS_COMPOSITOR, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&bypass_value, 1);

        /* Enable variable sync */
        int vrr_value = 1;
        XChangeProperty(wm.info.x11.display, wm.info.x11.window,
                        _VARIABLE_REFRESH, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)&vrr_value, 1);
    }

    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_D3DWindowBufferFromDmaBuf(struct DRIPresent *This,
        int dmaBufFd, int width, int height, int stride, int depth,
        int bpp, struct D3DWindowBuffer **out)
{
    const struct dri_backend *dri_backend = This->dri_backend;

    if (!dri_backend->funcs->window_buffer_from_dmabuf(dri_backend->priv,
            This->present_priv, dmaBufFd, width, height, stride, depth, bpp, out))
    {
        ERR("window_buffer_from_dmabuf failed\n");
        return D3DERR_DRIVERINTERNALERROR;
    }

    //TRACE("This=%p buffer=%p\n", This, *out);
    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_DestroyD3DWindowBuffer(struct DRIPresent *This,
        struct D3DWindowBuffer *buffer)
{
    const struct dri_backend *dri_backend = This->dri_backend;

    /* the pixmap is managed by the PRESENT backend.
     * But if it can delete it right away, we may have
     * better performance */
    //TRACE("This=%p buffer=%p of priv %p\n", This, buffer, buffer->present_pixmap_priv);
    PRESENTTryFreePixmap(buffer->present_pixmap_priv);
    dri_backend->funcs->destroy_pixmap(dri_backend->priv, buffer->priv);
    free(buffer);
    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_WaitBufferReleased(struct DRIPresent *This,
        struct D3DWindowBuffer *buffer)
{
    //TRACE("This=%p buffer=%p\n", This, buffer);
    if(!PRESENTWaitPixmapReleased(buffer->present_pixmap_priv))
    {
        ERR("PRESENTWaitPixmapReleased failed\n");
        return D3DERR_DRIVERINTERNALERROR;
    }
    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_FrontBufferCopy(struct DRIPresent *This,
        struct D3DWindowBuffer *buffer)
{
    const struct dri_backend *dri_backend = This->dri_backend;

    if (!dri_backend->funcs->copy_front(buffer->present_pixmap_priv))
        return D3DERR_DRIVERINTERNALERROR;

    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_PresentBuffer( struct DRIPresent *This,
        struct D3DWindowBuffer *buffer, HWND hWndOverride, const RECT *pSourceRect,
        const RECT *pDestRect, const RGNDATA *pDirtyRegion, DWORD Flags )
{
    const struct dri_backend *dri_backend = This->dri_backend;
    HWND hwnd;
    SDL_SysWMinfo wm;

    if (hWndOverride)
        hwnd = hWndOverride;
    else if (This->params.hDeviceWindow)
        hwnd = This->params.hDeviceWindow;
    else
        hwnd = This->focus_wnd;

    //TRACE("This=%p hwnd=%p\n", This, hwnd);

    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(hwnd, &wm))
        return D3DERR_DRIVERINTERNALERROR;

    if (!PRESENTPixmapPrepare(wm.info.x11.window, buffer->present_pixmap_priv))
    {
        ERR("PresentPrepare call failed\n");
        return D3DERR_DRIVERINTERNALERROR;
    }

    /* FIMXE: Do we need to aquire present mutex here? */
    dri_backend->funcs->present_pixmap(dri_backend->priv, buffer->priv);

    if (!PRESENTPixmap(wm.info.x11.window, buffer->present_pixmap_priv,
            This->present_interval, This->present_async, This->present_swapeffectcopy,
            pSourceRect, pDestRect, pDirtyRegion))
    {
        TRACE("Present call failed\n");
        return D3DERR_DRIVERINTERNALERROR;
    }

    return D3D_OK;
}

/* Based on wine's wined3d_get_adapter_raster_status. */
static HRESULT WINAPI DRIPresent_GetRasterStatus( struct DRIPresent *This,
        D3DRASTER_STATUS *pRasterStatus )
{
    Uint64 freq_per_frame, freq_per_line, counter, freq_per_sec;

    TRACE("This=%p, pRasterStatus=%p\n", This, pRasterStatus);

    counter = SDL_GetPerformanceCounter();
    freq_per_sec = SDL_GetPerformanceFrequency();

    SDL_DisplayMode dm;
    ZeroMemory(&dm, sizeof(dm));
    if (SDL_GetWindowDisplayMode(This->params.hDeviceWindow, &dm) < 0)
        return D3DERR_INVALIDCALL;

    if (dm.refresh_rate == 0)
        dm.refresh_rate = 60;

    TRACE("refresh_rate=%u, height=%u\n", dm.refresh_rate, dm.h);

    freq_per_frame = freq_per_sec / dm.refresh_rate;
    /* Assume 20 scan lines in the vertical blank. */
    freq_per_line = freq_per_frame / (dm.h + 20);
    pRasterStatus->ScanLine = (counter % freq_per_frame) / freq_per_line;
    if (pRasterStatus->ScanLine < dm.h)
        pRasterStatus->InVBlank = FALSE;
    else
    {
        pRasterStatus->ScanLine = 0;
        pRasterStatus->InVBlank = TRUE;
    }

    TRACE("Returning fake value, InVBlank %u, ScanLine %u.\n",
          pRasterStatus->InVBlank, pRasterStatus->ScanLine);

    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_GetDisplayMode( struct DRIPresent *This,
        D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation )
{
    SDL_DisplayMode dm;

    ZeroMemory(&dm, sizeof(dm));

    SDL_GetWindowDisplayMode(This->params.hDeviceWindow, &dm);
    pMode->Width = dm.w;
    pMode->Height = dm.h;
    pMode->RefreshRate = dm.refresh_rate;
    pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    pMode->Format = to_d3d_format(dm.format);

    *pRotation = D3DDISPLAYROTATION_IDENTITY;
    return D3D_OK;
}

static HRESULT WINAPI DRIPresent_GetPresentStats( struct DRIPresent *This, D3DPRESENTSTATS *pStats )
{
    FIXME("(%p, %p), stub!\n", This, pStats);
    return D3DERR_INVALIDCALL;
}

static HRESULT WINAPI DRIPresent_GetCursorPos( struct DRIPresent *This, POINT *pPoint )
{
    BOOL ok;
    HWND draw_window;

    if (!pPoint)
        return D3DERR_INVALIDCALL;

    draw_window = This->params.hDeviceWindow ?
            This->params.hDeviceWindow : This->focus_wnd;

    ok = SDL_GetGlobalMouseState(&pPoint->x, &pPoint->y) >= 0;
    return ok ? S_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI DRIPresent_SetCursorPos( struct DRIPresent *This, POINT *pPoint )
{
    BOOL ok;
    POINT real_pos;

    if (!pPoint)
        return D3DERR_INVALIDCALL;

    /* starting with present v1.4 we check against proper values ourselves */
    if (This->minor > 3)
    {
        SDL_GetGlobalMouseState(&real_pos.x, &real_pos.y);
        if (real_pos.x == pPoint->x && real_pos.y == pPoint->y)
            return D3D_OK;
    }

    ok = SDL_WarpMouseGlobal(pPoint->x, pPoint->y) >= 0;
    if (!ok)
        goto error;

    ok = SDL_GetGlobalMouseState(&real_pos.x, &real_pos.y) >= 0;
    if (!ok || real_pos.x != pPoint->x || real_pos.y != pPoint->y)
        goto error;

    return D3D_OK;

error:
    SDL_ShowCursor(SDL_DISABLE); /* Hide cursor rather than put wrong pos */
    return D3DERR_DRIVERINTERNALERROR;
}

/* Note: assuming 32x32 cursor */
static HRESULT WINAPI DRIPresent_SetCursor( struct DRIPresent *This, void *pBitmap,
        POINT *pHotspot, BOOL bShow )
{
    if (pBitmap)
    {
        SDL_Cursor* cursor;
        SDL_Surface* surface;

        if (!pHotspot)
            return D3DERR_INVALIDCALL;

        surface = SDL_CreateRGBSurfaceWithFormatFrom(pBitmap, 32, 32, 32, 4 * 32,
                                                     SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return D3DERR_DRIVERINTERNALERROR;
        
        cursor = SDL_CreateColorCursor(surface, pHotspot->x, pHotspot->y);
        if (cursor)
        {
            SDL_FreeCursor(This->hCursor);
            This->hCursor = cursor;
        }
        SDL_FreeSurface(surface);
    }
    SDL_SetCursor(This->hCursor);
    SDL_ShowCursor(bShow ? SDL_ENABLE : SDL_DISABLE);

   return D3D_OK;
}

static HRESULT WINAPI DRIPresent_SetGammaRamp( struct DRIPresent *This,
        const D3DGAMMARAMP *pRamp, HWND hWndOverride )
{
    HWND draw_window = This->params.hDeviceWindow ?
        This->params.hDeviceWindow : This->focus_wnd;
    HWND hWnd = hWndOverride ? hWndOverride : draw_window;
    BOOL ok;
    if (!pRamp)
        return D3DERR_INVALIDCALL;

    ok = SDL_SetWindowGammaRamp(hWnd, pRamp->red, pRamp->green, pRamp->blue) >= 0;
    return ok ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI DRIPresent_GetWindowInfo( struct DRIPresent *This,
        HWND hWnd, int *width, int *height, int *depth )
{
    HWND draw_window = This->params.hDeviceWindow ?
        This->params.hDeviceWindow : This->focus_wnd;

    if (!hWnd)
        hWnd = draw_window;
    SDL_GetWindowSize(hWnd, width, height);
    *depth = 24; //TODO
    return D3D_OK;
}

#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 1
static BOOL WINAPI DRIPresent_GetWindowOccluded(struct DRIPresent *This)
{
    return This->focus_wnd && SDL_GetWindowFlags(This->focus_wnd) & SDL_WINDOW_MINIMIZED;
}
#endif

#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 2
static BOOL WINAPI DRIPresent_ResolutionMismatch(struct DRIPresent *This)
{
    /* The resolution might change due to a third party app.
     * Poll this function to get the device's resolution match.
     * A device reset is required to restore the requested resolution.
     */
    if (This->ex || This->params.Windowed || !This->params.hDeviceWindow)
        return FALSE;
    
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(This->params.hDeviceWindow), &mode) < 0)
        return FALSE;
    return mode.w != This->params.BackBufferWidth || mode.h != This->params.BackBufferHeight;
}

static HANDLE WINAPI DRIPresent_CreateThread( struct DRIPresent *This,
        void *pThreadfunc, void *pParam )
{
    return SDL_CreateThread(pThreadfunc, "D3D9 Thread", pParam);
}

static BOOL WINAPI DRIPresent_WaitForThread( struct DRIPresent *This, HANDLE thread )
{
    SDL_WaitThread(thread, NULL);
    return TRUE;
}
#endif

#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 3
static HRESULT WINAPI DRIPresent_SetPresentParameters2( struct DRIPresent *This, D3DPRESENT_PARAMETERS2 *pParams )
{
    This->allow_discard_delayed_release = pParams->AllowDISCARDDelayedRelease;
    This->tear_free_discard = pParams->AllowDISCARDDelayedRelease && pParams->TearFreeDISCARD;
    return D3D_OK;
}

static BOOL WINAPI DRIPresent_IsBufferReleased( struct DRIPresent *This, struct D3DWindowBuffer *buffer )
{
    //TRACE("This=%p buffer=%p\n", This, buffer);
    return PRESENTIsPixmapReleased(buffer->present_pixmap_priv);
}

static HRESULT WINAPI DRIPresent_WaitBufferReleaseEvent( struct DRIPresent *This )
{
    PRESENTWaitReleaseEvent(This->present_priv);
    return D3D_OK;
}
#endif

static ID3DPresentVtbl DRIPresent_vtable = {
    (void *)DRIPresent_QueryInterface,
    (void *)DRIPresent_AddRef,
    (void *)DRIPresent_Release,
    (void *)DRIPresent_SetPresentParameters,
    (void *)DRIPresent_D3DWindowBufferFromDmaBuf,
    (void *)DRIPresent_DestroyD3DWindowBuffer,
    (void *)DRIPresent_WaitBufferReleased,
    (void *)DRIPresent_FrontBufferCopy,
    (void *)DRIPresent_PresentBuffer,
    (void *)DRIPresent_GetRasterStatus,
    (void *)DRIPresent_GetDisplayMode,
    (void *)DRIPresent_GetPresentStats,
    (void *)DRIPresent_GetCursorPos,
    (void *)DRIPresent_SetCursorPos,
    (void *)DRIPresent_SetCursor,
    (void *)DRIPresent_SetGammaRamp,
    (void *)DRIPresent_GetWindowInfo,
#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 1
    (void *)DRIPresent_GetWindowOccluded,
#endif
#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 2
    (void *)DRIPresent_ResolutionMismatch,
    (void *)DRIPresent_CreateThread,
    (void *)DRIPresent_WaitForThread,
#endif
#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 3
    (void *)DRIPresent_SetPresentParameters2,
    (void *)DRIPresent_IsBufferReleased,
    (void *)DRIPresent_WaitBufferReleaseEvent,
#endif
};

static HRESULT present_create(Display *gdi_display, HWND focus_wnd, D3DPRESENT_PARAMETERS *params,
        D3DDISPLAYMODEEX *pFullscreenDisplayMode, struct DRIPresent **out,
        BOOL ex, BOOL no_window_changes, struct dri_backend *dri_backend, int major, int minor)
{
    struct DRIPresent *This;
    HRESULT hr;

    if (!focus_wnd && !params->hDeviceWindow)
    {
        ERR("No focus HWND specified for presentation backend.\n");
        return D3DERR_INVALIDCALL;
    }

    This = calloc(1, sizeof(struct DRIPresent));
    if (!This)
    {
        ERR("Out of memory.\n");
        return E_OUTOFMEMORY;
    }

    This->vtable = &DRIPresent_vtable;
    This->refs = 1;
    This->major = major;
    This->minor = minor;
    This->focus_wnd = focus_wnd;
    This->ex = ex;
    This->no_window_changes = no_window_changes;
    This->dri_backend = dri_backend;

    if (!params->hDeviceWindow)
        params->hDeviceWindow = This->focus_wnd;

    hr = DRIPresent_SetPresentParameters(This, params, pFullscreenDisplayMode);
    if (FAILED(hr))
        return hr;

    if (!PRESENTInit(gdi_display, &(This->present_priv)))
    {
        ERR("Failed to init Present backend\n");
        return D3DERR_DRIVERINTERNALERROR;
    }

    if (!dri_backend->funcs->init(dri_backend->priv))
    {
        free(This);
        return D3DERR_DRIVERINTERNALERROR;
    }

    *out = This;

    return D3D_OK;
}

/* ID3DPresentGroupVtbl */

static ULONG WINAPI DRIPresentGroup_AddRef(struct DRIPresentGroup *This)
{
    ULONG refs = InterlockedIncrement(&This->refs);
    TRACE("%p increasing refcount to %u.\n", This, refs);
    return refs;
}

static ULONG WINAPI DRIPresentGroup_Release(struct DRIPresentGroup *This)
{
    ULONG refs = InterlockedDecrement(&This->refs);
    TRACE("%p decreasing refcount to %u.\n", This, refs);
    if (refs == 0)
    {
        unsigned i;
        if (This->present_backends)
        {
            for (i = 0; i < This->npresent_backends; ++i)
            {
                if (This->present_backends[i])
                    DRIPresent_Release(This->present_backends[i]);
            }
            free(This->present_backends);
        }
        free(This);
    }
    return refs;
}

static HRESULT WINAPI DRIPresentGroup_QueryInterface(struct DRIPresentGroup *This,
        REFIID riid, void **ppvObject )
{
    if (!ppvObject)
        return E_POINTER;
    if (IsEqualGUID(&IID_ID3DPresentGroup, riid) ||
            IsEqualGUID(&IID_IUnknown, riid))
    {
        *ppvObject = This;
        DRIPresentGroup_AddRef(This);
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", nine_dbgstr_guid(riid));
    *ppvObject = NULL;

    return E_NOINTERFACE;
}

static UINT WINAPI DRIPresentGroup_GetMultiheadCount(struct DRIPresentGroup *This)
{
    FIXME("(%p), stub!\n", This);
    return 1;
}

static HRESULT WINAPI DRIPresentGroup_GetPresent(struct DRIPresentGroup *This,
        UINT Index, ID3DPresent **ppPresent)
{
    if (Index >= DRIPresentGroup_GetMultiheadCount(This))
    {
        ERR("Index >= MultiHeadCount\n");
        return D3DERR_INVALIDCALL;
    }
    DRIPresent_AddRef(This->present_backends[Index]);
    *ppPresent = (ID3DPresent *)This->present_backends[Index];

    return D3D_OK;
}

static HRESULT WINAPI DRIPresentGroup_CreateAdditionalPresent(struct DRIPresentGroup *This,
        D3DPRESENT_PARAMETERS *pPresentationParameters, ID3DPresent **ppPresent)
{
    HRESULT hr;
    hr = present_create(This->gdi_display, 0, pPresentationParameters, NULL,
            (struct DRIPresent **)ppPresent, This->ex, This->no_window_changes,
            This->dri_backend, This->major, This->minor);

    return hr;
}

static void WINAPI DRIPresentGroup_GetVersion(struct DRIPresentGroup *This,
        int *major, int *minor)
{
    *major = This->major;
    *minor = This->minor;
}

static ID3DPresentGroupVtbl DRIPresentGroup_vtable = {
    (void *)DRIPresentGroup_QueryInterface,
    (void *)DRIPresentGroup_AddRef,
    (void *)DRIPresentGroup_Release,
    (void *)DRIPresentGroup_GetMultiheadCount,
    (void *)DRIPresentGroup_GetPresent,
    (void *)DRIPresentGroup_CreateAdditionalPresent,
    (void *)DRIPresentGroup_GetVersion
};

HRESULT present_create_present_group(Display *gdi_display, HWND focus_wnd,
        D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *pFullscreenDisplayMode,
        unsigned nparams, ID3DPresentGroup **group, BOOL ex, DWORD BehaviorFlags,
        struct dri_backend *dri_backend)
{
    struct DRIPresentGroup *This;
    HRESULT hr;
    unsigned i;

    This = calloc(1, sizeof(struct DRIPresentGroup));
    if (!This)
    {
        ERR("Out of memory.\n");
        return E_OUTOFMEMORY;
    }

    This->gdi_display = gdi_display;
    This->vtable = &DRIPresentGroup_vtable;
    This->refs = 1;

    This->major = D3DADAPTER_DRIVER_PRESENT_VERSION_MAJOR;
    This->minor = D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR;
    /* present v1.4 requires d3dadapter9 v0.2 */
    if (d3d9_drm->minor_version < 2 && This->minor > 3)
    {
        This->minor = 3;
        TRACE("Limiting present version due to d3dadapter9 v%u.%u\n",
              d3d9_drm->major_version, d3d9_drm->minor_version);
    }
    TRACE("Active present version: v%d.%d\n", This->major, This->minor);

    This->ex = ex;
    This->dri_backend = dri_backend;
    This->npresent_backends = nparams;
    This->no_window_changes = !!(BehaviorFlags & D3DCREATE_NOWINDOWCHANGES);
    This->present_backends = calloc(This->npresent_backends, sizeof(struct DRIPresent *));
    if (!This->present_backends)
    {
        DRIPresentGroup_Release(This);
        ERR("Out of memory.\n");
        return E_OUTOFMEMORY;
    }

    for (i = 0; i < This->npresent_backends; ++i)
    {
        /* create an ID3DPresent for it */
        hr = present_create(gdi_display, focus_wnd, &params[i],
                pFullscreenDisplayMode ? &pFullscreenDisplayMode[i] : NULL,
                &This->present_backends[i], ex, This->no_window_changes,
                This->dri_backend, This->major, This->minor);
        if (FAILED(hr))
        {
            DRIPresentGroup_Release(This);
            return hr;
        }
    }

    *group = (ID3DPresentGroup *)This;
    TRACE("Returning %p\n", *group);

    return D3D_OK;
}

HRESULT present_create_adapter9(struct dri_backend *dri_backend, ID3DAdapter9 **out)
{
    HRESULT hr;
    int fd;

    if (!d3d9_drm)
    {
        ERR("DRM drivers are not supported on your system.\n");
        return D3DERR_DRIVERINTERNALERROR;
    }

    fd = dri_backend->funcs->get_fd(dri_backend->priv);
    if (fd < 0) {
        ERR("Got invalid fd from backend (fd=%d)\n", fd);
        return D3DERR_DRIVERINTERNALERROR;
    }

    hr = d3d9_drm->create_adapter(fd, out);
    if (FAILED(hr))
    {
        ERR("Unable to create ID3DAdapter9 (fd=%d)\n", fd);
        return hr;
    }

    TRACE("Created ID3DAdapter9 with fd %d\n", fd);

    return D3D_OK;
}

BOOL present_has_d3dadapter(Display *gdi_display)
{
    static const void * WINAPI (*pD3DAdapter9GetProc)(const char *);
    static void *handle = NULL;
    static int done = 0;
    char *pathbuf = NULL;

    /* like in opengl.c (single threaded assumption OK?) */
    if (done)
        return handle != NULL;
    done = 1;

    handle = common_load_d3dadapter(&pathbuf, NULL);

    if (!handle)
        goto cleanup;

    /* find our entry point in d3dadapter9 */
    pD3DAdapter9GetProc = dlsym(handle, "D3DAdapter9GetProc");
    if (!pD3DAdapter9GetProc)
    {
        ERR("Failed to get the entry point from %s: %s\n", pathbuf, dlerror());
        goto cleanup;
    }

    /* get a handle to the drm backend struct */
    d3d9_drm = pD3DAdapter9GetProc("drm");
    if (!d3d9_drm)
    {
        ERR("%s doesn't support the drm backend.\n", pathbuf);
        goto cleanup;
    }

    /* verify that we're binary compatible */
    if (d3d9_drm->major_version != 0)
    {
        ERR("Version mismatch. %s has %u.%u, was expecting 0.x\n",
            pathbuf, d3d9_drm->major_version, d3d9_drm->minor_version);
        goto cleanup;
    }

    TRACE("d3dadapter9 version: %u.%u\n",
          d3d9_drm->major_version, d3d9_drm->minor_version);

    if (!PRESENTCheckExtension(gdi_display, 1, 0))
    {
        ERR("Unable to query PRESENT.\n");
        goto cleanup;
    }

    if (!backend_probe(gdi_display))
    {
        ERR("No available backends.\n");
        goto cleanup;
    }

    return TRUE;

cleanup:
    if (handle)
    {
        dlclose(handle);
        handle = NULL;
    }

    free(pathbuf);

    return FALSE;
}
