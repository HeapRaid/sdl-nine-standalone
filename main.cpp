#include <SDL2/SDL.h>
#include <math.h>
#include <d3d9.h>
#include <d3d9_sdl.h>

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

LPDIRECT3D9EX d3d;
LPDIRECT3DDEVICE9EX d3ddev;
LPDIRECT3DVERTEXBUFFER9 v_buffer = NULL;
LPDIRECT3DINDEXBUFFER9 i_buffer = NULL;

void initD3D(HWND hWnd);
void render_frame(void);
void cleanD3D(void);
void init_graphics(void);

struct CUSTOMVERTEX {FLOAT X, Y, Z; DWORD COLOR;};
#define CUSTOMFVF (D3DFVF_XYZ | D3DFVF_DIFFUSE)

// Matrix functions
D3DMATRIX* MatrixPerspectiveFovLH(
    D3DMATRIX * pOut,
    FLOAT fovy,
    FLOAT Aspect,
    FLOAT zn,
    FLOAT zf
    );


D3DMATRIX* MatrixLookAtLH( 
    D3DMATRIX *pOut, 
    const D3DVECTOR *pEye, 
    const D3DVECTOR *pAt,
    const D3DVECTOR *pUp 
    );

D3DMATRIX* MatrixRotationY(
    D3DMATRIX *pOut,
    FLOAT angle
    );

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return -1;

    SDL_Window* window = SDL_CreateWindow(
        "Gallium Nine SDL",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        0);
    if (!window)
        return -1;

    initD3D(window);

    while(!SDL_QuitRequested())
        render_frame();

    cleanD3D();

    return 0;
}

void initD3D(HWND hWnd)
{
    Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d);

    D3DPRESENT_PARAMETERS d3dpp;

    memset(&d3dpp, 0, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferWidth = SCREEN_WIDTH;
    d3dpp.BackBufferHeight = SCREEN_HEIGHT;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;

    d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,
                      D3DDEVTYPE_HAL,
                      hWnd,
                      D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                      &d3dpp,
                      NULL,
                      &d3ddev);

    init_graphics();

    d3ddev->SetRenderState(D3DRS_LIGHTING, FALSE);
    d3ddev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d3ddev->SetRenderState(D3DRS_ZENABLE, TRUE);
}


void render_frame(void)
{
    d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    d3ddev->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    d3ddev->BeginScene();

    d3ddev->SetFVF(CUSTOMFVF);

    D3DMATRIX matView;
    
    D3DVECTOR from = {0.0f, 8.0f, 25.0f};
    D3DVECTOR at = {0.0f, 0.0f, 0.0f};
    D3DVECTOR up = {0.0f, 1.0f, 0.0f};
    MatrixLookAtLH(&matView, &from, &at, &up);
    d3ddev->SetTransform(D3DTS_VIEW, &matView);

    D3DMATRIX matProjection;
    MatrixPerspectiveFovLH(&matProjection,
                           (FLOAT)M_PI_4,
                           (FLOAT)SCREEN_WIDTH / (FLOAT)SCREEN_HEIGHT,
                           1.0f,
                           100.0f);
    d3ddev->SetTransform(D3DTS_PROJECTION, &matProjection);

    static float index = 0.0f; index+=0.03f;
    D3DMATRIX matRotateY;
    MatrixRotationY(&matRotateY, index);
    d3ddev->SetTransform(D3DTS_WORLD, &(matRotateY));

    d3ddev->SetStreamSource(0, v_buffer, 0, sizeof(CUSTOMVERTEX));
    d3ddev->SetIndices(i_buffer);

    d3ddev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 8, 0, 12);

    d3ddev->EndScene(); 

    d3ddev->Present(NULL, NULL, NULL, NULL);
}


void cleanD3D(void)
{
    v_buffer->Release();
    i_buffer->Release();
    d3ddev->Release();
    d3d->Release();
}


void init_graphics(void)
{
    CUSTOMVERTEX vertices[] =
    {
        { -3.0f, 3.0f, -3.0f, D3DCOLOR_XRGB(0, 0, 255), },
        { 3.0f, 3.0f, -3.0f, D3DCOLOR_XRGB(0, 255, 0), },
        { -3.0f, -3.0f, -3.0f, D3DCOLOR_XRGB(255, 0, 0), },
        { 3.0f, -3.0f, -3.0f, D3DCOLOR_XRGB(0, 255, 255), },
        { -3.0f, 3.0f, 3.0f, D3DCOLOR_XRGB(0, 0, 255), },
        { 3.0f, 3.0f, 3.0f, D3DCOLOR_XRGB(255, 0, 0), },
        { -3.0f, -3.0f, 3.0f, D3DCOLOR_XRGB(0, 255, 0), },
        { 3.0f, -3.0f, 3.0f, D3DCOLOR_XRGB(0, 255, 255), },
    };

    d3ddev->CreateVertexBuffer(8*sizeof(CUSTOMVERTEX),
                               0,
                               CUSTOMFVF,
                               D3DPOOL_MANAGED,
                               &v_buffer,
                               NULL);

    void* pVoid;

    v_buffer->Lock(0, 0, (void**)&pVoid, 0);
    memcpy(pVoid, vertices, sizeof(vertices));
    v_buffer->Unlock();

    short indices[] =
    {
        0, 1, 2,
        2, 1, 3,
        4, 0, 6,
        6, 0, 2,
        7, 5, 6,
        6, 5, 4,
        3, 1, 7,
        7, 1, 5,
        4, 5, 0,
        0, 5, 1,
        3, 7, 2,
        2, 7, 6,
    };

    d3ddev->CreateIndexBuffer(36*sizeof(short),
                              0,
                              D3DFMT_INDEX16,
                              D3DPOOL_MANAGED,
                              &i_buffer,
                              NULL);

    i_buffer->Lock(0, 0, (void**)&pVoid, 0);
    memcpy(pVoid, indices, sizeof(indices));
    i_buffer->Unlock();
}

//////////////////////////////////////////////////////////////////////
//
// Matrix functions
//
// The purpose of these functions is to remove any dependencies on
// the D3DX utility library from this sample. The functions are
// modeled after the equivalent D3DX functions. In a real application,
// you should use the D3DX library instead.
//
// The MIT License (MIT)
//
// Copyright (c) Microsoft Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
//
//////////////////////////////////////////////////////////////////////

template <class T>
inline T SQUARED(T x)
{
    return x * x;
}

D3DVECTOR* VecSubtract(D3DVECTOR *pOut, const D3DVECTOR *pV1, const D3DVECTOR *pV2)
{
    pOut->x = pV1->x - pV2->x;
    pOut->y = pV1->y - pV2->y;
    pOut->z = pV1->z - pV2->z;
    return pOut;
}

D3DVECTOR* VecNormalize(D3DVECTOR *pOut, const D3DVECTOR *pV1)
{
    FLOAT norm_sq = SQUARED(pV1->x) + SQUARED(pV1->y) + SQUARED(pV1->z);

    if (norm_sq > FLT_MIN)
    {
        FLOAT f = sqrtf(norm_sq);
        pOut->x = pV1->x / f;
        pOut->y = pV1->y / f;
        pOut->z = pV1->z / f;
    }
    else
    {
        pOut->x = 0.0f;
        pOut->y = 0.0f;
        pOut->z = 0.0f;
    }
    return pOut;
}

D3DVECTOR* VecCross(D3DVECTOR *pOut, const D3DVECTOR *pV1, const D3DVECTOR *pV2)
{
    pOut->x = pV1->y * pV2->z - pV1->z * pV2->y;
    pOut->y = pV1->z * pV2->x - pV1->x * pV2->z;
    pOut->z = pV1->x * pV2->y - pV1->y * pV2->x;

    return pOut;
}

FLOAT VecDot(const D3DVECTOR *pV1, const D3DVECTOR *pV2)
{
    return pV1->x * pV2->x + pV1->y * pV2->y + pV1->z * pV2->z;
}

// MatrixLookAtLH: Approximately equivalent to D3DXMatrixLookAtLH.

D3DMATRIX* MatrixLookAtLH( 
    D3DMATRIX *pOut, 
    const D3DVECTOR *pEye, 
    const D3DVECTOR *pAt,
    const D3DVECTOR *pUp 
    )
{

    D3DVECTOR vecX, vecY, vecZ;

    // Compute direction of gaze. (+Z)

    VecSubtract(&vecZ, pAt, pEye);
    VecNormalize(&vecZ, &vecZ);

    // Compute orthogonal axes from cross product of gaze and pUp vector.
    VecCross(&vecX, pUp, &vecZ);
    VecNormalize(&vecX, &vecX);
    VecCross(&vecY, &vecZ, &vecX);

    // Set rotation and translate by pEye
    pOut->_11 = vecX.x;
    pOut->_21 = vecX.y;
    pOut->_31 = vecX.z;
    pOut->_41 = -VecDot(&vecX, pEye);

    pOut->_12 = vecY.x;
    pOut->_22 = vecY.y;
    pOut->_32 = vecY.z;
    pOut->_42 = -VecDot(&vecY, pEye);

    pOut->_13 = vecZ.x;
    pOut->_23 = vecZ.y;
    pOut->_33 = vecZ.z;
    pOut->_43 = -VecDot(&vecZ, pEye);

    pOut->_14 = 0.0f;
    pOut->_24 = 0.0f;
    pOut->_34 = 0.0f;
    pOut->_44 = 1.0f;

    return pOut;
}


// MatrixPerspectiveFovLH: Approximately equivalent to D3DXMatrixPerspectiveFovLH.

D3DMATRIX* MatrixPerspectiveFovLH(
    D3DMATRIX * pOut,
    FLOAT fovy,
    FLOAT Aspect,
    FLOAT zn,
    FLOAT zf
    )
{   
    // yScale = cot(fovy/2)

    FLOAT yScale = cosf(fovy * 0.5f) / sinf(fovy * 0.5f);
    FLOAT xScale = yScale / Aspect;

    memset(pOut, 0, sizeof(D3DMATRIX));

    pOut->_11 = xScale;

    pOut->_22 = yScale;

    pOut->_33 = zf / (zf - zn);
    pOut->_34 = 1.0f;

    pOut->_43 = -pOut->_33 * zn;

    return pOut;
}

D3DMATRIX* MatrixRotationY(
    D3DMATRIX *pOut,
    FLOAT angle
    )
{
    FLOAT sine = sinf(angle);
    FLOAT cosine = cosf(angle);

    memset(pOut, 0, sizeof(D3DMATRIX));

    pOut->_11 = cosine;
    pOut->_13 = -sine;

    pOut->_22 = 1.0f;

    pOut->_31 = sine;
    pOut->_33 = cosine;

    pOut->_44 = 1.0f;

    return pOut;
}
