/* -LICENSE-START-
** Copyright (c) 2011 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
** 
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/

#include "stdafx.h"

#include "PreviewWindow.h"
#include <strsafe.h>

#pragma comment (lib, "d3d9")

// Hack for now: Windows gl/gl.h doesn't provide these extensions.
// In order to prevent this sample from depending on vendor OpenGL
// code, we just define them here:

#ifndef GL_TEXTURE_RECTANGLE_EXT
#define GL_TEXTURE_RECTANGLE_EXT          0x84F5
#endif

#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV       0x8367
#endif

#ifndef GL_BGRA
#define GL_BGRA                           0x80E1
#endif


// Note: some of this code is based off Microsoft's MFCapture3
// sample application

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride);
static void TransformImage_NV12(
    BYTE* pDst,
    LONG dstStride,
    const BYTE* pSrc,
    LONG srcStride,
    DWORD dwWidthInPixels,
    DWORD dwHeightInPixels);

static const TCHAR CLASS_NAME[]  = _T("StreamingPreview Sample Window Class");
static const TCHAR WINDOW_NAME[] = _T("StreamingPreview Sample");

PreviewWindow::PreviewWindow(PreviewWindowClosedCb closedCb, void* ctx)
{
	m_hwnd = NULL;
	m_newTextureInBuffer = false;

	m_needsReset = false;

	InitializeCriticalSection(&m_criticalSection);

	m_closedCb = closedCb;
	m_closedCbCtx = ctx;
}

PreviewWindow::~PreviewWindow()
{
	if (m_hwnd != NULL)
		DestroyWindow(m_hwnd);
}

PreviewWindow* PreviewWindow::CreateInstance(PreviewWindowClosedCb closedCb, void* ctx)
{
	PreviewWindow* instance = new PreviewWindow(closedCb, ctx);

	if (! instance->DoCreateWindow())
	{
		delete instance;
		instance = NULL;
	}

	return instance;
}

// Called by the DecoderMF class when the media type
// changes.
//
// Thread context: decoder thread
bool PreviewWindow::SetMediaType(IMFMediaType* mediaType)
{
	HRESULT			hr;
	bool			ret = false;
	GUID			subtype;
	UINT			width, height;
	LONG			defaultStride;
	MFRatio			PAR = { 0 };

	EnterCriticalSection(&m_criticalSection);

		hr = mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
		if (FAILED(hr))
			goto bail;

		hr = MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &width, &height);
		if (FAILED(hr))
			goto bail;

		// TODO: get if it's interlaced / progressive (MF_MT_INTERLACE_MODE)

		hr = GetDefaultStride(mediaType, &defaultStride);
		if (FAILED(hr))
			goto bail;

		// Get the pixel aspect ratio. Default: Assume square pixels (1:1)
		hr = MFGetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO,
			(UINT32*)&PAR.Numerator,
			(UINT32*)&PAR.Denominator);

		if (FAILED(hr))
		{
			PAR.Numerator = PAR.Denominator = 1;
		}

		// Creates a new RGBA (32bpp) buffer for the converted frame
		m_width = width;
		m_height = height;
		m_defaultStride = defaultStride;
		m_newTextureInBuffer = false;

		ret = true;

bail:
	LeaveCriticalSection(&m_criticalSection);

	return ret;
}

static void copy_picture(const void* src, void* dst, int bytes_per_line, int src_stride, int dst_stride, int height)
{
	if(src_stride == dst_stride && (abs(src_stride) == bytes_per_line))
	{
		if (src_stride < 0) {
			src = (char*)src + (height-1)*src_stride;
			dst = (char*)dst + (height-1)*dst_stride;
			src_stride = -src_stride;
		}
		memcpy(dst, src, src_stride*height);
	}
	else
	{
		for(int y = 0; y < height; ++y)
		{
			memcpy(dst, src, bytes_per_line);
			src = (char*)src + src_stride;
			dst = (char*)dst + dst_stride;
		}
	}
}

// Called by the DecoderMF class when we have a new frame.
// We convert and copy the frame into BGRA32 format and place
// into the m_textureBuffer buffer.
//
// Thread context: decoder thread
bool PreviewWindow::DrawFrame(IMFMediaBuffer* mediaBuffer)
{
	bool			ret = false;
	HRESULT			hr;
	BYTE*			pData = NULL;
	BYTE*			pScanLine0;

	if (m_hwnd == NULL)
		return true;

	hr = m_device->TestCooperativeLevel();
    if(hr == D3DERR_DEVICELOST)
        return false;
    else if(hr == D3DERR_DEVICENOTRESET || m_needsReset)
        if(!reset())
            return false;

	EnterCriticalSection(&m_criticalSection);

		// Get the pointer to the data
		hr = mediaBuffer->Lock(&pData, NULL, NULL);
		if (FAILED(hr))
			goto bail;

		if (m_defaultStride < 0)
			pScanLine0 = pData + abs((long)m_defaultStride) * (m_height - 1);
		else
			pScanLine0 = pData;

		if(!m_surface)
		{
			hr = m_device->CreateOffscreenPlainSurface(m_width, m_height, D3DFMT_YUY2, D3DPOOL_DEFAULT, &m_surface, NULL);
			ATLASSERT(hr == S_OK);
		}
		D3DLOCKED_RECT locked;
		RECT rc;
		rc.left = 0;
		rc.top = 0;
		rc.right = m_width;
		rc.bottom = m_height;
		if(m_surface->LockRect(&locked, &rc, D3DLOCK_DISCARD) == S_OK)
		{
			copy_picture(pScanLine0, (BYTE*)locked.pBits, m_width*2, m_width*2, locked.Pitch, m_height);
			m_surface->UnlockRect();
		}

		hr = mediaBuffer->Unlock();
		if (FAILED(hr))
			goto bail;

		{
			CComPtr<IDirect3DSurface9> screen_surface;
			m_device->GetRenderTarget(0, &screen_surface);
			m_device->StretchRect(m_surface, &rc, screen_surface, NULL, D3DTEXF_LINEAR);
		}

		if (m_newTextureInBuffer)
			OutputDebugString(_T("WARNING: overwritten undisplayed texture\n"));
		m_newTextureInBuffer = true;

	ret = true;

bail:
	LeaveCriticalSection(&m_criticalSection);

	if (ret)
		InvalidateRect(m_hwnd, NULL, FALSE);
	return ret;
}

// Thread context: decoder thread
void PreviewWindow::SetWindowSize(int width, int height)
{
	SetWindowPos(m_hwnd, NULL, 0, 0, width, height,	SWP_NOMOVE | SWP_NOOWNERZORDER);
}

bool PreviewWindow::DoCreateWindow()
{
	HWND		newHwnd;

	if (! DoCreateClass())
		return false;

	// Create the new window
    newHwnd = CreateWindow(CLASS_NAME,
		WINDOW_NAME,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, // x, y, width, height
        NULL,
		NULL,
		GetModuleHandle(NULL),
		this);

    if (! newHwnd)
		return false;

    m_hwnd = newHwnd;

    ShowWindow(newHwnd, SW_SHOWDEFAULT);
    UpdateWindow(newHwnd);

    return true;
}

bool PreviewWindow::DoCreateClass()
{
	static bool classCreated = false;

	if (! classCreated)
	{
	    WNDCLASSEX	wc = {0};

		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = PreviewWindowProc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = GetModuleHandle(NULL);
		wc.hIcon = 0;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = NULL;
		wc.lpszMenuName = NULL;
		wc.lpszClassName = CLASS_NAME;
		wc.hIconSm = NULL;

		if (!RegisterClassEx(&wc))
			return false;

		classCreated = true;
	}

	return true;
}

LRESULT PreviewWindow::OnCreate(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	CWindow window;
	window.Attach(hwnd);

	CRect rcClient;
	window.GetWindowRect(rcClient);

	m_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	D3DPRESENT_PARAMETERS	d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
	d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
	d3dpp.BackBufferCount = 2;
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = hwnd;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
	HRESULT hr = m_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &d3dpp, &m_device);
	ATLASSERT(S_OK == hr);

	return 0;
}

bool PreviewWindow::reset()
{
	m_needsReset = false;
	HRESULT hr = m_device->TestCooperativeLevel();
	if(hr == D3DERR_DEVICENOTRESET || hr == D3D_OK )
	{
        m_surface.Release();

		D3DPRESENT_PARAMETERS d3dpp;
		ZeroMemory(&d3dpp, sizeof(d3dpp));
		d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
		d3dpp.BackBufferCount = 2;
		d3dpp.Windowed = TRUE;
		d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
		d3dpp.hDeviceWindow = m_hwnd;
		d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
		hr = m_device->Reset(&d3dpp);
		return true;
	}
    else
    {
        return false;
    }
}

LRESULT PreviewWindow::OnDestroy(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	m_hwnd = NULL;

	if (m_closedCb != NULL)
		m_closedCb(m_closedCbCtx, this);

	return 0;
}

LRESULT PreviewWindow::OnPaint(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	HDC		hDc;
	PAINTSTRUCT paintStruct;

	hDc = BeginPaint(hwnd, &paintStruct);

	EnterCriticalSection(&m_criticalSection);

	HRESULT hr = m_device->TestCooperativeLevel();
    if(hr == D3DERR_DEVICELOST)
		goto bail;
    else if(hr == D3DERR_DEVICENOTRESET || m_needsReset)
        if(!reset())
			goto bail;

	m_device->Present(NULL, NULL, NULL, NULL);
	m_newTextureInBuffer = false;

bail:
	LeaveCriticalSection(&m_criticalSection);
	EndPaint(hwnd, &paintStruct);

	return 0;
}

LRESULT PreviewWindow::OnSize(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	m_needsReset = true;
	return 0;
}

LRESULT PreviewWindow::PreviewWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	PreviewWindow* window = NULL;

	// Store a pointer to the PreviewWindow object in the window class' user data
	LONG_PTR userData = GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (userData)
	{
		window = reinterpret_cast<PreviewWindow *>(userData);
	}
	else if (uMsg == WM_CREATE)
	{
		LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		void* lpCreateParam = createStruct->lpCreateParams;
		window = reinterpret_cast<PreviewWindow *>(lpCreateParam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
	}

	if (window)
	{
		switch (uMsg)
		{
		case WM_CREATE:
			return window->OnCreate(hwnd, wParam, lParam);
		case WM_DESTROY:
			return window->OnDestroy(hwnd, wParam, lParam);
		case WM_ERASEBKGND:
			return 0;
		case WM_PAINT:
			return window->OnPaint(hwnd, wParam, lParam);
		case WM_SIZE:
			return window->OnSize(hwnd, wParam, lParam);
		}
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

//-------------------------------------------------------------------
// Taken from MediaFoundation samples by Microsoft
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//-------------------------------------------------------------------

//-----------------------------------------------------------------------------
// GetDefaultStride
//
// Gets the default stride for a video frame, assuming no extra padding bytes.
//
//-----------------------------------------------------------------------------

HRESULT GetDefaultStride(IMFMediaType *pType, LONG *plStride)
{
    LONG lStride = 0;

    // Try to get the default stride from the media type.
    HRESULT hr = pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&lStride);
    if (FAILED(hr))
    {
        // Attribute not set. Try to calculate the default stride.
        GUID subtype = GUID_NULL;

        UINT32 width = 0;
        UINT32 height = 0;

        // Get the subtype and the image size.
        hr = pType->GetGUID(MF_MT_SUBTYPE, &subtype);
        if (SUCCEEDED(hr))
        {
            hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
        }
        if (SUCCEEDED(hr))
        {
            hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &lStride);
        }

        // Set the attribute for later reference.
        if (SUCCEEDED(hr))
        {
            (void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(lStride));
        }
    }

    if (SUCCEEDED(hr))
    {
        *plStride = lStride;
    }
    return hr;
}

//-------------------------------------------------------------------
//
// Conversion functions
//
//-------------------------------------------------------------------

__forceinline BYTE Clip(int clr)
{
    return (BYTE)(clr < 0 ? 0 : ( clr > 255 ? 255 : clr ));
}

__forceinline RGBQUAD ConvertYCrCbToRGB(
    int y,
    int cr,
    int cb
    )
{
    RGBQUAD rgbq;

    int c = y - 16;
    int d = cb - 128;
    int e = cr - 128;

    rgbq.rgbRed =   Clip(( 298 * c           + 409 * e + 128) >> 8);
    rgbq.rgbGreen = Clip(( 298 * c - 100 * d - 208 * e + 128) >> 8);
    rgbq.rgbBlue =  Clip(( 298 * c + 516 * d           + 128) >> 8);

    return rgbq;
}


//-------------------------------------------------------------------
// TransformImage_NV12
//
// NV12 to RGB-32
//-------------------------------------------------------------------

void TransformImage_NV12(
    BYTE* pDst,
    LONG dstStride,
    const BYTE* pSrc,
    LONG srcStride,
    DWORD dwWidthInPixels,
    DWORD dwHeightInPixels
    )
{
    const BYTE* lpBitsY = pSrc;
    const BYTE* lpBitsCb = lpBitsY  + (dwHeightInPixels * srcStride);;
    const BYTE* lpBitsCr = lpBitsCb + 1;

    for (UINT y = 0; y < dwHeightInPixels; y += 2)
    {
        const BYTE* lpLineY1 = lpBitsY;
        const BYTE* lpLineY2 = lpBitsY + srcStride;
        const BYTE* lpLineCr = lpBitsCr;
        const BYTE* lpLineCb = lpBitsCb;

        LPBYTE lpDibLine1 = pDst;
        LPBYTE lpDibLine2 = pDst + dstStride;

        for (UINT x = 0; x < dwWidthInPixels; x += 2)
        {
            int  y0 = (int)lpLineY1[0];
            int  y1 = (int)lpLineY1[1];
            int  y2 = (int)lpLineY2[0];
            int  y3 = (int)lpLineY2[1];
            int  cb = (int)lpLineCb[0];
            int  cr = (int)lpLineCr[0];

            RGBQUAD r = ConvertYCrCbToRGB(y0, cr, cb);
            lpDibLine1[0] = r.rgbBlue;
            lpDibLine1[1] = r.rgbGreen;
            lpDibLine1[2] = r.rgbRed;
            lpDibLine1[3] = 0; // Alpha

            r = ConvertYCrCbToRGB(y1, cr, cb);
            lpDibLine1[4] = r.rgbBlue;
            lpDibLine1[5] = r.rgbGreen;
            lpDibLine1[6] = r.rgbRed;
            lpDibLine1[7] = 0; // Alpha

            r = ConvertYCrCbToRGB(y2, cr, cb);
            lpDibLine2[0] = r.rgbBlue;
            lpDibLine2[1] = r.rgbGreen;
            lpDibLine2[2] = r.rgbRed;
            lpDibLine2[3] = 0; // Alpha

            r = ConvertYCrCbToRGB(y3, cr, cb);
            lpDibLine2[4] = r.rgbBlue;
            lpDibLine2[5] = r.rgbGreen;
            lpDibLine2[6] = r.rgbRed;
            lpDibLine2[7] = 0; // Alpha

            lpLineY1 += 2;
            lpLineY2 += 2;
            lpLineCr += 2;
            lpLineCb += 2;

            lpDibLine1 += 8;
            lpDibLine2 += 8;
        }

        pDst += (2 * dstStride);
        lpBitsY   += (2 * srcStride);
        lpBitsCr  += srcStride;
        lpBitsCb  += srcStride;
    }
}

