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

#include "platform.h"
#include "DeckLinkOpenGLWidget.h"
#include <QPainter>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QOpenGLFunctions>

///
/// DeckLinkOpenGLDelegate
///

DeckLinkOpenGLDelegate::DeckLinkOpenGLDelegate() :
	m_refCount(1)
{
}

/// IUnknown methods

HRESULT DeckLinkOpenGLDelegate::QueryInterface(REFIID iid, LPVOID *ppv)
{
	HRESULT result = S_OK;

	if (ppv == nullptr)
		return E_INVALIDARG;

	// Obtain the IUnknown interface and compare it the provided REFIID
	if (iid == IID_IUnknown)
	{
		*ppv = this;
		AddRef();
	}
	else if (iid == IID_IDeckLinkScreenPreviewCallback)
	{
		*ppv = static_cast<IDeckLinkScreenPreviewCallback*>(this);
		AddRef();
	}
	else
	{
		*ppv = nullptr;
		result = E_NOINTERFACE;
	}

	return result;
}

ULONG DeckLinkOpenGLDelegate::AddRef ()
{
	return ++m_refCount;
}

ULONG DeckLinkOpenGLDelegate::Release()
{
	ULONG newRefValue = --m_refCount;
	if (newRefValue == 0)
		delete this;

	return newRefValue;
}

/// IDeckLinkScreenPreviewCallback methods

HRESULT DeckLinkOpenGLDelegate::DrawFrame(IDeckLinkVideoFrame* frame)
{
	emit frameArrived(com_ptr<IDeckLinkVideoFrame>(frame));
	return S_OK;
}

///
/// DeckLinkOpenGLOverlay
///

void DeckLinkOpenGLOverlay::paintEvent(QPaintEvent *)
{
	QPainter painter;
	painter.begin(this);

	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	QBrush brush(QColor(0, 0, 0, 128));

	if (!m_parent->signalValid())
	{
		font.setPixelSize(height() / 12);
		QFontMetrics metrics(font, this);

		QString text("No Signal");
		painter.setPen(QColor(Qt::red));
		painter.setFont(font);
		painter.drawText((width() - metrics.width(text)) / 2, (height() - metrics.height()) / 2 + metrics.ascent(), text);
	}

	if (m_parent->isDeviceLabelEnabled())
	{
		font.setPixelSize(height() / 16);
		QFontMetrics metrics(font, this);

		QRect box(0, 0, width(), metrics.height() + 4);
		painter.fillRect(box, brush);
		painter.setPen(QColor(Qt::white));
		painter.setFont(font);
		painter.drawText((width() - metrics.width(m_parent->deviceLabel())) / 2, box.top() + 2 + metrics.ascent(), m_parent->deviceLabel());
	}

	if (m_parent->isTimecodeEnabled())
	{
		font.setPixelSize(height() / 16);
		QFontMetrics metrics(font, this);

		QRect box(0, height() - (metrics.height() + 4), width(), metrics.height() + 4);
		painter.fillRect(box, brush);
		painter.setPen(QColor(Qt::white));
		painter.setFont(font);
		painter.drawText((width() - metrics.width(m_parent->timecode())) / 2, box.top() + 2 + metrics.ascent(), m_parent->timecode());
	}

	painter.end();
}

///
/// DeckLinkOpenGLWidget
///

DeckLinkOpenGLWidget::DeckLinkOpenGLWidget(QWidget* parent) :
	QOpenGLWidget(parent),
	m_enableTimecode(false),
	m_enableDeviceLabel(false)
{
	GetDeckLinkOpenGLScreenPreviewHelper(m_deckLinkScreenPreviewHelper);
	m_delegate = make_com_ptr<DeckLinkOpenGLDelegate>();

	connect(m_delegate.get(), &DeckLinkOpenGLDelegate::frameArrived, this, &DeckLinkOpenGLWidget::setFrame, Qt::QueuedConnection);

	m_overlay = new DeckLinkOpenGLOverlay(this);
}

DeckLinkOpenGLWidget::~DeckLinkOpenGLWidget()
{
}

/// QOpenGLWidget methods

void DeckLinkOpenGLWidget::initializeGL()
{
	if (m_deckLinkScreenPreviewHelper)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_deckLinkScreenPreviewHelper->InitializeGL();
	}
}

void DeckLinkOpenGLWidget::paintGL()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_deckLinkScreenPreviewHelper->PaintGL();
}

void DeckLinkOpenGLWidget::resizeGL(int width, int height)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_overlay->resize(width, height);

	QOpenGLFunctions* f = context()->functions();
	f->glViewport(0, 0, width, height);
}

/// Other methods

IDeckLinkScreenPreviewCallback* DeckLinkOpenGLWidget::delegate()
{
	return m_delegate.get();
}

void DeckLinkOpenGLWidget::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_signalValid = false;
	if (m_delegate)
		m_delegate->DrawFrame(nullptr);
}

void DeckLinkOpenGLWidget::setFrame(com_ptr<IDeckLinkVideoFrame> frame)
{
	if (m_deckLinkScreenPreviewHelper)
	{
		bool validTimecode = false;
		m_deckLinkScreenPreviewHelper->SetFrame(frame.get());

		{
			std::lock_guard<std::mutex> lock(m_mutex);

			if (frame)
			{
				m_signalValid = (frame->GetFlags() & bmdFrameHasNoInputSource) == 0;

				if (m_signalValid)
				{
					// Get the timecode attached to this frame
					com_ptr<IDeckLinkTimecode>	timecode;
					HRESULT result = frame->GetTimecode(bmdTimecodeRP188Any, timecode.releaseAndGetAddressOf());
					if (result == S_OK)
					{
						dlstring_t timecodeStr;

						result = timecode->GetString(&timecodeStr);
						if (result == S_OK)
						{
							m_timecode = DlToQString(timecodeStr);
							DeleteString(timecodeStr);
							validTimecode = true;
						}
					}
				}
			}

			if (!validTimecode)
				m_timecode = "00:00:00:00";

		}

		update();
		m_overlay->update();
	}
}

void DeckLinkOpenGLWidget::setDeviceLabel(const QString& label)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_deviceLabel = label;
	m_overlay->update();
}

QString DeckLinkOpenGLWidget::deviceLabel()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_deviceLabel;
}

void DeckLinkOpenGLWidget::enableTimecode(bool enable)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_enableTimecode = enable;
	m_overlay->update();
}

bool DeckLinkOpenGLWidget::isTimecodeEnabled()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_enableTimecode;
}

void DeckLinkOpenGLWidget::enableDeviceLabel(bool enable)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_enableDeviceLabel = enable;
	m_overlay->update();
}

bool DeckLinkOpenGLWidget::isDeviceLabelEnabled()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_enableDeviceLabel;
}

bool DeckLinkOpenGLWidget::signalValid()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_signalValid;
}

QString DeckLinkOpenGLWidget::timecode()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_timecode;
}
