/* -LICENSE-START-
 ** Copyright (c) 2015 Blackmagic Design
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
#include "CommonGui.h"

#include <QFont>
#include <QApplication>
#include <QDesktopWidget>
#include <QScreen>

static QFont baseFontFor(CommonGui::FontStyle style)
{
	switch (style)
	{
        case CommonGui::kSentinelLight:
            return QFont("Open Sans Light", 30, QFont::Light);
        case CommonGui::kIcemanSemibold:
            return QFont("Open Sans Semibold", 12, QFont::DemiBold);
        case CommonGui::kVoltronLight:
            return QFont("Gotham Extra Light", 68, QFont::Light);
        case CommonGui::kGalactusLight:
            return QFont("Open Sans Light", 23, QFont::Light);
        default:
            break;
	}
	return QFont("Times", 24, QFont::Bold);
}

QFont CommonGui::font(CommonGui::FontStyle style)
{
	static const int kBaseLineDPI = 72;
	QFont font = baseFontFor(style);
	qreal currentDPI = QApplication::desktop()->logicalDpiX();
	font.setPointSizeF(font.pointSize() * kBaseLineDPI / currentDPI);
	return font;
}

