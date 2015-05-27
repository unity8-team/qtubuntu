/*
 * Copyright (C) 2014 Canonical, Ltd.
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

// platform-api
#include <ubuntu/application/ui/display.h>
#include <ubuntu/application/ui/options.h>

#if !defined(QT_NO_DEBUG)
static void printEglInfo(EGLDisplay display)
{
    DASSERT(display != EGL_NO_DISPLAY);
    const char* string = eglQueryString(display, EGL_VENDOR);
    LOG("EGL vendor: %s", string);
    string = eglQueryString(display, EGL_VERSION);
    LOG("EGL version: %s", string);
    string = eglQueryString(display, EGL_EXTENSIONS);
    LOG("EGL extensions: %s", string);
}
#endif


const QEvent::Type OrientationChangeEvent::mType =
        static_cast<QEvent::Type>(QEvent::registerEventType());


UbuntuScreen::UbuntuScreen()
    : mFormat(QImage::Format_RGB32)
    , mDepth(32)
    , mEglDisplay(EGL_NO_DISPLAY)
{
    // Initialize EGL.
    ASSERT(eglBindAPI(EGL_OPENGL_ES_API) == EGL_TRUE);

    UAUiDisplay* u_display = ua_ui_display_new_with_index(0);
    mEglNativeDisplay = ua_ui_display_get_native_type(u_display);
    ASSERT((mEglDisplay = eglGetDisplay(mEglNativeDisplay)) != EGL_NO_DISPLAY);
    ua_ui_display_destroy(u_display);
    ASSERT(eglInitialize(mEglDisplay, nullptr, nullptr) == EGL_TRUE);
#if !defined(QT_NO_DEBUG)
    printEglInfo(mEglDisplay);
#endif

    // Get screen resolution.
    UAUiDisplay* display = ua_ui_display_new_with_index(0);
    const int kScreenWidth = ua_ui_display_query_horizontal_res(display);
    const int kScreenHeight = ua_ui_display_query_vertical_res(display);
    DASSERT(kScreenWidth > 0 && kScreenHeight > 0);
    DLOG("ubuntumirclient: screen resolution: %dx%d", kScreenWidth, kScreenHeight);
    ua_ui_display_destroy(display);

    mGeometry = QRect(0, 0, kScreenWidth, kScreenHeight);
    mAvailableGeometry = QRect(0, 0, kScreenWidth, kScreenHeight);

    DLOG("QUbuntuScreen::QUbuntuScreen (this=%p)", this);

    // Set the default orientation based on the initial screen dimmensions.
    mNativeOrientation = (mAvailableGeometry.width() >= mAvailableGeometry.height()) ? Qt::LandscapeOrientation : Qt::PortraitOrientation;

    // If it's a landscape device (i.e. some tablets), start in landscape, otherwise portrait
    mCurrentOrientation = (mNativeOrientation == Qt::LandscapeOrientation) ? Qt::LandscapeOrientation : Qt::PortraitOrientation;
}

UbuntuScreen::~UbuntuScreen()
{
    eglTerminate(mEglDisplay);
}

void UbuntuScreen::customEvent(QEvent* event) {
    DASSERT(QThread::currentThread() == thread());

    OrientationChangeEvent* oReadingEvent = static_cast<OrientationChangeEvent*>(event);
    switch (oReadingEvent->mOrientation) {
        case QOrientationReading::LeftUp: {
            mCurrentOrientation = (mNativeOrientation == Qt::LandscapeOrientation) ?
                        Qt::InvertedPortraitOrientation : Qt::LandscapeOrientation;
            break;
        }
        case QOrientationReading::TopUp: {
            mCurrentOrientation = (mNativeOrientation == Qt::LandscapeOrientation) ?
                        Qt::LandscapeOrientation : Qt::PortraitOrientation;
            break;
        }
        case QOrientationReading::RightUp: {
            mCurrentOrientation = (mNativeOrientation == Qt::LandscapeOrientation) ?
                        Qt::PortraitOrientation : Qt::InvertedLandscapeOrientation;
            break;
        }
        case QOrientationReading::TopDown: {
            mCurrentOrientation = (mNativeOrientation == Qt::LandscapeOrientation) ?
                        Qt::InvertedLandscapeOrientation : Qt::InvertedPortraitOrientation;
            break;
        }
        default: {
            DLOG("UbuntuScreen::customEvent - Unknown orientation.");
            return;
        }
    }

    // Raise the event signal so that client apps know the orientation changed
    QWindowSystemInterface::handleScreenOrientationChange(screen(), mCurrentOrientation);
    DLOG("UbuntuScreen::customEvent - handling orientation change to %d", mCurrentOrientation);
}
