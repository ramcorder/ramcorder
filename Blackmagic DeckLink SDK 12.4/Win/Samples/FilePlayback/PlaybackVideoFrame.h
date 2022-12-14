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

#pragma once

#include <atomic>
#include "mfidl.h"
#include "mfreadwrite.h"
#include "DeckLinkAPI_h.h"

class PlaybackVideoFrame : public IDeckLinkVideoFrame
{
public:
	PlaybackVideoFrame(CComPtr<IMFSample> readSample, int64_t streamTimestamp, long frameWidth, long frameHeight, long defaultStride, BMDPixelFormat pixelFormat);
	virtual ~PlaybackVideoFrame();

	// IDeckLinkVideoFrame interface
	virtual long			__stdcall	GetWidth(void) override { return m_frameWidth; }
	virtual long			__stdcall	GetHeight(void) override { return m_frameHeight; }
	virtual long			__stdcall	GetRowBytes(void) override { return m_frameRowBytes; }
	virtual BMDPixelFormat	__stdcall	GetPixelFormat(void) override { return m_pixelFormat; }
	virtual BMDFrameFlags	__stdcall	GetFlags(void) override { return m_frameFlags; }
	virtual HRESULT			__stdcall	GetBytes(void **buffer) override;

	virtual HRESULT			__stdcall	GetTimecode(BMDTimecodeFormat /*format*/, IDeckLinkTimecode** /*timecode*/) override { return E_NOTIMPL; }
	virtual HRESULT			__stdcall	GetAncillaryData(IDeckLinkVideoFrameAncillary** /*ancillary*/) override { return E_NOTIMPL; }

	// IUnknown interface
	virtual HRESULT			__stdcall	QueryInterface(REFIID riid, void** ppv) override;
	virtual ULONG			__stdcall	AddRef(void) override;
	virtual ULONG			__stdcall	Release(void) override;

	// Other methods
	BMDTimeValue						GetStreamTime(BMDTimeScale timeScale);

private:
	std::atomic<ULONG>		m_refCount;

	long					m_frameWidth;
	long					m_frameHeight;
	long					m_frameRowBytes;
	BMDFrameFlags			m_frameFlags;
	BMDPixelFormat			m_pixelFormat;

	int64_t					m_streamTimestamp;
	BYTE*					m_lockedBuffer;
	bool					m_bufferIsLocked;

	CComPtr<IMFSample>		m_readSample;
	CComPtr<IMFMediaBuffer>	m_readBuffer;
	CComQIPtr<IMF2DBuffer>	m_read2DBuffer;
};
