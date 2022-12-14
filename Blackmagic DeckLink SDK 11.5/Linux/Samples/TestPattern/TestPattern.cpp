/* -LICENSE-START-
** Copyright (c) 2018 Blackmagic Design
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <libgen.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "TestPattern.h"
#include "VideoFrame3D.h"

pthread_mutex_t			sleepMutex;
pthread_cond_t			sleepCond;
bool					do_exit = false;

const unsigned long		kAudioWaterlevel = 48000;

void sigfunc(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		do_exit = true;
	}
	pthread_cond_signal(&sleepCond);
}

int main(int argc, char *argv[])
{
	int				exitStatus = 1;
	TestPattern*	generator = NULL;

	pthread_mutex_init(&sleepMutex, NULL);
	pthread_cond_init(&sleepCond, NULL);

	signal(SIGINT, sigfunc);
	signal(SIGTERM, sigfunc);
	signal(SIGHUP, sigfunc);

	BMDConfig config;
	if (!config.ParseArguments(argc, argv))
	{
		config.DisplayUsage(exitStatus);
		goto bail;
	}

	generator = new TestPattern(&config);

	if (!generator->Run())
		goto bail;

	// All Okay.
	exitStatus = 0;

bail:
	if (generator)
	{
		generator->Release();
		generator = NULL;
	}
	return exitStatus;
}

TestPattern::~TestPattern()
{
}

TestPattern::TestPattern(BMDConfig *config) :
	m_refCount(1),
	m_config(config),
	m_running(false),
	m_deckLink(),
	m_deckLinkOutput(),
	m_deckLinkConfiguration(),
	m_displayMode(),
	m_videoFrameBlack(),
	m_videoFrameBars(),
	m_outputSignal(kOutputSignalDrop),
	m_audioBuffer(),
	m_audioSampleRate(bmdAudioSampleRate48kHz)
{
}

bool TestPattern::Run()
{
	HRESULT							result;
	int								idx;
	bool							success = false;

	IDeckLinkIterator*				deckLinkIterator = NULL;
	IDeckLinkDisplayModeIterator*	displayModeIterator = NULL;
	char*							displayModeName = NULL;

	// Get the DeckLink device
	deckLinkIterator = CreateDeckLinkIteratorInstance();
	if (!deckLinkIterator)
	{
		fprintf(stderr, "This application requires the DeckLink drivers installed.\n");
		goto bail;
	}

	idx = m_config->m_deckLinkIndex;

	while ((result = deckLinkIterator->Next(&m_deckLink)) == S_OK)
	{
		if (idx == 0)
			break;
		--idx;

		m_deckLink->Release();
	}

	if (result != S_OK || m_deckLink == NULL)
	{
		fprintf(stderr, "Unable to get DeckLink device %u\n", m_config->m_deckLinkIndex);
		goto bail;
	}

	// Get the output (display) interface of the DeckLink device
	if (m_deckLink->QueryInterface(IID_IDeckLinkOutput, (void**)&m_deckLinkOutput) != S_OK)
		goto bail;

	// Get the configuration interface of the DeckLink device
	if (m_deckLink->QueryInterface(IID_IDeckLinkConfiguration, (void**)&m_deckLinkConfiguration) != S_OK)
		goto bail;

	// Get the display mode
	idx = m_config->m_displayModeIndex;

	result = m_deckLinkOutput->GetDisplayModeIterator(&displayModeIterator);
	if (result != S_OK)
		goto bail;

	while ((result = displayModeIterator->Next(&m_displayMode)) == S_OK)
	{
		if (idx == 0)
			break;
		--idx;

		m_displayMode->Release();
	}

	if (result != S_OK || m_displayMode == NULL)
	{
		fprintf(stderr, "Unable to get display mode %d\n", m_config->m_displayModeIndex);
		goto bail;
	}

	// Get display mode name
	result = m_displayMode->GetName((const char**)&displayModeName);
	if (result != S_OK)
	{
		displayModeName = (char *)malloc(32);
		snprintf(displayModeName, 32, "[index %d]", m_config->m_displayModeIndex);
	}

	// Check for 3D support on display mode
	if ((m_config->m_outputFlags & bmdVideoOutputDualStream3D) && !(m_displayMode->GetFlags() & bmdDisplayModeSupports3D))
	{
		fprintf(stderr, "The display mode %s is not supported with 3D\n", displayModeName);
		goto bail;
	}

	m_config->DisplayConfiguration();

	// Provide this class as a delegate to the audio and video output interfaces
	m_deckLinkOutput->SetScheduledFrameCompletionCallback(this);
	m_deckLinkOutput->SetAudioCallback(this);

	success = true;

	// Start.
	while (!do_exit)
	{
		StartRunning();
		fprintf(stderr, "Starting playback\n");

		pthread_mutex_lock(&sleepMutex);
		pthread_cond_wait(&sleepCond, &sleepMutex);
		pthread_mutex_unlock(&sleepMutex);

		fprintf(stderr, "Stopping playback\n");
		StopRunning();
	}

	printf("\n");

bail:
	if (displayModeName != NULL)
		free(displayModeName);

	if (m_displayMode != NULL)
		m_displayMode->Release();

	if (displayModeIterator != NULL)
		displayModeIterator->Release();

	if (m_deckLinkConfiguration != NULL)
		m_deckLinkConfiguration->Release();

	if (m_deckLinkOutput != NULL)
		m_deckLinkOutput->Release();

	if (m_deckLink != NULL)
		m_deckLink->Release();

	if (deckLinkIterator != NULL)
		deckLinkIterator->Release();

	return success;
}

void TestPattern::StartRunning()
{
	HRESULT					result;
	unsigned long			audioSamplesPerFrame;
	IDeckLinkVideoFrame*	rightFrame;
	VideoFrame3D*			frame3D;

	m_frameWidth = m_displayMode->GetWidth();
	m_frameHeight = m_displayMode->GetHeight();
	m_displayMode->GetFrameRate(&m_frameDuration, &m_frameTimescale);

	// Calculate the number of frames per second, rounded up to the nearest integer.  For example, for NTSC (29.97 FPS), framesPerSecond == 30.
	m_framesPerSecond = (unsigned long)((m_frameTimescale + (m_frameDuration-1))  /  m_frameDuration);

	// Set the output to 444 if RGB mode is selected
	result = m_deckLinkConfiguration->SetFlag(bmdDeckLinkConfig444SDIVideoOutput, m_config->m_output444);
	// If a device without SDI output is used (eg Intensity Pro 4K), then SetFlags will return E_NOTIMPL
	if ((result != S_OK) && (result != E_NOTIMPL))
	{
		fprintf(stderr, "Failed to write to 444 output configuration flag\n");
		goto bail;
	}

	// Set the video output mode
	result = m_deckLinkOutput->EnableVideoOutput(m_displayMode->GetDisplayMode(), m_config->m_outputFlags);
	if (result != S_OK)
	{
		fprintf(stderr, "Failed to enable video output. Is another application using the card?\n");
		goto bail;
	}

	// Set the audio output mode
	result = m_deckLinkOutput->EnableAudioOutput(bmdAudioSampleRate48kHz, m_config->m_audioSampleDepth, m_config->m_audioChannels, bmdAudioOutputStreamContinuous);
	if (result != S_OK)
	{
		fprintf(stderr, "Failed to enable audio output\n");
		goto bail;
	}

	// Generate one second of audio
	m_audioBufferSampleLength = (unsigned long)((m_framesPerSecond * m_audioSampleRate * m_frameDuration) / m_frameTimescale);
	m_audioBuffer = valloc(m_audioBufferSampleLength * m_config->m_audioChannels * (m_config->m_audioSampleDepth / 8));

	if (m_audioBuffer == NULL)
	{
		fprintf(stderr, "Failed to allocate audio buffer memory\n");
		goto bail;
	}

	// Zero the buffer (interpreted as audio silence)
	memset(m_audioBuffer, 0x0, (m_audioBufferSampleLength * m_config->m_audioChannels * m_config->m_audioSampleDepth / 8));
	audioSamplesPerFrame = (unsigned long)((m_audioSampleRate * m_frameDuration) / m_frameTimescale);

	if (m_outputSignal == kOutputSignalPip)
		FillSine(m_audioBuffer, audioSamplesPerFrame, m_config->m_audioChannels, m_config->m_audioSampleDepth);
	else
		FillSine((void*)((unsigned long)m_audioBuffer + (audioSamplesPerFrame * m_config->m_audioChannels * m_config->m_audioSampleDepth / 8)), (m_audioBufferSampleLength - audioSamplesPerFrame), m_config->m_audioChannels, m_config->m_audioSampleDepth);

	// Generate a frame of black
	if (CreateFrame(&m_videoFrameBlack, FillBlack) != S_OK)
		goto bail;

	if (m_config->m_outputFlags & bmdVideoOutputDualStream3D)
	{
		frame3D = new VideoFrame3D(m_videoFrameBlack);
		m_videoFrameBlack->Release();
		m_videoFrameBlack = frame3D;
		frame3D = NULL;
	}

	// Generate a frame of colour bars
	if (CreateFrame(&m_videoFrameBars, FillForwardColourBars) != S_OK)
		goto bail;

	if (m_config->m_outputFlags & bmdVideoOutputDualStream3D)
	{
		if (CreateFrame(&rightFrame, FillReverseColourBars) != S_OK)
			goto bail;

		frame3D = new VideoFrame3D(m_videoFrameBars, rightFrame);
		m_videoFrameBars->Release();
		rightFrame->Release();
		m_videoFrameBars = frame3D;
		frame3D = NULL;
	}

	// Begin video preroll by scheduling a second of frames in hardware
	m_totalFramesScheduled = 0;
	m_totalFramesDropped = 0;
	m_totalFramesCompleted = 0;
	for (unsigned i = 0; i < m_framesPerSecond; i++)
		ScheduleNextFrame(true);

	// Begin audio preroll.  This will begin calling our audio callback, which will start the DeckLink output stream.
	m_audioBufferOffset = 0;
	if (m_deckLinkOutput->BeginAudioPreroll() != S_OK)
	{
		fprintf(stderr, "Failed to begin audio preroll\n");
		goto bail;
	}

	{
		std::lock_guard<std::mutex> guard(m_mutex);
		m_running = true;
	}
	
	return;

bail:
	// *** Error-handling code.  Cleanup any resources that were allocated. *** //
	StopRunning();
}

void TestPattern::StopRunning()
{
	// Stop the audio and video output streams immediately
	m_deckLinkOutput->StopScheduledPlayback(0, NULL, 0);

	{
		// Wait for scheduled playback to stop
		std::unique_lock<std::mutex> lock(m_mutex);
		m_stoppedCondition.wait(lock, [this]{ return m_running == false; });
	}

	m_deckLinkOutput->DisableAudioOutput();
	m_deckLinkOutput->DisableVideoOutput();

	if (m_videoFrameBlack != NULL)
		m_videoFrameBlack->Release();
	m_videoFrameBlack = NULL;

	if (m_videoFrameBars != NULL)
		m_videoFrameBars->Release();
	m_videoFrameBars = NULL;

	if (m_audioBuffer != NULL)
		free(m_audioBuffer);
	m_audioBuffer = NULL;
}

void TestPattern::ScheduleNextFrame(bool prerolling)
{
	if (prerolling == false)
	{
		// If not prerolling, make sure that playback is still active
		std::lock_guard<std::mutex> guard(m_mutex);
		if (m_running == false)
			return;
	}
	if (m_outputSignal == kOutputSignalPip)
	{
		if ((m_totalFramesScheduled % m_framesPerSecond) == 0)
		{
			// On each second, schedule a frame of bars
			if (m_deckLinkOutput->ScheduleVideoFrame(m_videoFrameBars, (m_totalFramesScheduled * m_frameDuration), m_frameDuration, m_frameTimescale) != S_OK)
				return;
		}
		else
		{
			// Schedue frames of black
			if (m_deckLinkOutput->ScheduleVideoFrame(m_videoFrameBlack, (m_totalFramesScheduled * m_frameDuration), m_frameDuration, m_frameTimescale) != S_OK)
				return;
		}
	}
	else
	{
		if ((m_totalFramesScheduled % m_framesPerSecond) == 0)
		{
			// On each second, schedule a frame of black
			if (m_deckLinkOutput->ScheduleVideoFrame(m_videoFrameBlack, (m_totalFramesScheduled * m_frameDuration), m_frameDuration, m_frameTimescale) != S_OK)
				return;
		}
		else
		{
			// Schedue frames of color bars
			if (m_deckLinkOutput->ScheduleVideoFrame(m_videoFrameBars, (m_totalFramesScheduled * m_frameDuration), m_frameDuration, m_frameTimescale) != S_OK)
				return;
		}
	}

	m_totalFramesScheduled += 1;
}

void TestPattern::WriteNextAudioSamples()
{
	unsigned int		bufferedSamples;

	// Try to maintain the number of audio samples buffered in the API at a specified waterlevel
	if ((m_deckLinkOutput->GetBufferedAudioSampleFrameCount(&bufferedSamples) == S_OK) && (bufferedSamples < kAudioWaterlevel))
	{
		unsigned int		samplesToEndOfBuffer;
		unsigned int		samplesToWrite;
		unsigned int		samplesWritten;

		samplesToEndOfBuffer = (m_audioBufferSampleLength - m_audioBufferOffset);
		samplesToWrite = (kAudioWaterlevel - bufferedSamples);
		if (samplesToWrite > samplesToEndOfBuffer)
			samplesToWrite = samplesToEndOfBuffer;

		if (m_deckLinkOutput->ScheduleAudioSamples((void*)((unsigned long)m_audioBuffer + (m_audioBufferOffset * m_config->m_audioChannels * m_config->m_audioSampleDepth / 8)), samplesToWrite, 0, 0, &samplesWritten) == S_OK)
		{
			m_audioBufferOffset = ((m_audioBufferOffset + samplesWritten) % m_audioBufferSampleLength);
		}
	}
}

HRESULT TestPattern::CreateFrame(IDeckLinkVideoFrame** frame, void (*fillFunc)(IDeckLinkVideoFrame*))
{
	HRESULT						result;
	int							bytesPerRow = GetRowBytes(m_config->m_pixelFormat, m_frameWidth);
	int							referenceBytesPerRow = GetRowBytes(bmdFormat8BitYUV, m_frameWidth);
	IDeckLinkMutableVideoFrame*	newFrame = NULL;
	IDeckLinkMutableVideoFrame*	referenceFrame = NULL;
	IDeckLinkVideoConversion*	frameConverter = NULL;

	*frame = NULL;

	result = m_deckLinkOutput->CreateVideoFrame(m_frameWidth, m_frameHeight, bytesPerRow, m_config->m_pixelFormat, bmdFrameFlagDefault, &newFrame);
	if (result != S_OK)
	{
		fprintf(stderr, "Failed to create video frame\n");
		goto bail;
	}

	if (m_config->m_pixelFormat == bmdFormat8BitYUV)
	{
		fillFunc(newFrame);
	}
	else
	{
		// Create a black frame in 8 bit YUV and convert to desired format
		result = m_deckLinkOutput->CreateVideoFrame(m_frameWidth, m_frameHeight, referenceBytesPerRow, bmdFormat8BitYUV, bmdFrameFlagDefault, &referenceFrame);
		if (result != S_OK)
		{
			fprintf(stderr, "Failed to create reference video frame\n");
			goto bail;
		}

		fillFunc(referenceFrame);

		frameConverter = CreateVideoConversionInstance();

		result = frameConverter->ConvertFrame(referenceFrame, newFrame);
		if (result != S_OK)
		{
			fprintf(stderr, "Failed to convert frame\n");
			goto bail;
		}
	}

	*frame = newFrame;
	newFrame = NULL;

bail:
	if (referenceFrame != NULL)
		referenceFrame->Release();

	if (frameConverter != NULL)
		frameConverter->Release();

	if (newFrame != NULL)
		newFrame->Release();

	return result;
}

void TestPattern::PrintStatusLine()
{
	printf("\rscheduled %-16lu completed %-16lu dropped %-16lu\r",
		m_totalFramesScheduled, m_totalFramesCompleted, m_totalFramesDropped);
}

/************************* DeckLink API Delegate Methods *****************************/


HRESULT TestPattern::QueryInterface(REFIID iid, LPVOID *ppv)
{
	*ppv = NULL;
	return E_NOINTERFACE;
}

ULONG TestPattern::AddRef()
{
	// gcc atomic operation builtin
	return __sync_add_and_fetch(&m_refCount, 1);
}

ULONG TestPattern::Release()
{
	// gcc atomic operation builtin
	ULONG newRefValue = __sync_sub_and_fetch(&m_refCount, 1);
	if (!newRefValue)
		delete this;
	return newRefValue;
}

HRESULT TestPattern::ScheduledFrameCompleted(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result)
{
	++m_totalFramesCompleted;
	PrintStatusLine();

	// When a video frame has been released by the API, schedule another video frame to be output
	ScheduleNextFrame(false);
	return S_OK;
}

HRESULT TestPattern::ScheduledPlaybackHasStopped()
{
	std::lock_guard<std::mutex> guard(m_mutex);
	m_running = false;
	m_stoppedCondition.notify_all();

	return S_OK;
}

HRESULT TestPattern::RenderAudioSamples(bool preroll)
{
	// Provide further audio samples to the DeckLink API until our preferred buffer waterlevel is reached
	WriteNextAudioSamples();

	if (preroll)
	{
		// Start audio and video output
		m_deckLinkOutput->StartScheduledPlayback(0, 100, 1.0);
	}

	return S_OK;
}

/*****************************************/

void FillSine(void* audioBuffer, unsigned long samplesToWrite, unsigned long channels, unsigned long sampleDepth)
{
	if (sampleDepth == 16)
	{
		short*		nextBuffer;

		nextBuffer = (short*)audioBuffer;
		for (unsigned i = 0; i < samplesToWrite; i++)
		{
			short		sample;

			sample = (short)(24576.0 * sin((i * 2.0 * M_PI) / 48.0));
			for (unsigned ch = 0; ch < channels; ch++)
				*(nextBuffer++) = sample;
		}
	}
	else if (sampleDepth == 32)
	{
		int*		nextBuffer;

		nextBuffer = (int*)audioBuffer;
		for (unsigned i = 0; i < samplesToWrite; i++)
		{
			int		sample;

			sample = (int)(1610612736.0 * sin((i * 2.0 * M_PI) / 48.0));
			for (unsigned ch = 0; ch < channels; ch++)
				*(nextBuffer++) = sample;
		}
	}
}

void FillColourBars(IDeckLinkVideoFrame* theFrame, bool reverse)
{
	unsigned int*	nextWord;
	unsigned long	width;
	unsigned long	height;
	unsigned int	bars[8] = {0xEA80EA80, 0xD292D210, 0xA910A9A5, 0x90229035, 0x6ADD6ACA, 0x51EF515A, 0x286D28EF, 0x10801080};

	theFrame->GetBytes((void**)&nextWord);
	width = theFrame->GetWidth();
	height = theFrame->GetHeight();

	if (reverse)
	{
		for (long y = 0; y < height; y++)
		{
			for (long x = width - 2; x >= 0; x -= 2)
			{
				*(nextWord++) = bars[(x * 8) / width];
			}
		}
	}
	else
	{
		for (long y = 0; y < height; y++)
		{
			for (long x = 0; x < width; x += 2)
			{
				*(nextWord++) = bars[(x * 8) / width];
			}
		}
	}
}

void FillBlack(IDeckLinkVideoFrame* theFrame)
{
	unsigned int*	nextWord;
	unsigned long	width;
	unsigned long	height;
	unsigned long	wordsRemaining;

	theFrame->GetBytes((void**)&nextWord);
	width = theFrame->GetWidth();
	height = theFrame->GetHeight();

	wordsRemaining = (width * 2 * height) / 4;

	while (wordsRemaining-- > 0)
		*(nextWord++) = 0x10801080;
}

int GetRowBytes(BMDPixelFormat pixelFormat, int frameWidth)
{
	int bytesPerRow;

	// Refer to DeckLink SDK Manual - 2.7.4 Pixel Formats
	switch (pixelFormat)
	{
	case bmdFormat8BitYUV:
		bytesPerRow = frameWidth * 2;
		break;

	case bmdFormat10BitYUV:
		bytesPerRow = ((frameWidth + 47) / 48) * 128;
		break;

	case bmdFormat10BitRGB:
		bytesPerRow = ((frameWidth + 63) / 64) * 256;
		break;

	case bmdFormat8BitARGB:
	case bmdFormat8BitBGRA:
	default:
		bytesPerRow = frameWidth * 4;
		break;
	}

	return bytesPerRow;
}

