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

#include <QCoreApplication>
#include <QMessageBox>
#include <QTextStream>
#include "DeckLinkInputDevice.h"

DeckLinkInputDevice::DeckLinkInputDevice(CapturePreview* owner, IDeckLink* device)
	: m_uiDelegate(owner), m_refCount(1), m_deckLink(device), m_deckLinkInput(nullptr), 
	m_deckLinkConfig(nullptr), m_deckLinkHDMIInputEDID(nullptr), m_deckLinkProfileManager(nullptr),
	m_supportsFormatDetection(false), m_currentlyCapturing(false), m_applyDetectedInputMode(false),
	m_supportedInputConnections(0)
{
	m_deckLink->AddRef();
}

DeckLinkInputDevice::~DeckLinkInputDevice()
{
	if (m_deckLinkHDMIInputEDID != nullptr)
	{
		m_deckLinkHDMIInputEDID->Release();
		m_deckLinkHDMIInputEDID = nullptr;
	}

	if (m_deckLinkProfileManager != nullptr)
	{
		m_deckLinkProfileManager->Release();
		m_deckLinkProfileManager = nullptr;
	}

	if (m_deckLinkInput != nullptr)
	{
		m_deckLinkInput->Release();
		m_deckLinkInput = nullptr;
	}

	if (m_deckLinkConfig != nullptr)
	{
		m_deckLinkConfig->Release();
		m_deckLinkConfig = nullptr;
	}

	if (m_deckLink != nullptr)
	{
		m_deckLink->Release();
		m_deckLink = nullptr;
	}
}

HRESULT	DeckLinkInputDevice::QueryInterface(REFIID iid, LPVOID *ppv)
{
	CFUUIDBytes		iunknown;
	HRESULT			result = E_NOINTERFACE;

	if (ppv == nullptr)
		return E_INVALIDARG;

	// Initialise the return result
	*ppv = nullptr;

	// Obtain the IUnknown interface and compare it the provided REFIID
	iunknown = CFUUIDGetUUIDBytes(IUnknownUUID);
	if (memcmp(&iid, &iunknown, sizeof(REFIID)) == 0)
	{
		*ppv = this;
		AddRef();
		result = S_OK;
	}
	else if (memcmp(&iid, &IID_IDeckLinkInputCallback, sizeof(REFIID)) == 0)
	{
		*ppv = (IDeckLinkInputCallback*)this;
		AddRef();
		result = S_OK;
	}

	return result;
}

ULONG DeckLinkInputDevice::AddRef(void)
{
	return (ULONG) m_refCount.fetchAndAddAcquire(1);
}

ULONG DeckLinkInputDevice::Release(void)
{
	ULONG newRefValue = m_refCount.fetchAndAddAcquire(-1);
	
	if (newRefValue == 0)
	{
		delete this;
		return 0;
	}

	return newRefValue;
}

bool DeckLinkInputDevice::Init()
{
	HRESULT							result;
	IDeckLinkProfileAttributes*		deckLinkAttributes	= nullptr;
	const char*						deviceNameStr;

	// Get input interface
	result = m_deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&m_deckLinkInput);
	if (result != S_OK)
	{
		// This may occur if device does not have input interface, for instance DeckLink Mini Monitor.
		return false;
	}
		
	// Get configuration interface so we can change input connector
	// We hold onto IDeckLinkConfiguration until destructor to retain input connector setting
	result = m_deckLink->QueryInterface(IID_IDeckLinkConfiguration, (void**)&m_deckLinkConfig);
	if (result != S_OK)
	{
		QMessageBox::critical(m_uiDelegate, "DeckLink Input initialization error", "Unable to query IDeckLinkConfiguration object interface");
		return false;
	}

	// Get attributes interface
	result = m_deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**) &deckLinkAttributes);
	if (result != S_OK)
	{
		QMessageBox::critical(m_uiDelegate, "DeckLink Input initialization error", "Unable to query IDeckLinkProfileAttributes object interface");
		return false;
	}

	// Check if input mode detection is supported.
	if (deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &m_supportsFormatDetection) != S_OK)
		m_supportsFormatDetection = false;

	// Get the supported input connections for the device
	if (deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &m_supportedInputConnections) != S_OK)
		m_supportedInputConnections = 0;
		
	deckLinkAttributes->Release();

	// Enable all EDID functionality if possible
	if (m_deckLink->QueryInterface(IID_IDeckLinkHDMIInputEDID, (void**)&m_deckLinkHDMIInputEDID) == S_OK && m_deckLinkHDMIInputEDID)
	{
		int64_t allKnownRanges = bmdDynamicRangeSDR | bmdDynamicRangeHDRStaticPQ | bmdDynamicRangeHDRStaticHLG;
		m_deckLinkHDMIInputEDID->SetInt(bmdDeckLinkHDMIInputEDIDDynamicRange, allKnownRanges);
		m_deckLinkHDMIInputEDID->WriteToEDID();
	}

	// Get device name
	result = m_deckLink->GetDisplayName(&deviceNameStr); 
	if (result == S_OK)
	{
		m_deviceName = deviceNameStr;
		free((void*)deviceNameStr);
	}
	else
	{
		m_deviceName = "DeckLink";
	}

	// Get the profile manager interface
	// Will return S_OK when the device has > 1 profiles
	if (m_deckLink->QueryInterface(IID_IDeckLinkProfileManager, (void**) &m_deckLinkProfileManager) != S_OK)
	{
		m_deckLinkProfileManager = nullptr;
	}

	return true;
}

bool DeckLinkInputDevice::StartCapture(BMDDisplayMode displayMode, IDeckLinkScreenPreviewCallback* screenPreviewCallback, bool applyDetectedInputMode)
{
	HRESULT				result;
	BMDVideoInputFlags	videoInputFlags = bmdVideoInputFlagDefault;

	m_applyDetectedInputMode = applyDetectedInputMode;

	// Enable input video mode detection if the device supports it
	if (m_supportsFormatDetection)
		videoInputFlags |=  bmdVideoInputEnableFormatDetection;

	// Set the screen preview
	m_deckLinkInput->SetScreenPreviewCallback(screenPreviewCallback);

	// Set capture callback
	m_deckLinkInput->SetCallback(this);

	// Set the video input mode
	result = m_deckLinkInput->EnableVideoInput(displayMode, bmdFormat8BitYUV, videoInputFlags);
	if (result != S_OK)
	{
		QMessageBox::critical(m_uiDelegate, "Error starting the capture", "This application was unable to select the chosen video mode. Perhaps, the selected device is currently in-use.");
		return false;
	}

	// Start the capture
	result = m_deckLinkInput->StartStreams();
	if (result != S_OK)
	{
		QMessageBox::critical(m_uiDelegate, "Error starting the capture", "This application was unable to start the capture. Perhaps, the selected device is currently in-use.");
		return false;
	}

	m_currentlyCapturing = true;

	return true;
}

void DeckLinkInputDevice::StopCapture()
{
	if (m_deckLinkInput != NULL)
	{
		// Stop the capture
		m_deckLinkInput->StopStreams();

		//
		m_deckLinkInput->SetScreenPreviewCallback(NULL);

		// Delete capture callback
		m_deckLinkInput->SetCallback(NULL);
	}

	m_currentlyCapturing = false;
}

HRESULT DeckLinkInputDevice::VideoInputFormatChanged (BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	HRESULT 		result;
	BMDPixelFormat	pixelFormat = bmdFormat10BitYUV;

	// Unexpected callback when auto-detect mode not enabled
	if (!m_applyDetectedInputMode)
		return E_FAIL;;

	if (detectedSignalFlags & bmdDetectedVideoInputRGB444)
		pixelFormat = bmdFormat10BitRGB;

	// Stop the capture
	m_deckLinkInput->StopStreams();

	// Set the video input mode
	result = m_deckLinkInput->EnableVideoInput(newMode->GetDisplayMode(), pixelFormat, bmdVideoInputEnableFormatDetection);
	if (result != S_OK)
	{
		QMessageBox::critical(m_uiDelegate, "Error restarting the capture", "This application was unable to set new display mode");
		return result;
	}

	// Start the capture
	result = m_deckLinkInput->StartStreams();
	if (result != S_OK)
	{
		QMessageBox::critical(m_uiDelegate, "Error restarting the capture", "This application was unable to restart capture");
		return result;
	}

	// Notify UI of new display mode
	if ((m_uiDelegate != nullptr) && (notificationEvents & bmdVideoInputDisplayModeChanged))
		QCoreApplication::postEvent(m_uiDelegate, new DeckLinkInputFormatChangedEvent(newMode->GetDisplayMode()));
	
	return S_OK;
}

HRESULT DeckLinkInputDevice::VideoInputFrameArrived (IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* /* audioPacket */)
{
	bool					validFrame;
	AncillaryDataStruct*	ancillaryData;
	MetadataStruct*			metadata;

	if (videoFrame == NULL)
		return S_OK;

	validFrame = (videoFrame->GetFlags() & bmdFrameHasNoInputSource) == 0;

	// Get the various timecodes and userbits attached to this frame
	ancillaryData = new AncillaryDataStruct();
	GetAncillaryDataFromFrame(videoFrame, bmdTimecodeVITC,					&ancillaryData->vitcF1Timecode,		&ancillaryData->vitcF1UserBits);
	GetAncillaryDataFromFrame(videoFrame, bmdTimecodeVITCField2,			&ancillaryData->vitcF2Timecode,		&ancillaryData->vitcF2UserBits);
	GetAncillaryDataFromFrame(videoFrame, bmdTimecodeRP188VITC1,			&ancillaryData->rp188vitc1Timecode,	&ancillaryData->rp188vitc1UserBits);
	GetAncillaryDataFromFrame(videoFrame, bmdTimecodeRP188VITC2,			&ancillaryData->rp188vitc2Timecode,	&ancillaryData->rp188vitc2UserBits);
	GetAncillaryDataFromFrame(videoFrame, bmdTimecodeRP188LTC,				&ancillaryData->rp188ltcTimecode,	&ancillaryData->rp188ltcUserBits);
	GetAncillaryDataFromFrame(videoFrame, bmdTimecodeRP188HighFrameRate,	&ancillaryData->rp188hfrtcTimecode,	&ancillaryData->rp188hfrtcUserBits);

	metadata = new MetadataStruct();
	GetMetadataFromFrame(videoFrame, metadata);

	// Update the UI with new Ancillary data
	if (m_uiDelegate != nullptr)
		QCoreApplication::postEvent(m_uiDelegate, new DeckLinkInputFrameArrivedEvent(ancillaryData, metadata, validFrame));
	else
	{
		delete ancillaryData;
		delete metadata;
	}

	return S_OK;
}

void DeckLinkInputDevice::GetAncillaryDataFromFrame(IDeckLinkVideoInputFrame* videoFrame, BMDTimecodeFormat timecodeFormat, QString* timecodeString, QString* userBitsString)
{
	IDeckLinkTimecode*		timecode	= nullptr;
	const char*				timecodeStr	= nullptr;
	BMDTimecodeUserBits		userBits 	= 0;

	if ((videoFrame != NULL) && (timecodeString != NULL) && (userBitsString != NULL)
		&& (videoFrame->GetTimecode(timecodeFormat, &timecode) == S_OK))
	{
		if (timecode->GetString(&timecodeStr) == S_OK)
		{
			*timecodeString = timecodeStr;
			free((void*)timecodeStr);
		}
		else
		{
			*timecodeString = "";
		}

		timecode->GetTimecodeUserBits(&userBits);
		*userBitsString = QString("0x%1").arg(userBits, 8, 16, QChar('0'));

		timecode->Release();
	}
	else
	{
		*timecodeString = "";
		*userBitsString = "";
	}
}

void DeckLinkInputDevice::GetMetadataFromFrame(IDeckLinkVideoInputFrame* videoFrame, MetadataStruct* metadata)
{
	metadata->electroOpticalTransferFunction = "";
	metadata->displayPrimariesRedX = "";
	metadata->displayPrimariesRedY = "";
	metadata->displayPrimariesGreenX = "";
	metadata->displayPrimariesGreenY = "";
	metadata->displayPrimariesBlueX = "";
	metadata->displayPrimariesBlueY = "";
	metadata->whitePointX = "";
	metadata->whitePointY = "";
	metadata->maxDisplayMasteringLuminance = "";
	metadata->minDisplayMasteringLuminance = "";
	metadata->maximumContentLightLevel = "";
	metadata->maximumFrameAverageLightLevel = "";
	metadata->colorspace = "";

	IDeckLinkVideoFrameMetadataExtensions* metadataExtensions = NULL;
	if (videoFrame->QueryInterface(IID_IDeckLinkVideoFrameMetadataExtensions, (void**)&metadataExtensions) == S_OK)
	{
		double doubleValue = 0.0;
		int64_t intValue = 0;

		if (metadataExtensions->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, &intValue) == S_OK)
		{
			switch (intValue)
			{
			case 0:
				metadata->electroOpticalTransferFunction = "SDR";
				break;
			case 1:
				metadata->electroOpticalTransferFunction = "HDR";
				break;
			case 2:
				metadata->electroOpticalTransferFunction = "PQ (ST2084)";
				break;
			case 3:
				metadata->electroOpticalTransferFunction = "HLG";
				break;
			default:
				metadata->electroOpticalTransferFunction = QString("Unknown EOTF: %1").arg((int32_t)intValue);
				break;
			}
		}

		if (videoFrame->GetFlags() & bmdFrameContainsHDRMetadata)
		{
			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedX, &doubleValue) == S_OK)
				metadata->displayPrimariesRedX = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesRedY, &doubleValue) == S_OK)
				metadata->displayPrimariesRedY = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenX, &doubleValue) == S_OK)
				metadata->displayPrimariesGreenX = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesGreenY, &doubleValue) == S_OK)
				metadata->displayPrimariesGreenY = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueX, &doubleValue) == S_OK)
				metadata->displayPrimariesBlueX = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRDisplayPrimariesBlueY, &doubleValue) == S_OK)
				metadata->displayPrimariesBlueY = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointX, &doubleValue) == S_OK)
				metadata->whitePointX = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRWhitePointY, &doubleValue) == S_OK)
				metadata->whitePointY = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaxDisplayMasteringLuminance, &doubleValue) == S_OK)
				metadata->maxDisplayMasteringLuminance = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMinDisplayMasteringLuminance, &doubleValue) == S_OK)
				metadata->minDisplayMasteringLuminance = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumContentLightLevel, &doubleValue) == S_OK)
				metadata->maximumContentLightLevel = QString::number(doubleValue, 'f', 4);

			if (metadataExtensions->GetFloat(bmdDeckLinkFrameMetadataHDRMaximumFrameAverageLightLevel, &doubleValue) == S_OK)
				metadata->maximumFrameAverageLightLevel = QString::number(doubleValue, 'f', 4);
		}

		if (metadataExtensions->GetInt(bmdDeckLinkFrameMetadataColorspace, &intValue) == S_OK)
		{
			switch (intValue)
			{
			case bmdColorspaceRec601:
				metadata->colorspace = "Rec.601";
				break;
			case bmdColorspaceRec709:
				metadata->colorspace = "Rec.709";
				break;
			case bmdColorspaceRec2020:
				metadata->colorspace = "Rec.2020";
				break;
			default:
				metadata->colorspace = QString("Unknown Colorspace: %1").arg((int32_t)intValue);
				break;
			}
		}

		metadataExtensions->Release();
	}
}

DeckLinkInputFormatChangedEvent::DeckLinkInputFormatChangedEvent(BMDDisplayMode displayMode)
	: QEvent(kVideoFormatChangedEvent), m_displayMode(displayMode)
{
}

DeckLinkInputFrameArrivedEvent::DeckLinkInputFrameArrivedEvent(AncillaryDataStruct* ancillaryData, MetadataStruct* metadata, bool signalValid)
	: QEvent(kVideoFrameArrivedEvent), m_ancillaryData(ancillaryData), m_metadata(metadata), m_signalValid(signalValid)
{
}


