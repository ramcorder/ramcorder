// GdiKeyer.cpp
/* -LICENSE-START-
** Copyright (c) 2010 Blackmagic Design
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

#include <conio.h>
#include <objbase.h>		// Necessary for COM
#include <comutil.h>
#include <stdio.h>
#include <string.h>
#include "DeckLinkAPI_h.h"
#include "stdafx.h"
#include "GdiKeyer.h"

static BMDDisplayMode currentDisplayMode;
static bool						displayModeDetected = false;
static IDeckLinkIterator		*deckLinkIterator = NULL; 
static IDeckLink				*deckLink = NULL;
static IDeckLinkInput			*deckLinkInput = NULL;
static IDeckLinkOutput			*deckLinkOutput = NULL;
static IDeckLinkDisplayMode		*detectedDisplayMode = NULL;

int	OutputGraphic (IDeckLinkDisplayMode *deckLink);
void GdiDraw (IDeckLinkVideoFrame* theFrame);
int	CheckFormatDetect (IDeckLinkProfileAttributes		*deckLinkAttributes);

DeckLinkKeyerDelegate::DeckLinkKeyerDelegate()
	: m_refCount(1)
{
}

DeckLinkKeyerDelegate::~DeckLinkKeyerDelegate()
{
}

HRESULT DeckLinkKeyerDelegate::QueryInterface(REFIID iid, LPVOID *ppv)
{
	*ppv = NULL;
	return E_NOINTERFACE;
}

ULONG DeckLinkKeyerDelegate::AddRef(void)
{
	return InterlockedIncrement((LONG*)&m_refCount);
}

ULONG  DeckLinkKeyerDelegate::Release(void)
{
	ULONG           newRefValue;

	newRefValue = InterlockedDecrement((LONG*)&m_refCount);
	if (newRefValue == 0)
	{
		delete this;
		return 0;
	}

	return newRefValue;
}

HRESULT DeckLinkKeyerDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode* newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	BMDTimeValue timeVal;
	BMDTimeScale timeScale;
	BSTR displayName;
	long width;
	long height;
	
	detectedDisplayMode = newDisplayMode;
	detectedDisplayMode->AddRef();								// keep the display mode details for main()

	currentDisplayMode = newDisplayMode->GetDisplayMode();
	newDisplayMode->GetFrameRate(&timeVal, &timeScale);
	height = newDisplayMode->GetHeight();
	width = newDisplayMode->GetWidth();

	newDisplayMode->GetName(&displayName);
	_bstr_t displaymodeName(displayName);		

	printf("Detected video mode: %s Height: %lu Width: %lu\n", (char*)displaymodeName, height, width);
	displayModeDetected = true;

	switch (notificationEvents)
	{
		case bmdVideoInputDisplayModeChanged:
			printf("Video input display mode changed\n");
			break;
		case bmdVideoInputFieldDominanceChanged:
			printf("Video input field dominance changed\n");
			break;
		case bmdVideoInputColorspaceChanged:
			printf("Video input color space changed\n");
			break;
		default:
			break;
	}

	switch (detectedSignalFlags)
	{
		case bmdDetectedVideoInputYCbCr422:
			printf("Signal flags: bmdDetectedVideoInputYCbCr422\n");
			break;
		case bmdDetectedVideoInputRGB444:
			printf("Signal flags: bmdDetectedVideoInputRGB444\n");
			break;
		default:
			break;
	}
	return S_OK;
}

HRESULT DeckLinkKeyerDelegate::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioFrame)
{
	BMDPixelFormat	pixelFormat;
	BMDFrameFlags	frameFlags;

	pixelFormat = videoFrame->GetPixelFormat();
	frameFlags = videoFrame->GetFlags();
	printf("VideoInputFrameArrived\nPixel format: ");
	switch (pixelFormat)
	{
		case bmdFormat8BitYUV:
			printf("8BitYUV\n");
			break;
		case bmdFormat10BitYUV:
			printf("10BitYUV\n");
			break;
		case bmdFormat8BitARGB:
			printf("8BitARGB\n");
			break;
		case bmdFormat8BitBGRA:
			printf("8BitBGRA\n");
			break;
		case bmdFormat10BitRGB:
			printf("10BitRGB\n");
			break;
		default:
			printf("unknown\n");
			break;
	}
	if (frameFlags == bmdFrameFlagDefault)
	{
		printf("Default flag\n");
	}

	if (frameFlags == bmdFrameFlagFlipVertical)
	{
		printf("Flip vertical flag\n");
	}
	return S_OK;
}

int _tmain(int argc, _TCHAR* argv[])
{
	int						numDevices = 0; 
	HRESULT					result;
	IDeckLinkProfileAttributes		*deckLinkAttributes = NULL;
	HRESULT					attributeResult;
	BOOL					keyingSupported;
	int						selectedMode = 0;

	printf("GDI Keyer Sample Application\n\n"); 
	// Initialize COM on this thread
	result = CoInitialize(NULL);

	if (FAILED(result))
	{
		fprintf(stderr, "Initialization of COM failed - result = %08x.\n", result);
		return 1;
	}
	
	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	result = CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&deckLinkIterator);
	if (FAILED(result))
	{
		fprintf(stderr, "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed.\n");
		return 1;
	}

	// Enumerate all cards in this system 
	while (deckLinkIterator->Next(&deckLink) == S_OK) 
	{ 
		BSTR	deviceNameBSTR = NULL; 
		 
		// Increment the total number of DeckLink cards found 
		numDevices++; 
		if (numDevices > 1) 
			printf("\n\n");	 
		 
		// *** Print the model name of the DeckLink card 
		result = deckLink->GetModelName(&deviceNameBSTR); 
		if (result == S_OK) 
		{	
			_bstr_t deviceName(deviceNameBSTR);		
			SysFreeString(deviceNameBSTR);
			printf("Found Blackmagic device: %s\n", (char*)deviceName);
			attributeResult = deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&deckLinkAttributes);
			if (attributeResult != S_OK)
			{
				fprintf(stderr, "Could not obtain the IDeckLinkProfileAttributes interface");
				goto bail;
			}
			else
			{
				attributeResult = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInternalKeying, &keyingSupported);	// is keying supported on this device?
				if (attributeResult == S_OK && keyingSupported)			
				{
					IDeckLinkDisplayModeIterator* displayModeIterator = NULL;
					IDeckLinkDisplayMode* deckLinkDisplayMode = NULL;

					// check if automode detection support - if so, use it for autodetection				
					if (CheckFormatDetect(deckLinkAttributes) == 0)
					{
						fprintf(stderr, "Input mode detection not supported\n");

						if (deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&deckLinkOutput) != S_OK)
						{
							fprintf(stderr, "Could not obtain the IDeckLinkOutput interface\n");
						}
						else
						{
							int		index = 0;
							BOOL	modeSupported;
							if (deckLinkOutput->GetDisplayModeIterator(&displayModeIterator) != S_OK)
							{
								fprintf(stderr, "Could not obtain the display mode iterator\n");
							}
							else
							{
								printf("\n");
								while (displayModeIterator->Next(&deckLinkDisplayMode) == S_OK)
								{
									BSTR			displayModeBSTR = NULL;
									int				modeWidth;
									int				modeHeight;
									BMDTimeValue	frameRateDuration;
									BMDTimeScale	frameRateScale;

									if (deckLinkOutput->DoesSupportVideoMode(bmdVideoConnectionUnspecified, deckLinkDisplayMode->GetDisplayMode(),
																			 bmdFormat8BitARGB, bmdNoVideoOutputConversion, bmdSupportedVideoModeKeying, nullptr, &modeSupported) != S_OK || !modeSupported)
									{
										// Keying not supported in this mode
									}
									else
									{

										// Obtain the display mode's properties
										modeWidth = deckLinkDisplayMode->GetWidth();
										modeHeight = deckLinkDisplayMode->GetHeight();
										deckLinkDisplayMode->GetFrameRate(&frameRateDuration, &frameRateScale);

										if (deckLinkDisplayMode->GetName(&displayModeBSTR) == S_OK)
										{
											_bstr_t			modeName(displayModeBSTR, false);
											SysFreeString(displayModeBSTR);
											printf("%d %-20s \t %d x %d \t %g FPS\n", index, (char*)modeName, modeWidth, modeHeight, (double)frameRateScale / (double)frameRateDuration);					
										}
										index++;

									}

									deckLinkDisplayMode->Release();
								}
								displayModeIterator->Release();

								printf("Select Mode (0-%d):\n", index-1);

								scanf_s("%d", &selectedMode);
								printf("Mode selected: %d\n", selectedMode);
								if(selectedMode < index)
								{
									int modeCount = 0;
									if (deckLinkOutput->GetDisplayModeIterator(&displayModeIterator) != S_OK)
									{
										fprintf(stderr, "Could not obtain the display mode iterator\n");
									}
									else
									{
										while(displayModeIterator->Next(&deckLinkDisplayMode) == S_OK)
										{
											if (deckLinkOutput->DoesSupportVideoMode(bmdVideoConnectionUnspecified, deckLinkDisplayMode->GetDisplayMode(),
																			 		 bmdFormat8BitARGB, bmdNoVideoOutputConversion, bmdSupportedVideoModeKeying, nullptr, &modeSupported) == S_OK && modeSupported)
											{
												if (selectedMode == modeCount)
												{
													OutputGraphic(deckLinkDisplayMode);
													deckLinkDisplayMode->Release();
													break;
												}
												modeCount++;
											}
											deckLinkDisplayMode->Release();
										}										
										displayModeIterator->Release();
									}									
								}
								else
								{
									printf("Illegal video mode selected\n");
								}
							}

							deckLinkOutput->Release();
						}
					}
				}

				deckLinkAttributes->Release();
				printf("Press Enter key to exit.\n");
				_getch();
			}		
		}		 
bail:				 
		deckLink->Release(); // Release the IDeckLink instance when we've finished with it to prevent leaks
 	} 

	if (deckLinkIterator != NULL)
		deckLinkIterator->Release();

	// If no DeckLink cards were found in the system, inform the users
	if (numDevices == 0) 
		printf("No Blackmagic Design devices were found.\n");

	// Uninitalize COM on this thread
	CoUninitialize();
	return 0;
}

// Prepare video output
int	OutputGraphic (IDeckLinkDisplayMode* deckLinkDisplayMode)
{
	IDeckLinkMutableVideoFrame*	m_videoFrameGdi = NULL;

	if (deckLinkDisplayMode != NULL)
	{
		BSTR modeNameBSTR;
	
		if (deckLinkDisplayMode->GetName(&modeNameBSTR) == S_OK)
		{
			unsigned long m_frameWidth = 0;
			unsigned long m_frameHeight = 0;
			_bstr_t modeName(modeNameBSTR);

			m_frameWidth = deckLinkDisplayMode->GetWidth();
			m_frameHeight = deckLinkDisplayMode->GetHeight();
			printf("Using video mode: %s, width: %lu, height: %lu\n", (char*)modeName, m_frameWidth, m_frameHeight);			// Use the first supported video mode
		
			if (deckLinkOutput->EnableVideoOutput(deckLinkDisplayMode->GetDisplayMode(), bmdVideoOutputFlagDefault) != S_OK)
			{
				fprintf(stderr, "Could not enable Video output\n");
			}
			else
			{
				// Generate a key frame
				if (deckLinkOutput->CreateVideoFrame(m_frameWidth, m_frameHeight, m_frameWidth*4, bmdFormat8BitARGB, bmdFrameFlagFlipVertical, &m_videoFrameGdi) != S_OK)
				{
					fprintf(stderr, "Could not obtain the IDeckLinkOutput CreateVideoFrame interface\n");
				}
				else
				{
					IDeckLinkKeyer*					deckLinkKeyer = NULL;
					GdiDraw (m_videoFrameGdi);

					if (deckLink->QueryInterface(IID_IDeckLinkKeyer, (void**)&deckLinkKeyer) != S_OK)
					{
						fprintf(stderr, "Could not obtain the IDeckLinkOutput interface\n");												
					}
					else
					{
						int keyLevel = 0;	
						deckLinkKeyer->Enable(false);							// Enable internal keying									
						for (int keyStep = 0; keyStep < 5; keyStep++)			// Key in steps
						{
							deckLinkKeyer->SetLevel(keyLevel);					// Set a keying level
							printf("Current key level: %d\n", keyLevel);
							deckLinkOutput->DisplayVideoFrameSync(m_videoFrameGdi);		
							Sleep(3000);
							keyLevel += 60;
						}
						deckLinkKeyer->SetLevel(0);
						printf("Ramp up keying\n");
						deckLinkKeyer->RampUp(100);
						Sleep(5000);
						printf("Ramp down keying\n");
						deckLinkKeyer->RampDown(100);
						Sleep(5000);
						deckLinkKeyer->Disable();
						deckLinkKeyer->Release();
					}

					m_videoFrameGdi->Release();
				}

				deckLinkOutput->DisableVideoOutput();
			}
		}
	}
	return 0;	
}

int		CheckFormatDetect (IDeckLinkProfileAttributes		*deckLinkAttributes)
{
	int			result = 0;
	HRESULT		attributeResult;
	BOOL		FormatDetectsupported;
	DeckLinkKeyerDelegate 	*keyDelegate = NULL;
	IDeckLinkDisplayModeIterator* displayModeIterator = NULL;
	IDeckLinkDisplayMode* deckLinkDisplayMode = NULL;

	attributeResult = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &FormatDetectsupported);
	if (attributeResult == S_OK && FormatDetectsupported)
	{
		printf("Input mode detection supported\n");

		if (deckLink->QueryInterface(IID_IDeckLinkInput, (void**) &deckLinkInput) == S_OK)
		{
			keyDelegate = new DeckLinkKeyerDelegate();
			deckLinkInput->SetCallback(keyDelegate);

			if (deckLinkInput->EnableVideoInput(bmdModePAL, bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection) != S_OK)	// set starting mode
			{
				fprintf(stderr, "EnableVideoInput failed\n");
			}
			else
			{
				if (deckLinkInput->StartStreams() == S_OK)
				{
					Sleep(500);	// check for format..
					deckLinkInput->StopStreams();
					deckLinkInput->DisableVideoInput();				
					
					if (displayModeDetected == true)
					{
						if (deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&deckLinkOutput) == S_OK)
						{
							OutputGraphic(detectedDisplayMode);
							detectedDisplayMode->Release();
							result = 1;
							deckLinkOutput->Release();
						}
						else
						{
							fprintf(stderr, "Could not obtain the IDeckLinkOutput interface\n");
						}
					}
					else
					{
						fprintf(stderr, "Unable to detect the input video mode \n");
					}
				}
				else
				{
					fprintf(stderr, "StartStream failed\n");
				}
			}

			deckLinkInput->Release();
			keyDelegate->Release();
		}
		else
		{
			printf("Unable to get DeckLinkInput interface\n");
		}
	}

	return result;
}

// Use GDI to draw in video frame
void	GdiDraw (IDeckLinkVideoFrame* theFrame)
{
	BITMAPINFOHEADER bmi;
	
	ZeroMemory(&bmi, sizeof(bmi));
	bmi.biSize = sizeof(bmi);
	bmi.biWidth = theFrame->GetWidth();
	bmi.biHeight = theFrame->GetHeight();
	bmi.biPlanes = 1;
	bmi.biBitCount = 32;
	bmi.biCompression = BI_RGB;
	bmi.biSizeImage = (bmi.biWidth * bmi.biHeight * 4);
	
	HDC hdc = CreateCompatibleDC(NULL);
	BYTE* pbData = NULL;
	BYTE* pbDestData = NULL;

	RECT fillRect1 = {50, 50, 100, 100};
	RECT fillRect2 = {100, 100, 150, 150};
	RECT fillRect3 = {150, 150, 200, 200};

	HBITMAP hbm = CreateDIBSection(hdc, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, (void**)&pbData, NULL, 0);
	SelectObject(hdc, hbm);

	HBRUSH		backBrush = (HBRUSH)GetStockObject(DC_BRUSH);
	
	TextOut(hdc, bmi.biWidth/2, bmi.biHeight/2, _T("Hello, world!"), 13);	// Place string in centre of screen

	SetDCBrushColor(hdc, RGB(0, 200, 240));								// create square
	FillRect(hdc, &fillRect1, backBrush);
	SetDCBrushColor(hdc, RGB(180, 130, 130));							// create square
	FillRect(hdc, &fillRect2, backBrush);
	SetDCBrushColor(hdc, RGB(0, 120, 242));								// create square
	FillRect(hdc, &fillRect3, backBrush);

	Ellipse(hdc, 200, 200,300,300);										// create ellipse

	theFrame->GetBytes((void**)&pbDestData);							// get frame buffer pointer
	memcpy(pbDestData, pbData, bmi.biSizeImage); 
	DeleteObject(SelectObject(hdc, hbm));								// delete attached GDI object
}

