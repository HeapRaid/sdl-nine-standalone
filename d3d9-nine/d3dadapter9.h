/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * D3DAdapter9 interface
 *
 * Copyright 2015 Patrick Rudolph
 */

#ifndef __NINE_D3D9ADAPTER_H
#define __NINE_D3D9ADAPTER_H

#include <d3d9.h>
#include <X11/Xlib.h>

HRESULT d3dadapter9_new(Display *gdi_display, BOOL ex, IDirect3D9Ex **ppOut);

#endif /* __NINE_D3D9ADAPTER_H */
