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

#pragma once

#include "DeckLinkAPI_h.h"
#include "DecoderMF.h"
#include "PreviewWindow.h"

#define WM_PREVIEWSTART	(WM_APP + 1)
#define WM_PREVIEWSTOP	(WM_APP + 2)

// CStreamingPreviewDlg dialog
class CStreamingPreviewDlg : public CDialog,
	public IBMDStreamingDeviceNotificationCallback,
	public IBMDStreamingH264InputCallback
{
// Construction
public:
	explicit CStreamingPreviewDlg(CWnd* pParent = NULL);	// standard constructor

// Dialog Data
	enum { IDD = IDD_STREAMINGPREVIEW_DIALOG };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
private:
	ULONG							m_refCount;
	HICON							m_hIcon;
	//
	CButton							m_startButton;
	CStatic							m_configBoxStatic;
	CComboBox						m_videoInputCombo;
	CComboBox						m_videoEncodingCombo;

	// Streaming API:
	IBMDStreamingDiscovery*			m_streamingDiscovery;
	IDeckLink*						m_streamingDevice;
	IBMDStreamingDeviceInput*		m_streamingDeviceInput;
	BMDStreamingDeviceMode			m_deviceMode;
	BMDVideoConnection				m_inputConnector;
	BMDDisplayMode					m_inputMode;

	//
	PreviewWindow*					m_previewWindow;
	DecoderMF*						m_decoder;

	~CStreamingPreviewDlg();

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg void OnClose();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

public:
	afx_msg void					OnBnClickedOk();
	afx_msg void					OnInputModeChanged();

private:
	afx_msg LRESULT 				StartPreview(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT 				StopPreview(WPARAM wParam, LPARAM lParam);
	void							UpdateUIForNewDevice();
	void							UpdateUIForNoDevice();
	void							UpdateUIForModeChanges();
	void							UpdateEncodingPresetsUIForInputMode();
	void							EncodingPresetsRemoveItems();

private:
	static void						PreviewWindowClosedFunction(void* ctx, PreviewWindow* previewWindow);
	void							OnPreviewWindowClosed(PreviewWindow* previewWindow);

public:
	// IUnknown
	virtual HRESULT STDMETHODCALLTYPE	QueryInterface (REFIID iid, LPVOID* ppv);
	virtual ULONG STDMETHODCALLTYPE		AddRef ();
	virtual ULONG STDMETHODCALLTYPE		Release ();

public:
	// IBMDStreamingDeviceNotificationCallback
	virtual HRESULT STDMETHODCALLTYPE StreamingDeviceArrived(IDeckLink* device);
	virtual HRESULT STDMETHODCALLTYPE StreamingDeviceRemoved(IDeckLink* device);
	virtual HRESULT STDMETHODCALLTYPE StreamingDeviceModeChanged(IDeckLink* device, BMDStreamingDeviceMode mode);
	virtual HRESULT STDMETHODCALLTYPE StreamingDeviceFirmwareUpdateProgress(IDeckLink* device, unsigned char percent);

public:
	// IBMDStreamingH264InputCallback
	virtual HRESULT STDMETHODCALLTYPE H264NALPacketArrived(IBMDStreamingH264NALPacket* nalPacket);
	virtual HRESULT STDMETHODCALLTYPE H264AudioPacketArrived(IBMDStreamingAudioPacket* audioPacket);
	virtual HRESULT STDMETHODCALLTYPE MPEG2TSPacketArrived(IBMDStreamingMPEG2TSPacket* mpeg2TSPacket);
	virtual HRESULT STDMETHODCALLTYPE H264VideoInputConnectorScanningChanged(void);
	virtual HRESULT STDMETHODCALLTYPE H264VideoInputConnectorChanged(void);
	virtual HRESULT STDMETHODCALLTYPE H264VideoInputModeChanged(void);

};
