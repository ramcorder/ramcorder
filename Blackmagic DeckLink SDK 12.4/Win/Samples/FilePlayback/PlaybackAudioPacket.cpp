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
#include "PlaybackAudioPacket.h"

PlaybackAudioPacket::PlaybackAudioPacket(CComPtr<IMFSample> readSample, int64_t streamTimestamp, BMDAudioSampleType audioSampleType, uint32_t audioChannelCount) :
	m_refCount(1),
	m_timestamp(streamTimestamp),
	m_bufferIsLocked(false),
	m_audioSampleCount(0)
{
	HRESULT hr = readSample->ConvertToContiguousBuffer(&m_readBuffer);
	if (hr == S_OK)
	{
		DWORD packetByteCount;
		hr = m_readBuffer->Lock(&m_lockedBuffer, nullptr, (DWORD*)&packetByteCount);
		if (hr == S_OK)
		{
			m_bufferIsLocked = true;
			m_audioSampleCount = (uint32_t)(packetByteCount / (audioChannelCount * audioSampleType / 8));
		}
	}
}

PlaybackAudioPacket::~PlaybackAudioPacket()
{
	if (m_bufferIsLocked)
	{
		(void)m_readBuffer->Unlock();
	}
}

HRESULT PlaybackAudioPacket::GetBytes(void** buffer)
{
	if (m_bufferIsLocked)
	{
		*buffer = m_lockedBuffer;
		return S_OK;
	}
	else
	{
		*buffer = nullptr;
		return E_FAIL;
	}
}

BMDTimeValue PlaybackAudioPacket::GetStreamTime(BMDTimeScale timeScale)
{
	return (BMDTimeValue)(m_timestamp / (kMFTimescale / timeScale));
}

HRESULT __stdcall PlaybackAudioPacket::QueryInterface(REFIID riid, void** ppv)
{
	*ppv = NULL;
	return E_INVALIDARG;
}

ULONG __stdcall PlaybackAudioPacket::AddRef(void)
{
	return ++m_refCount;
}

ULONG __stdcall PlaybackAudioPacket::Release(void)
{
	ULONG newRefCount = --m_refCount;
	if (newRefCount == 0)
		delete this;

	return newRefCount;
}
