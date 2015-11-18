/*
 * Copyright (C) 2014-2015 Canonical, Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranties of MERCHANTABILITY,
 * SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <mir_toolkit/mir_client_library.h>

// Qt
#include <QCoreApplication>
#include <QtCore/qmath.h>
#include <QScreen>
#include <QThread>
#include <qpa/qwindowsysteminterface.h>

// local
#include "screen.h"
#include "logging.h"
#include "orientationchangeevent_p.h"
#include "utils.h"

#include "memory"

#if !defined(QT_NO_DEBUG)

static const char *orientationToStr(Qt::ScreenOrientation orientation) {
    switch (orientation) {
        case Qt::PrimaryOrientation:
            return "primary";
        case Qt::PortraitOrientation:
            return "portrait";
        case Qt::LandscapeOrientation:
            return "landscape";
        case Qt::InvertedPortraitOrientation:
            return "inverted portrait";
        case Qt::InvertedLandscapeOrientation:
            return "inverted landscape";
        default:
            return "INVALID!";
    }
}
#endif

namespace {

    static inline bool isLittleEndian() {
        unsigned int i = 1;
        char *c = (char*)&i;
        return *c == 1;
    }

    int qGetEnvIntValue(const char *varName, bool *ok)
    {
        return qgetenv(varName).toInt(ok);
    }
} // anonymous namespace


const QEvent::Type OrientationChangeEvent::mType =
        static_cast<QEvent::Type>(QEvent::registerEventType());

UbuntuScreen::UbuntuScreen(const MirDisplayOutput &output)
    : mDpi{0}
    , mFormFactor{mir_form_factor_unknown}
    , mScale{1.0}
{
    DLOG("QUbuntuScreen::QUbuntuScreen (this=%p)", this);

    // Get screen resolution and properties.
    const int dpr = qGetEnvIntValue("QT_DEVICE_PIXEL_RATIO", &ok);
    mDevicePixelRatio = (ok && dpr > 0) ? dpr : 1.0;

    setMirDisplayOutput(output);

    // Set the default orientation based on the initial screen dimmensions.
    mNativeOrientation = (mGeometry.width() >= mGeometry.height()) ? Qt::LandscapeOrientation : Qt::PortraitOrientation;

    // If it's a landscape device (i.e. some tablets), start in landscape, otherwise portrait
    mCurrentOrientation = (mNativeOrientation == Qt::LandscapeOrientation) ? Qt::LandscapeOrientation : Qt::PortraitOrientation;
}

UbuntuScreen::~UbuntuScreen()
{
}

void UbuntuScreen::customEvent(QEvent* event) {
    DASSERT(QThread::currentThread() == thread());

    OrientationChangeEvent* oReadingEvent = static_cast<OrientationChangeEvent*>(event);
    switch (oReadingEvent->mOrientation) {
        case OrientationChangeEvent::LeftUp: {
            mCurrentOrientation = (screen()->primaryOrientation() == Qt::LandscapeOrientation) ?
                        Qt::InvertedPortraitOrientation : Qt::LandscapeOrientation;
            break;
        }
        case OrientationChangeEvent::TopUp: {
            mCurrentOrientation = (screen()->primaryOrientation() == Qt::LandscapeOrientation) ?
                        Qt::LandscapeOrientation : Qt::PortraitOrientation;
            break;
        }
        case OrientationChangeEvent::RightUp: {
            mCurrentOrientation = (screen()->primaryOrientation() == Qt::LandscapeOrientation) ?
                        Qt::PortraitOrientation : Qt::InvertedLandscapeOrientation;
            break;
        }
        case OrientationChangeEvent::TopDown: {
            mCurrentOrientation = (screen()->primaryOrientation() == Qt::LandscapeOrientation) ?
                        Qt::InvertedLandscapeOrientation : Qt::InvertedPortraitOrientation;
            break;
        }
        default: {
            DLOG("UbuntuScreen::customEvent - Unknown orientation.");
            return;
        }
    }

    // Raise the event signal so that client apps know the orientation changed
    DLOG("UbuntuScreen::customEvent - handling orientation change to %s", orientationToStr(mCurrentOrientation));
    QWindowSystemInterface::handleScreenOrientationChange(screen(), mCurrentOrientation);
}

void UbuntuScreen::handleWindowSurfaceResize(int windowWidth, int windowHeight)
{
    if ((windowWidth > windowHeight && mGeometry.width() < mGeometry.height())
     || (windowWidth < windowHeight && mGeometry.width() > mGeometry.height())) {

        // The window aspect ratio differ's from the screen one. This means that
        // unity8 has rotated the window in its scene.
        // As there's no way to express window rotation in Qt's API, we have
        // Flip QScreen's dimensions so that orientation properties match
        // (primaryOrientation particularly).
        // FIXME: This assumes a phone scenario. Won't work, or make sense,
        //        on the desktop

        QRect currGeometry = mGeometry;
        mGeometry.setWidth(currGeometry.height());
        mGeometry.setHeight(currGeometry.width());

        DLOG("UbuntuScreen::handleWindowSurfaceResize - new screen geometry (w=%d, h=%d)",
            mGeometry.width(), mGeometry.height());
        QWindowSystemInterface::handleScreenGeometryChange(screen(),
                                                           mGeometry /* newGeometry */,
                                                           mGeometry /* newAvailableGeometry */);

        if (mGeometry.width() < mGeometry.height()) {
            mCurrentOrientation = Qt::PortraitOrientation;
        } else {
            mCurrentOrientation = Qt::LandscapeOrientation;
        }
        DLOG("UbuntuScreen::handleWindowSurfaceResize - new orientation %s",orientationToStr(mCurrentOrientation));
        QWindowSystemInterface::handleScreenOrientationChange(screen(), mCurrentOrientation);
    }
}

void UbuntuScreen::setMirDisplayOutput(const MirDisplayOutput &output)
{
    // Physical screen size
    mPhysicalSize.setWidth(output.physical_width_mm);
    mPhysicalSize.setHeight(output.physical_height_mm);

    // Pixel Format
    mFormat = qImageFormatFromMirPixelFormat(output.current_format);

    // Pixel depth
    mDepth = 8 * MIR_BYTES_PER_PIXEL(output.current_format);

    // Mode = Resolution & refresh rate
    MirDisplayMode mode = output.modes[output.current_mode];
    mNativeGeometry.setX(output.position_x);
    mNativeGeometry.setY(output.position_y);
    mNativeGeometry.setWidth(mode.horizontal_resolution);
    mNativeGeometry.setHeight(mode.vertical_resolution);
    mRefreshRate = mode.refresh_rate;

    // geometry in device pixels
    mGeometry.setX(mNativeGeometry.x() / mDevicePixelRatio);
    mGeometry.setY(mNativeGeometry.y() / mDevicePixelRatio);
    mGeometry.setWidth(mNativeGeometry.width() / mDevicePixelRatio);
    mGeometry.setHeight(mNativeGeometry.height() / mDevicePixelRatio);

    // Misc
    mScale = output.scale;
    mFormFactor = output.form_factor;
    mOutputId = output.output_id;
}

QDpi UbuntuScreen::logicalDpi() const
{
    if (mDpi > 0) {
        return QDpi(mDpi, mDpi);
    } else {
        return QPlatformScreen::logicalDpi();
    }
}
