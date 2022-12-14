/* -LICENSE-START-
 ** Copyright (c) 2014 Blackmagic Design
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

#include <atomic>
#include "platform.h"

// Video mode parameters
const BMDDisplayMode      kDisplayMode = bmdModeHD1080i50;
const BMDVideoOutputFlags kOutputFlag  = bmdVideoOutputVANC;
const BMDPixelFormat      kPixelFormat = bmdFormat10BitYUV;

// Frame parameters
const INT32_UNSIGNED kFrameDuration = 1000;
const INT32_UNSIGNED kTimeScale = 25000;
const INT32_UNSIGNED kFrameWidth = 1920;
const INT32_UNSIGNED kFrameHeight = 1080;
const INT32_UNSIGNED kRowBytes = 5120;

// 10-bit YUV blue pixels
const INT32_UNSIGNED kBlueData[4] = { 0x40aa298, 0x2a8a62a8, 0x298aa040, 0x2a8102a8 };

// Studio Camera control packet:
// Set dynamic range to film.
// See Studio Camera manual for more information on protocol.
const INT8_UNSIGNED kSDIRemoteControlData[9] = { 0x00, 0x07, 0x00, 0x00, 0x01, 0x07, 0x01, 0x00, 0x00 };

// Data Identifier 
const INT8_UNSIGNED kSDIRemoteControlDID = 0x51;

// Secondary Data Identifier
const INT8_UNSIGNED kSDIRemoteControlSDID = 0x53;

// Define VANC line for camera control
const INT32_UNSIGNED kSDIRemoteControlLine = 16;

// Keep track of the number of scheduled frames
INT32_UNSIGNED gTotalFramesScheduled = 0;

class OutputCallback: public IDeckLinkVideoOutputCallback
{
public:
	OutputCallback(IDeckLinkOutput* deckLinkOutput) : m_refCount(1)
	{
		m_deckLinkOutput = deckLinkOutput;
		m_deckLinkOutput->AddRef();
	}

	HRESULT	STDMETHODCALLTYPE ScheduledFrameCompleted(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
	{
		// When a video frame completes,reschedule another frame
		m_deckLinkOutput->ScheduleVideoFrame(completedFrame, gTotalFramesScheduled*kFrameDuration, kFrameDuration, kTimeScale);
		gTotalFramesScheduled++;
		return S_OK;
	}
	
	HRESULT	STDMETHODCALLTYPE ScheduledPlaybackHasStopped(void)
	{
		return S_OK;
	}
	// IUnknown needs only a dummy implementation
	HRESULT	STDMETHODCALLTYPE QueryInterface (REFIID iid, LPVOID *ppv)
	{
		return E_NOINTERFACE;
	}
	
	ULONG STDMETHODCALLTYPE AddRef()
	{
		return ++m_refCount;
	}
	
	ULONG STDMETHODCALLTYPE Release()
	{
		ULONG newRefValue = --m_refCount;

		if (newRefValue == 0)
			delete this;

		return newRefValue;
	}

private:
	IDeckLinkOutput*	m_deckLinkOutput;
	std::atomic<ULONG>	m_refCount;

	virtual ~OutputCallback(void)
	{
		m_deckLinkOutput->Release();
	}
};

class RemoteControlAncillaryPacket: public IDeckLinkAncillaryPacket
{
public:
	RemoteControlAncillaryPacket()
	{
		m_refCount = 1;
	}
	
	~RemoteControlAncillaryPacket()
	{
	}
	
	// IDeckLinkAncillaryPacket
	HRESULT STDMETHODCALLTYPE GetBytes(BMDAncillaryPacketFormat format, const void** data, INT32_UNSIGNED* size)
	{
		if (format != bmdAncillaryPacketFormatUInt8)
		{
			// In this example we only implement our packets with 8-bit data. This is fine because DeckLink accepts
			// whatever format we can supply and, if required, converts it.
			return E_NOTIMPL;
		}
		if (size) // Optional
			*size = (INT32_UNSIGNED)sizeof(kSDIRemoteControlData) / sizeof(*kSDIRemoteControlData);
		if (data) // Optional
			*data = (void*)kSDIRemoteControlData;
		return S_OK;
	}
	
	// IDeckLinkAncillaryPacket
	INT8_UNSIGNED STDMETHODCALLTYPE GetDID(void)
	{
		return kSDIRemoteControlDID;
	}
	
	// IDeckLinkAncillaryPacket
	INT8_UNSIGNED STDMETHODCALLTYPE GetSDID(void)
	{
		return kSDIRemoteControlSDID;
	}
	
	// IDeckLinkAncillaryPacket
	INT32_UNSIGNED STDMETHODCALLTYPE GetLineNumber(void)
	{
		// Returning zero here tells DeckLink to attempt to assume the line for known DIDs/SDIDs or otherwise place the
		// packet on the initial VANC lines of a frame. In this example we know the line for this mode, so let's use it.
		return kSDIRemoteControlLine;
	}
	
	// IDeckLinkAncillaryPacket
	INT8_UNSIGNED STDMETHODCALLTYPE GetDataStreamIndex(void)
	{
		return 0;
	}
	
	// IUnknown
	HRESULT	STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv)
	{
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	
	// IUnknown
	ULONG STDMETHODCALLTYPE AddRef()
	{
		return ++m_refCount;
	}
	
	// IUnknown
	ULONG STDMETHODCALLTYPE Release()
	{
		ULONG newRefValue = --m_refCount;
		
		if (newRefValue == 0)
			delete this;
		
		return newRefValue;
	}
	
private:
	std::atomic<ULONG>	m_refCount;
};

static void FillBlue(IDeckLinkMutableVideoFrame* theFrame)
{
	INT32_UNSIGNED* nextWord;
	INT32_UNSIGNED  wordsRemaining;
	
	theFrame->GetBytes((void**)&nextWord);
	wordsRemaining = (kRowBytes * kFrameHeight) / 4;
	
	while (wordsRemaining > 0)
	{
		*(nextWord++) = kBlueData[0];
		*(nextWord++) = kBlueData[1];
		*(nextWord++) = kBlueData[2];
		*(nextWord++) = kBlueData[3];
		wordsRemaining = wordsRemaining - 4;
	}
}

static IDeckLinkMutableVideoFrame* CreateFrame(IDeckLinkOutput* deckLinkOutput)
{
	HRESULT									result;
	IDeckLinkMutableVideoFrame*				frame = NULL;
	IDeckLinkVideoFrameAncillaryPackets*	ancillaryPackets = NULL;
	IDeckLinkAncillaryPacket*				packet = NULL;
	
	result = deckLinkOutput->CreateVideoFrame(kFrameWidth, kFrameHeight, kRowBytes, kPixelFormat, bmdFrameFlagDefault, &frame);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not create a video frame - result = %08x\n", result);
		goto bail;
	}
	FillBlue(frame);
	result = frame->QueryInterface(IID_IDeckLinkVideoFrameAncillaryPackets, (void**)&ancillaryPackets);
	if(result != S_OK)
	{
		fprintf(stderr, "Could not get frame's ancillary packet store - result = %08x\n", result);
		goto bail;
	}
	packet = new RemoteControlAncillaryPacket;
	result = ancillaryPackets->AttachPacket(packet);
	if (result != S_OK)
	{
		fprintf(stderr, "Could not attach packet to VANC - result = %08x\n", result);
		packet->Release(); // IDeckLinkVideoFrameAncillaryPackets didn't take ownership of the packet object
	}
bail:
	if (ancillaryPackets != NULL)
		ancillaryPackets->Release();
	return frame;
}


int main(int argc, const char * argv[])
{
	
	IDeckLinkIterator*      deckLinkIterator = NULL;
	IDeckLink*              deckLink         = NULL;
	IDeckLinkOutput*        deckLinkOutput   = NULL;
	OutputCallback*         outputCallback   = NULL;
	IDeckLinkVideoFrame*    videoFrameBlue   = NULL;
	HRESULT                 result;
	
	Initialize();
	
	// Create an IDeckLinkIterator object to enumerate all DeckLink cards in the system
	result = GetDeckLinkIterator(&deckLinkIterator);
	if(result != S_OK)
	{
		fprintf(stderr, "A DeckLink iterator could not be created.  The DeckLink drivers may not be installed.\n");
		goto bail;
	}
	
	// Obtain the first DeckLink device
	result = deckLinkIterator->Next(&deckLink);
	if(result != S_OK)
	{
		fprintf(stderr, "Could not find DeckLink device - result = %08x\n", result);
		goto bail;
	}
	
	// Obtain the output interface for the DeckLink device
	result = deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&deckLinkOutput);
	if(result != S_OK)
	{
		fprintf(stderr, "Could not obtain the IDeckLinkInput interface - result = %08x\n", result);
		goto bail;
	}
	
	// Create an instance of output callback
	outputCallback = new OutputCallback(deckLinkOutput);
	if(outputCallback == NULL)
	{
		fprintf(stderr, "Could not create output callback object\n");
		goto bail;
	}
	
	// Set the callback object to the DeckLink device's output interface
	result = deckLinkOutput->SetScheduledFrameCompletionCallback(outputCallback);
	if(result != S_OK)
	{
		fprintf(stderr, "Could not set callback - result = %08x\n", result);
		goto bail;
	}
	
	// Enable video output
	result = deckLinkOutput->EnableVideoOutput(kDisplayMode, kOutputFlag);
	if(result != S_OK)
	{
		fprintf(stderr, "Could not enable video output - result = %08x\n", result);
		goto bail;
	}
	
	// Create a frame with defined format
	videoFrameBlue = CreateFrame(deckLinkOutput);
	
	// Schedule a blue frame 3 times
	for(int i = 0; i < 3; i++)
	{
		result = deckLinkOutput->ScheduleVideoFrame(videoFrameBlue, gTotalFramesScheduled*kFrameDuration, kFrameDuration, kTimeScale);
		if(result != S_OK)
		{
			fprintf(stderr, "Could not schedule video frame - result = %08x\n", result);
			goto bail;
		}
		gTotalFramesScheduled ++;
	}
	
	// Start
	result = deckLinkOutput->StartScheduledPlayback(0, kTimeScale, 1.0);
	if(result != S_OK)
	{
		fprintf(stderr, "Could not start - result = %08x\n", result);
		goto bail;
	}
	
	// Wait until user presses Enter
	printf("Monitoring... Press <RETURN> to exit\n");
	
	getchar();
	
	printf("Exiting.\n");
	
	// Stop capture
	result = deckLinkOutput->StopScheduledPlayback(0, NULL, 0);
	
	// Disable the video input interface
	result = deckLinkOutput->DisableVideoOutput();
	
	// Release resources
bail:
	
	// Release the video input interface
	if(deckLinkOutput != NULL)
		deckLinkOutput->Release();
	
	// Release the Decklink object
	if(deckLink != NULL)
		deckLink->Release();
	
	// Release the DeckLink iterator
	if(deckLinkIterator != NULL)
		deckLinkIterator->Release();
	
	// Release the videoframe object
	if(videoFrameBlue != NULL)
		videoFrameBlue->Release();
	
	// Release the outputCallback callback object
	if(outputCallback != NULL)
		outputCallback->Release();
	
	return(result == S_OK) ? 0 : 1;
}

