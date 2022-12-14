/* -LICENSE-START-
 ** Copyright (c) 2017 Blackmagic Design
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

#ifndef __OPENGL_COMPOSITE_H__
#define __OPENGL_COMPOSITE_H__

#include "DeckLinkAPI.h"
#include "VideoFrameTransfer.h"
#include <QGLWidget>
#include <QMutex>
#include <QAtomicInt>
#include <map>
#include <vector>
#include <deque>

#undef Bool

class PlayoutDelegate;
class CaptureDelegate;
class PinnedMemoryAllocator;

class OpenGLComposite : public QGLWidget
{
	Q_OBJECT

public:
	OpenGLComposite(QWidget *parent = NULL);
	~OpenGLComposite();

	bool InitDeckLink();
	bool Start();
	bool Stop();

private:
	bool CheckOpenGLExtensions();

	// QGLWidget virtual methods
	virtual void initializeGL();
	virtual void paintGL();
	virtual void resizeGL(int width, int height);

private slots:
	void VideoFrameArrived(IDeckLinkVideoInputFrame* inputFrame, bool hasNoInputSource);
	void PlayoutNextFrame(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);

private:
	QWidget*								mParent;
	CaptureDelegate*						mCaptureDelegate;
	PlayoutDelegate*						mPlayoutDelegate;
	QMutex									mMutex;				// protect access to both OpenGL and DeckLink calls

	// DeckLink
	IDeckLinkInput*							mDLInput;
	IDeckLinkOutput*						mDLOutput;
	std::deque<IDeckLinkVideoFrame*>		mDLOutputVideoFrameQueue;
	PinnedMemoryAllocator*					mCaptureAllocator;
	PinnedMemoryAllocator*					mPlayoutAllocator;
	BMDTimeValue							mFrameDuration;
	BMDTimeScale							mFrameTimescale;
	unsigned								mTotalPlayoutFrames;
	unsigned								mFrameWidth;
	unsigned								mFrameHeight;
	bool									mHasNoInputSource;

	// OpenGL data
	bool									mFastTransferExtensionAvailable;
	GLuint									mCaptureTexture;
	GLuint									mFBOTexture;
	GLuint									mUnpinnedTextureBuffer;
	GLuint									mIdFrameBuf;
	GLuint									mIdColorBuf;
	GLuint									mIdDepthBuf;
	GLuint									mProgram;
	GLuint									mFragmentShader;
	GLfloat									mRotateAngle;
	GLfloat									mRotateAngleRate;
	int										mViewWidth;
	int										mViewHeight;

	bool InitOpenGLState();
	bool compileFragmentShader(int errorMessageSize, char* errorMessage);
};

////////////////////////////////////////////
// PinnedMemoryAllocator
////////////////////////////////////////////
class PinnedMemoryAllocator : public IDeckLinkMemoryAllocator
{
public:
	PinnedMemoryAllocator(QGLWidget* context, VideoFrameTransfer::Direction direction, unsigned cacheSize);
	virtual ~PinnedMemoryAllocator();

	bool transferFrame(void* address, GLuint gpuTexture);
	void waitForTransferComplete(void* address);
	void beginTextureInUse();
	void endTextureInUse();
	void unPinAddress(void* address);

	// IUnknown methods
	virtual HRESULT STDMETHODCALLTYPE	QueryInterface(REFIID iid, LPVOID *ppv);
	virtual ULONG STDMETHODCALLTYPE		AddRef(void);
	virtual ULONG STDMETHODCALLTYPE		Release(void);

	// IDeckLinkMemoryAllocator methods
	virtual HRESULT STDMETHODCALLTYPE	AllocateBuffer (uint32_t bufferSize, void* *allocatedBuffer);
	virtual HRESULT STDMETHODCALLTYPE	ReleaseBuffer (void* buffer);
	virtual HRESULT STDMETHODCALLTYPE	Commit ();
	virtual HRESULT STDMETHODCALLTYPE	Decommit ();

private:
	QGLWidget*								mContext;
	QAtomicInt								mRefCount;
	VideoFrameTransfer::Direction			mDirection;
	std::map<void*, VideoFrameTransfer*>	mFrameTransfer;
	std::map<void*, unsigned long>			mAllocatedSize;
	std::vector<void*>						mFrameCache;
	unsigned								mFrameCacheSize;
};

////////////////////////////////////////////
// Capture Delegate Class
////////////////////////////////////////////

class CaptureDelegate : public QObject, public IDeckLinkInputCallback
{
	Q_OBJECT

public:
	CaptureDelegate ();

	virtual HRESULT	STDMETHODCALLTYPE	QueryInterface (REFIID /*iid*/, LPVOID* /*ppv*/);
	virtual ULONG	STDMETHODCALLTYPE	AddRef ();
	virtual ULONG	STDMETHODCALLTYPE	Release ();

	virtual HRESULT STDMETHODCALLTYPE	VideoInputFrameArrived(IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioPacket);
	virtual HRESULT	STDMETHODCALLTYPE	VideoInputFormatChanged(BMDVideoInputFormatChangedEvents notificationEvents, IDeckLinkDisplayMode *newDisplayMode, BMDDetectedVideoInputFormatFlags detectedSignalFlags);

signals:
	void captureFrameArrived(IDeckLinkVideoInputFrame *videoFrame, bool hasNoInputSource);

private:
	QAtomicInt                              mRefCount;
};

////////////////////////////////////////////
// Playout Delegate Class
////////////////////////////////////////////

class PlayoutDelegate : public QObject, public IDeckLinkVideoOutputCallback
{
	Q_OBJECT

public:
	PlayoutDelegate ();

	virtual HRESULT	STDMETHODCALLTYPE	QueryInterface (REFIID /*iid*/, LPVOID* /*ppv*/);
	virtual ULONG	STDMETHODCALLTYPE	AddRef ();
	virtual ULONG	STDMETHODCALLTYPE	Release ();

	virtual HRESULT	STDMETHODCALLTYPE	ScheduledFrameCompleted (IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);
	virtual HRESULT	STDMETHODCALLTYPE	ScheduledPlaybackHasStopped ();

signals:
	void playoutFrameCompleted(IDeckLinkVideoFrame* completedFrame, BMDOutputFrameCompletionResult result);

private:
	QAtomicInt                              mRefCount;
};

#endif	// __OPENGL_COMPOSITE_H__
