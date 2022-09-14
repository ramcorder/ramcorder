/* -LICENSE-START-
** Copyright (c) 2019 Blackmagic Design
**  
** Permission is hereby granted, free of charge, to any person or organization 
** obtaining a copy of the software and accompanying documentation (the 
** "Software") to use, reproduce, display, distribute, sub-license, execute, 
** and transmit the Software, and to prepare derivative works of the Software, 
** and to permit third-parties to whom the Software is furnished to do so, in 
** accordance with:
** 
** (1) if the Software is obtained from Blackmagic Design, the End User License 
** Agreement for the Software Development Kit (“EULA”) available at 
** https://www.blackmagicdesign.com/EULA/DeckLinkSDK; or
** 
** (2) if the Software is obtained from any third party, such licensing terms 
** as notified by that third party,
** 
** and all subject to the following:
** 
** (3) the copyright notices in the Software and this entire statement, 
** including the above license grant, this restriction and the following 
** disclaimer, must be included in all copies of the Software, in whole or in 
** part, and all derivative works of the Software, unless such copies or 
** derivative works are solely in the form of machine-executable object code 
** generated by a source language processor.
** 
** (4) THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
** OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
** DEALINGS IN THE SOFTWARE.
** 
** A copy of the Software is available free of charge at 
** https://www.blackmagicdesign.com/desktopvideo_sdk under the EULA.
** 
** -LICENSE-END-
*/

#include "stdafx.h"
#include <comutil.h>
#include "D3D9Types.h"
#include "PreviewWindow.h"

PreviewWindow::PreviewWindow()
	: m_refCount(1), 
	m_previewBox(nullptr)
{
}

bool		PreviewWindow::Initialize(CStatic* previewBox)
{
	m_previewBox = previewBox;

	// Create the DeckLink screen preview helper
	if (m_deckLinkScreenPreviewHelper.CoCreateInstance(CLSID_CDeckLinkDX9ScreenPreviewHelper, nullptr, CLSCTX_ALL) != S_OK)
		return false;

	// Initialise DirectX
	return initDirectX(m_previewBox->GetSafeHwnd());
}

bool		PreviewWindow::initDirectX(HWND hWnd)
{
	D3DPRESENT_PARAMETERS	d3dpp;
	bool					result = false;

	m_dx3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (m_dx3D == nullptr)
		goto bail;

	ZeroMemory(&d3dpp, sizeof(D3DPRESENT_PARAMETERS));
	d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
	d3dpp.BackBufferCount = 2;
	d3dpp.Windowed = TRUE;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.hDeviceWindow = hWnd;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

	if (m_dx3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &d3dpp, &m_dx3DDevice) != D3D_OK)
		goto bail;

	if (m_deckLinkScreenPreviewHelper->Initialize(m_dx3DDevice) == S_OK)
		result = true;

bail:
	return result;
}

void		PreviewWindow::Render()
{
	D3DVIEWPORT9 viewport;
	
	m_dx3DDevice->BeginScene();

	m_dx3DDevice->GetViewport(&viewport);

	RECT rect;
	rect.top = viewport.Y;
	rect.left = viewport.X;
	rect.bottom = viewport.Y + viewport.Height;
	rect.right = viewport.X + viewport.Width;

	m_deckLinkScreenPreviewHelper->Render(&rect);
	
	m_dx3DDevice->EndScene();
	m_dx3DDevice->Present(nullptr, nullptr, nullptr, nullptr);
}


HRESULT 	PreviewWindow::QueryInterface(REFIID iid, LPVOID *ppv)
{
	*ppv = nullptr;
	return E_NOINTERFACE;
}

ULONG		PreviewWindow::AddRef()
{
	return ++m_refCount;
}

ULONG		PreviewWindow::Release()
{
	ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}

HRESULT			PreviewWindow::DrawFrame(IDeckLinkVideoFrame* theFrame)
{
	// Set current frame in preview helper
	m_deckLinkScreenPreviewHelper->SetFrame(theFrame);

	// Then draw the frame to the scene
	Render();

	return S_OK;
}

