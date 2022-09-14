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

#include "DecoderMF.h"

// Hrm, MFuuid.lib doesn't seem to have this. Define it here.
EXTERN_C const GUID DECLSPEC_SELECTANY CLSID_CMSH264DecoderMFT =
	{ 0x62CE7E72, 0x4C71, 0x4d20, 0xB1, 0x5D, 0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D };

// Thread context: Dialog window thread
DecoderMF::DecoderMF()
	: m_outputSample(NULL),
	m_previewWindow(NULL),
	m_decoderThread(NULL),
	m_decoderThreadRunning(false),
	m_decoderThreadEvent(NULL)
{
	HRESULT				hr;
	IMFMediaType*		mediaType;

	m_previewConfigured = true;

	hr = CoCreateInstance(CLSID_CMSH264DecoderMFT, NULL, CLSCTX_INPROC_SERVER, IID_IMFTransform, (void**)&m_h264Decoder);

	if (FAILED(hr))
		throw 1;

	// Create and set input Media Type
	if (! CreateInputMediaType(&mediaType))
	{
		m_h264Decoder->Release();
		throw 1;
	}

	hr = m_h264Decoder->SetInputType(0, mediaType, 0);
	mediaType->Release();
	if (FAILED(hr))
	{
		m_h264Decoder->Release();
		throw 1;
	}

	// Set output type
	hr = m_h264Decoder->GetOutputAvailableType(0, 0, &mediaType);
	if (FAILED(hr))
	{
		m_h264Decoder->Release();
		throw 1;
	}

	hr = m_h264Decoder->SetOutputType(0, mediaType, 0);
	mediaType->Release();
	if (FAILED(hr))
	{
		m_h264Decoder->Release();
		throw 1;
	}

	// Some sanity checks
	DWORD numInputStreams, numOutputStreams;
	hr = m_h264Decoder->GetStreamCount(&numInputStreams, &numOutputStreams);
	if (FAILED(hr))
	{
		m_h264Decoder->Release();
		throw 1;
	}

	if (numInputStreams != 1 || numOutputStreams != 1)
	{
		// This would be unexpected...
		m_h264Decoder->Release();
		throw 1;
	}

	MFT_OUTPUT_STREAM_INFO outputStreamInfo;
	hr = m_h264Decoder->GetOutputStreamInfo(0, &outputStreamInfo);
	if (FAILED(hr))
	{
		m_h264Decoder->Release();
		throw 1;
	}

	if ((outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) ||
		!(outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE) ||
		!(outputStreamInfo.dwFlags & MFT_OUTPUT_STREAM_WHOLE_SAMPLES))
	{
		// This would be unexpected...
		m_h264Decoder->Release();
		throw 1;
	}

	// Great - if we got here, it means Media Foundation is all OK. Lets
	// start the decoder thread.

	InitializeCriticalSection(&m_criticalSection);
	m_decoderThreadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_decoderThreadRunning = true;
	m_decoderThread = CreateThread(NULL, 0, DecoderThreadFunction, this, 0, NULL);
}

// Thread context: Dialog window thread
DecoderMF::~DecoderMF()
{
	// Delete thread:
	m_decoderThreadRunning = false;
	SetEvent(m_decoderThreadEvent);
	WaitForSingleObject(m_decoderThread, INFINITE);
	CloseHandle(m_decoderThread);
	CloseHandle(m_decoderThreadEvent);

	// Now that the thread is done, we can release
	// the Media Foundation objects.
	m_h264Decoder->Release();

	if (m_outputSample != NULL)
	{
		m_outputSample->Release();
	}
}

// Called by the dialog when a new preview window becomes
// available (or the old preview window is closed (called with NULL)
//
// Thread context: Dialog window thread
void DecoderMF::SetPreviewWindow(PreviewWindow* previewWindow)
{
	EnterCriticalSection(&m_criticalSection);
		m_previewConfigured = (previewWindow == NULL) || (previewWindow == m_previewWindow && m_previewConfigured);
		m_previewWindow = previewWindow;
	LeaveCriticalSection(&m_criticalSection);
}

bool DecoderMF::HandleNALPacket(IBMDStreamingH264NALPacket *nalPacket)
{
	// Don't do anything if there's no thread to pick up the results..
	if (! m_decoderThreadRunning)
		return false;

	nalPacket->AddRef();

	EnterCriticalSection(&m_criticalSection);
		// TODO: you may want to check this queue isn't getting too full.
		m_nalQueue.push_back(nalPacket);
	LeaveCriticalSection(&m_criticalSection);

	SetEvent(m_decoderThreadEvent);

	return true;
}

// Called by the constructor only (for setting up the IMFTransform)
//
// Thread context: Dialog window thread
bool DecoderMF::CreateInputMediaType(IMFMediaType** mediaType)
{
	bool			ret = false;
	HRESULT			hr;
	IMFMediaType*	newMediaType = NULL;

	if (mediaType == NULL)
		return false;

	hr = MFCreateMediaType(&newMediaType);
    if (FAILED(hr))
		goto bail;

	hr = newMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (FAILED(hr))
		goto bail;

	hr = newMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
	if (FAILED(hr))
		goto bail;

	ret = true;

bail:
	if (ret == false)
	{
		if (newMediaType != NULL)
		{
			newMediaType->Release();
			newMediaType = NULL;
		}
	}
	else
	{
		*mediaType = newMediaType;
	}

	return ret;
}

// Process the incomming NAL from the queue: wraps it up into a
// IMFMediaSample, sends it to the decoder.
//
// Thread context: decoder thread
bool DecoderMF::DoProcessInputNAL(IBMDStreamingH264NALPacket* nalPacket)
{
	bool				ret = false;
	HRESULT				hr;
	IMFMediaBuffer*		newBuffer = NULL;
	BYTE*				newBufferPtr;
	void*				nalPacketPtr;
	//
	IMFSample*			newSample = NULL;
	ULONGLONG			nalPresentationTime;
	const BYTE			nalPrefix[] = {0, 0, 0, 1};

	// Get a pointer to the NAL data
	hr = nalPacket->GetBytes(&nalPacketPtr);
	if (FAILED(hr))
		goto bail;

	// Create the MF media buffer (+ 4 bytes for the NAL Prefix (0x00 0x00 0x00 0x01)
	// which MF requires.
	hr = MFCreateMemoryBuffer(nalPacket->GetPayloadSize()+4, &newBuffer);
	if (FAILED(hr))
		goto bail;

	// Lock the MF media buffer
	hr = newBuffer->Lock(&newBufferPtr, NULL, NULL);
	if (FAILED(hr))
		goto bail;

	// Copy the prefix and the data
	memcpy(newBufferPtr, nalPrefix, 4);
	memcpy(newBufferPtr+4, nalPacketPtr, nalPacket->GetPayloadSize());

	// Unlock the MF media buffer
	hr = newBuffer->Unlock();
	if (FAILED(hr))
		goto bail;

	// Update the current length of the MF media buffer
	hr = newBuffer->SetCurrentLength(nalPacket->GetPayloadSize()+4);
	if (FAILED(hr))
		goto bail;

	// We now have a IMFMediaBuffer with the contents of the NAL
	// packet. We now construct a IMFSample with the buffer
	hr = MFCreateSample(&newSample);
	if (FAILED(hr))
		goto bail;

	hr = newSample->AddBuffer(newBuffer);
	if (FAILED(hr))
		goto bail;

	// Get the presentation (display) time in 100-nanosecond units
	// TODO: this is pretty meaningless without setting the start time.
	hr = nalPacket->GetDisplayTime(1000 * 1000 * 10, &nalPresentationTime);
	if (FAILED(hr))
		goto bail;

	// Set presentation time on the sample
	hr = newSample->SetSampleTime(nalPresentationTime);
	if (FAILED(hr))
		goto bail;

	// Now parse it to the decoder
	for (;;)
	{
		hr = m_h264Decoder->ProcessInput(0, newSample, 0);
		if (hr == S_OK)
			break;
		if (hr != MF_E_NOTACCEPTING || DoProcessOutput() == false)
			goto bail;
	}

	ret = true;

bail:
	if (newBuffer != NULL)
		newBuffer->Release();

	if (newSample != NULL)
		newSample->Release();

	return ret;
}

// Process any pending output from the decoder (until all output is
// processed).
//
// Thread context: decoder thread
bool DecoderMF::DoProcessOutput()
{
	bool						ret = false;
	HRESULT						hr;
	MFT_OUTPUT_DATA_BUFFER		mftDataBuffer;
	DWORD						mftStatus;
	bool						moreOutput;

	if (m_outputSample == NULL)
	{
		if (! CreateOutputSample())
			return false;
	}

	do
	{
		// Since we could be looping inside this method for a while,
		// if a whole stack of frames arrive at once, we want to exit
		// if the thread has been asked to die. So check on each
		// iteration of the loop.
		if (! m_decoderThreadRunning)
			return true;

		moreOutput = false;

		mftDataBuffer.dwStreamID = 0;
		mftDataBuffer.pSample = m_outputSample;
		mftDataBuffer.dwStatus = 0;
		mftDataBuffer.pEvents = NULL;
		mftStatus = 0;

		// Looks like we have to reset the sample before use:
		IMFMediaBuffer* mediaBuffer;
		hr = m_outputSample->GetBufferByIndex(0, &mediaBuffer);
		if (FAILED(hr))
			goto bail;

		hr = mediaBuffer->SetCurrentLength(0);
		if (FAILED(hr))
			goto bail;

		mediaBuffer->Release();

		hr = m_h264Decoder->ProcessOutput(0, 1, &mftDataBuffer, &mftStatus);

		// Check return code
		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
			break;
		EnterCriticalSection(&m_criticalSection);
		if (hr == MF_E_TRANSFORM_STREAM_CHANGE || !m_previewConfigured)
		{
			// If the output format has changed, we need to handle
			// the stream change. This will happen after the first
			// few packets have been delivered.
			moreOutput = HandleStreamChange();
			LeaveCriticalSection(&m_criticalSection);
			if (!moreOutput)
				goto bail;
			continue;
		}
		LeaveCriticalSection(&m_criticalSection);
		if (FAILED(hr))
			goto bail;

		if (mftDataBuffer.dwStatus == MFT_OUTPUT_DATA_BUFFER_INCOMPLETE)
			moreOutput = true;

		// Process each event:
		if (mftDataBuffer.pEvents != NULL)
		{
			DWORD numElements;
			hr = mftDataBuffer.pEvents->GetElementCount(&numElements);
			if (SUCCEEDED(hr))
			{
				for (DWORD i = 0; i < numElements; i++)
				{
					IUnknown* iunk = NULL;

					hr = mftDataBuffer.pEvents->GetElement(i, &iunk);
					if (SUCCEEDED(hr))
					{
						IMFMediaEvent* mediaEvent = NULL;
						hr = iunk->QueryInterface(IID_IMFMediaEvent, (void**)&mediaEvent);

						if (SUCCEEDED(hr))
						{
							OutputDebugString(_T("FIXME: process event!\n"));

							mediaEvent->Release();
						}

						iunk->Release();
					}
				}
			}

			mftDataBuffer.pEvents = NULL;
		}

		// Process sample:
		if (mftDataBuffer.pSample != NULL)
		{
			IMFMediaBuffer* mediaBuffer;
			hr = mftDataBuffer.pSample->GetBufferByIndex(0, &mediaBuffer);
			if (FAILED(hr))
				goto bail;

			EnterCriticalSection(&m_criticalSection);

				if (m_previewWindow != NULL && m_previewConfigured)
					m_previewWindow->DrawFrame(mediaBuffer);

			LeaveCriticalSection(&m_criticalSection);

			mediaBuffer->Release();
		}
	} while(moreOutput);

	ret = true;

bail:
	if (ret == false)
		OutputDebugString(_T("ERROR: failed to process output...\n"));

	return ret;
}

// This is called each time the stream format has changed, and
// deletes and then re-creates the output sample (IMFSample*),
// according to the required format.
//
// Thread context: decoder thread
bool DecoderMF::CreateOutputSample()
{
	bool				ret = false;
	HRESULT				hr;
	MFT_OUTPUT_STREAM_INFO outputStreamInfo;
	IMFMediaBuffer*		mediaBuffer = NULL;

	if (m_outputSample != NULL)
	{
		m_outputSample->Release();
		m_outputSample = NULL;
	}

	hr = MFCreateSample(&m_outputSample);
	if (FAILED(hr))
		goto bail;

	hr = m_h264Decoder->GetOutputStreamInfo(0, &outputStreamInfo);
	if (FAILED(hr))
		goto bail;

	hr = MFCreateAlignedMemoryBuffer(outputStreamInfo.cbSize, MF_16_BYTE_ALIGNMENT, &mediaBuffer);
	if (FAILED(hr))
		goto bail;

	hr = m_outputSample->AddBuffer(mediaBuffer);
	if (FAILED(hr))
		goto bail;

	ret = true;
	
bail:
	if (ret == false)
	{
		if (m_outputSample != NULL)
		{
			m_outputSample->Release();
			m_outputSample = NULL;
		}
	}

	if (mediaBuffer)
		mediaBuffer->Release();

	return ret;
}

// This is called from the DoProcessOutput method if the stream format
// has changed.
//
// Thread context: decoder thread
bool DecoderMF::HandleStreamChange()
{
	bool			ret = false;
	HRESULT			hr;
	DWORD			numOutputStreams;
	IMFMediaType*	mediaType = NULL;
	AM_MEDIA_TYPE*  mformt = NULL;

	hr = m_h264Decoder->GetStreamCount(NULL, &numOutputStreams);
	if (FAILED(hr))
		goto bail;

	if (numOutputStreams != 1)
		goto bail;

	hr = S_OK;
	int idx = 0;
	while (hr == S_OK)
	{
		hr = m_h264Decoder->GetOutputAvailableType(0, idx, &mediaType);
		if (FAILED(hr))
			goto bail;

		mediaType->GetRepresentation(FORMAT_MFVideoFormat , (LPVOID*)&mformt);
	    MFVIDEOFORMAT* z = (MFVIDEOFORMAT*)mformt->pbFormat;
		unsigned int format = z->surfaceInfo.Format;
		mediaType->FreeRepresentation(FORMAT_MFVideoFormat ,(LPVOID)mformt);

		if (format == '2YUY')
			break;

		++idx;
	}

	hr = m_h264Decoder->SetOutputType(0, mediaType, 0);
	if (FAILED(hr))
		goto bail;

	if (! CreateOutputSample())
		goto bail;

	if (m_previewWindow != NULL)
	{
		if (! m_previewWindow->SetMediaType(mediaType))
			goto bail;
		m_previewConfigured = true;
	}

	ret = true;

bail:
	if (mediaType != NULL)
		mediaType->Release();

	return ret;
}

DWORD DecoderMF::DecoderThreadFunction(void* arg)
{
	DecoderMF* self = (DecoderMF*)arg;
	self->DecoderThread();

	return 0;
}

void DecoderMF::DecoderThread()
{
	while (m_decoderThreadRunning)
	{
		bool haveMoreInput = false;
		IBMDStreamingH264NALPacket* inputNal = NULL;

		EnterCriticalSection(&m_criticalSection);

			// If we've got data in the queue, grab it
			if ( !m_nalQueue.empty())
			{
				inputNal = m_nalQueue.front();
				m_nalQueue.pop_front();

				// And update haveMoreInput so we don't sleep if there's more
				// data to process
				haveMoreInput = ! m_nalQueue.empty();
			}

		LeaveCriticalSection(&m_criticalSection);

		if (inputNal != NULL)
		{
			// Process the input NAL
			if (! DoProcessInputNAL(inputNal))
			{
				OutputDebugString(_T("ERROR: failed to process input NAL\n"));
			}

			// Release it:
			inputNal->Release();
		}

		// Now process any output that may have been generated. (once we've
		// processed all input)
		if (! haveMoreInput)
		{
			if (! DoProcessOutput())
			{
				OutputDebugString(_T("ERROR: failed to process output from MF decoder\n"));
			}
		}

		if (! haveMoreInput)
			WaitForSingleObject(m_decoderThreadEvent, INFINITE);
	}

	return;
}

