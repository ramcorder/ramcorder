/* -LICENSE-START-
** Copyright (c) 2019 Blackmagic Design
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

// FileCaptureDlg.h : header file
//

#pragma once

#include <thread>
#include "resource.h"
#include "AudioSampleQueue.h"
#include "DeckLinkInputDevice.h"
#include "DeckLinkDeviceDiscovery.h"
#include "PreviewWindow.h"
#include "ProfileCallback.h"
#include "SampleMemoryAllocator.h"
#include "SinkWriter.h"
#include "DeckLinkAPI_h.h"

// Custom messages
#define WM_INPUT_STREAM_VALID_MESSAGE			(WM_APP + 1)
#define WM_DETECT_VIDEO_MODE_MESSAGE			(WM_APP + 2)
#define WM_ADD_DEVICE_MESSAGE					(WM_APP + 3)
#define WM_REMOVE_DEVICE_MESSAGE				(WM_APP + 4)
#define WM_ERROR_RESTARTING_CAPTURE_MESSAGE		(WM_APP + 5)
#define WM_UPDATE_PROFILE_MESSAGE				(WM_APP + 6)
#define WM_UPDATE_STREAM_TIME_MESSAGE			(WM_APP + 7)
#define WM_SIGNAL_VALID_MESSAGE					(WM_APP + 8)
#define WM_SIGNAL_INVALID_MESSAGE				(WM_APP + 9)

// Forward declarations
class DeckLinkInputDevice;
class DeckLinkDeviceDiscovery;
class PreviewWindow;
class ProfileCallback;

class CFileCaptureDlg : public CDialog
{
public:
	CFileCaptureDlg(CWnd* pParent = nullptr);

	// Dialog Data
	enum { IDD = IDD_FILECAPTURE_DIALOG };

	// Capture state
	enum class CaptureState { WaitingForDevice, WaitingForValidInput, WaitingToRecord, Recording };
	
	// UI-related handlers
	afx_msg void			OnNewDeviceSelected();
	afx_msg void			OnInputConnectionSelected();
	afx_msg void			OnVideoModeSelected();
	afx_msg void			OnRecordBnClicked();
	afx_msg void			OnAutoDetectCBClicked();
	afx_msg void			OnClose();

	// Custom message handlers
	afx_msg LRESULT			OnDetectVideoMode(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnAddDevice(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnRemoveDevice(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnErrorRestartingCapture(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnProfileUpdate(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnUpdateStreamTime(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnSignalValid(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT			OnSignalInvalid(WPARAM wParam, LPARAM lParam);

	void					ShowErrorMessage(TCHAR* msg, TCHAR* title);

	CStatusBar				m_statusBar;

protected:
	// Internal helper methods
	void					UpdateInterface();
	void					RefreshInputConnectionList();
	void					RefreshVideoModeList();
	void					StartCapture();
	void					StopCapture();
	void					StartRecording();
	void					StopRecording();
	void					AddDevice(CComPtr<IDeckLink> deckLink);
	void					RemoveDevice(CComPtr<IDeckLink> deckLink);
	void					VideoFormatChanged(BMDDisplayMode videoFormat);

	// UI elements
	CComboBox				m_deviceListCombo;
	CComboBox				m_inputConnectionCombo;
	CComboBox				m_modeListCombo;
	CButton					m_recordButton;
	CButton					m_applyDetectedInputModeCheckbox;

	CStatic					m_previewBox;
	CComPtr<PreviewWindow>	m_previewWindow;

	//
	virtual void			DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	HICON					m_hIcon;

	// Generated message map functions
	virtual BOOL			OnInitDialog();
	afx_msg void			OnPaint();
	afx_msg HCURSOR			OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

private:
	void					UpdateStatusBar(UINT frameCount);

	CCriticalSection					m_critSec; // to synchronise access to the above structures
	CComPtr<DeckLinkInputDevice>		m_selectedDevice;
	BMDVideoConnection					m_selectedInputConnection;
	CComPtr<DeckLinkDeviceDiscovery>	m_deckLinkDiscovery;
	CComPtr<ProfileCallback>            m_profileCallback;
	CComPtr<SampleMemoryAllocator>		m_videoSampleQueue;
	CComPtr<AudioSampleQueue>			m_audioSampleQueue;
	CComPtr<SinkWriter>					m_sinkWriter;
	std::thread							m_sinkWriterThread;

	BMDDisplayMode						m_displayMode;
	BMDPixelFormat						m_pixelFormat;
	BMDTimeValue						m_frameDuration;
	BMDTimeScale						m_timeScale;
	UINT								m_dropFrames;
	UINT								m_frameCount;
	std::atomic<CaptureState>			m_captureState;
	TCHAR								m_tempFileName[MAX_PATH];
};
