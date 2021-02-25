
#ifndef __NINE_D3D9_SDL_H
#define __NINE_D3D9_SDL_H

#include <d3d9.h>

#define D3DCREATE_NOWINDOWCHANGES 0x00000800

#ifdef __cplusplus
extern "C" {
#endif

IDirect3D9 *WINAPI
Direct3DCreate9( UINT sdk_version );

HRESULT WINAPI
Direct3DCreate9Ex( UINT sdk_version,
                   IDirect3D9Ex **ppD3D9 );

void *WINAPI
Direct3DShaderValidatorCreate9( void );

int WINAPI
D3DPERF_BeginEvent( D3DCOLOR color,
                    const wchar_t* name );

int WINAPI
D3DPERF_EndEvent( void );

DWORD WINAPI
D3DPERF_GetStatus( void );

void WINAPI
D3DPERF_SetOptions( DWORD options );

BOOL WINAPI
D3DPERF_QueryRepeatFrame( void );

void WINAPI
D3DPERF_SetMarker( D3DCOLOR color,
                   const wchar_t* name );

void WINAPI
D3DPERF_SetRegion( D3DCOLOR color,
                   const wchar_t* name );

void WINAPI
DebugSetMute( void );

#ifdef __cplusplus
};
#endif

#endif /* __NINE_D3D9_SDL_H */
