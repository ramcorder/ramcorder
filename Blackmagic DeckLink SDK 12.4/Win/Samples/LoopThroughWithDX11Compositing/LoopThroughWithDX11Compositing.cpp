/* -LICENSE-START-
 ** Copyright (c) 2015 Blackmagic Design
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
//
// LoopThroughWithDX11Compositing.cpp
// LoopThroughWithDX11Compositing
//

#include "stdafx.h"
#include "resource.h"
#include "DX11Composite.h"


#define MAX_LOADSTRING 100

// Declaration for Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Select the pixel format for a given device context
void SetDCPixelFormat(HDC hDC)
{
	int nPixelFormat;

	static PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),	// Size of this structure
		1,								// Version of this structure
		PFD_DRAW_TO_WINDOW |			// Draw to Window (not to bitmap)
		PFD_DOUBLEBUFFER,				// Double buffered mode
		PFD_TYPE_RGBA,					// RGBA Color mode
		32,								// Want 32 bit color
		0, 0, 0, 0, 0, 0,					// Not used to select mode
		0, 0,							// Not used to select mode
		0, 0, 0, 0, 0,						// Not used to select mode
		16,								// Size of depth buffer
		0,								// Not used
		0,								// Not used
		0,								// Not used
		0,								// Not used
		0, 0, 0 };						// Not used

	// Choose a pixel format that best matches that described in pfd
	nPixelFormat = ChoosePixelFormat(hDC, &pfd);

	// Set the pixel format for the device context
	SetPixelFormat(hDC, nPixelFormat, &pfd);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	MSG			msg;									// Windows message structure
	WNDCLASS	wc;										// Windows class structure
	HWND		hWnd;									// Storeage for window handle
	TCHAR		szTitle[MAX_LOADSTRING];				// The title bar text
	TCHAR		szWindowClass[MAX_LOADSTRING];			// the main window class name

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_OPENGLOUTPUT, szWindowClass, MAX_LOADSTRING);

	// Register Window style
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = (WNDPROC)WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	// No need for background brush for Direct3D window
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = szWindowClass;

	// Register the window class
	if (RegisterClass(&wc) == 0)
		return FALSE;

	// Create the main application window
	hWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CW_USEDEFAULT, 0, 250, 250, NULL, NULL, hInstance, NULL);

	// If window was not created, quit
	if (hWnd == NULL)
		return FALSE;

	// Display the window
	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	// Process application messages until the application closes
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

// Window procedure, handles all messages for this program
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HGLRC				hRC = NULL;					// Permenant Rendering context
	static HDC					hDC = NULL;					// Private GDI Device context
	static DX11Composite*		pDX11Composite = NULL;

	switch (message)
	{
		// Window creation, setup for DX11 context
		case WM_CREATE:
			// Store the device context
			hDC = GetDC(hWnd);

			// Select the pixel format
			SetDCPixelFormat(hDC);

			// Initialize COM
			HRESULT result;
			result = CoInitialize(NULL);
			if (FAILED(result))
			{
				MessageBox(NULL, _T("Initialization of COM failed."), _T("Application initialization Error."), MB_OK);
				PostMessage(hWnd, WM_CLOSE, 0, 0);
				break;
			}

			// Setup DX11 and DeckLink capture and playout object
			pDX11Composite = new DX11Composite(hWnd, hDC);

			if (pDX11Composite->InitDeckLink())
			{
				if (pDX11Composite->Start())
					break;						// success
			}

			// Failed to initialize - cleanup
			delete pDX11Composite;
			pDX11Composite = NULL;
			PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;

		case WM_DESTROY:
			if (pDX11Composite)
			{
				pDX11Composite->Stop();
				delete pDX11Composite;
			}

			// Tell the application to terminate after the window is gone
			PostQuitMessage(0);
			break;

		case WM_SIZE:
			if (pDX11Composite)
				pDX11Composite->resizeDX(LOWORD(lParam), HIWORD(lParam));
			break;

		default:
			return (DefWindowProc(hWnd, message, wParam, lParam));
	}

	return (0L);
}

