/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Direct3D 9
 *
 * Copyright 2002-2003 Jason Edmeades
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2005 Oliver Stieber
 * Copyright 2015 Patrick Rudolph
 */

#include <d3d9.h>
#include <fcntl.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

#include "../common/debug.h"
#include "d3dadapter9.h"
#include "shader_validator.h"

static int D3DPERF_event_level = 0;

void WINAPI DebugSetMute(void)
{
    /* nothing to do */
}

IDirect3D9 * WINAPI Direct3DCreate9(UINT sdk_version)
{
    IDirect3D9 *native;
    Display* gdi_display;
    TRACE("sdk_version %#x.\n", sdk_version);

    if (!SDL_WasInit(SDL_INIT_VIDEO))
    {
        ERR("Initialize SDL with the video subsystem before D3D9.\n");
        return NULL;
    }

    if (!(gdi_display = XOpenDisplay( NULL )))
    {
        ERR("Failed to open display.\n");
        return NULL;
    }

    if (SUCCEEDED(d3dadapter9_new(gdi_display, FALSE, (IDirect3D9Ex **)&native)))
        return native;

    return NULL;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT sdk_version, IDirect3D9Ex **d3d9ex)
{
    Display* gdi_display;
    TRACE("sdk_version %#x, d3d9ex %p.\n", sdk_version, d3d9ex);

    if (!SDL_WasInit(SDL_INIT_VIDEO))
    {
        ERR("Initialize SDL with the video subsystem before D3D9.\n");
        return D3DERR_INVALIDCALL;
    }

    if (!(gdi_display = XOpenDisplay( NULL )))
    {
        ERR("Failed to open display\n");
        return D3DERR_INVALIDDEVICE;
    }
    return d3dadapter9_new(gdi_display, TRUE, d3d9ex);
}

/*******************************************************************
 *       Direct3DShaderValidatorCreate9 (D3D9.@)
 *
 * No documentation available for this function.
 * SDK only says it is internal and shouldn't be used.
 */

void* WINAPI Direct3DShaderValidatorCreate9(void)
{
    IDirect3DShaderValidator9Impl* object =
            calloc(1, sizeof(IDirect3DShaderValidator9Impl));

    object->lpVtbl = &IDirect3DShaderValidator9Vtbl;
    object->ref = 1;

    TRACE("Returning interface %p\n", object);
    return (void*) object;
}

/***********************************************************************
 *              D3DPERF_BeginEvent (D3D9.@)
 */
int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, const wchar_t *name)
{
    TRACE("color 0x%08x, name %ls.\n", color, name);

    return D3DPERF_event_level++;
}

/***********************************************************************
 *              D3DPERF_EndEvent (D3D9.@)
 */
int WINAPI D3DPERF_EndEvent(void)
{
    TRACE("(void) : stub\n");

    return --D3DPERF_event_level;
}

/***********************************************************************
 *              D3DPERF_GetStatus (D3D9.@)
 */
DWORD WINAPI D3DPERF_GetStatus(void)
{
    FIXME("(void) : stub\n");

    return 0;
}

/***********************************************************************
 *              D3DPERF_SetOptions (D3D9.@)
 *
 */
void WINAPI D3DPERF_SetOptions(DWORD options)
{
  FIXME("(%#x) : stub\n", options);
}

/***********************************************************************
 *              D3DPERF_QueryRepeatFrame (D3D9.@)
 */
BOOL WINAPI D3DPERF_QueryRepeatFrame(void)
{
    FIXME("(void) : stub\n");

    return FALSE;
}

/***********************************************************************
 *              D3DPERF_SetMarker (D3D9.@)
 */
void WINAPI D3DPERF_SetMarker(D3DCOLOR color, const wchar_t *name)
{
    FIXME("color 0x%08x, name %ls stub!\n", color, name);
}

/***********************************************************************
 *              D3DPERF_SetRegion (D3D9.@)
 */
void WINAPI D3DPERF_SetRegion(D3DCOLOR color, const wchar_t *name)
{
    FIXME("color 0x%08x, name %ls stub!\n", color, name);
}
