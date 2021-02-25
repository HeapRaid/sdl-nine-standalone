/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Wine present interface
 *
 * Copyright 2015 Patrick Rudolph
 */

#ifndef __NINE_PRESENT_H
#define __NINE_PRESENT_H

#include <d3dadapter/present.h>
#include <X11/Xlib.h>

struct dri_backend;
typedef enum _D3DFORMAT D3DFORMAT;

HRESULT present_create_present_group(Display *gdi_display, HWND focus,
        D3DPRESENT_PARAMETERS *params, D3DDISPLAYMODEEX *pFullscreenDisplayMode,
        unsigned nparams, ID3DPresentGroup **group, BOOL ex, DWORD BehaviorFlags,
        struct dri_backend *dri_backend);

HRESULT present_create_adapter9(struct dri_backend *dri_backend, ID3DAdapter9 **adapter);

BOOL present_has_d3dadapter(Display *gdi_display);

D3DFORMAT to_d3d_format(DWORD sdl_format);

#endif /* __NINE_PRESENT_H */
