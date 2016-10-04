/****************************************************************************
**
** Copyright (C) 2016 Canonical, Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtGui module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qmirclientglcontext.h"
#include "qmirclientlogging.h"
#include "qmirclientoffscreensurface.h"
#include "qmirclientwindow.h"

#include <QOpenGLFramebufferObject>
#include <QtPlatformSupport/private/qeglconvenience_p.h>
#include <QtGui/private/qopenglcontext_p.h>

Q_LOGGING_CATEGORY(mirclientGraphics, "qt.qpa.mirclient.graphics", QtWarningMsg)

namespace {

void printEglConfig(EGLDisplay display, EGLConfig config)
{
    Q_ASSERT(display != EGL_NO_DISPLAY);
    Q_ASSERT(config != nullptr);

    const char *string = eglQueryString(display, EGL_VENDOR);
    qCDebug(mirclientGraphics, "EGL vendor: %s", string);

    string = eglQueryString(display, EGL_VERSION);
    qCDebug(mirclientGraphics, "EGL version: %s", string);

    string = eglQueryString(display, EGL_EXTENSIONS);
    qCDebug(mirclientGraphics, "EGL extensions: %s", string);

    qCDebug(mirclientGraphics, "EGL configuration attributes:");
    q_printEglConfig(display, config);
}

} // anonymous namespace

QMirClientOpenGLContext::QMirClientOpenGLContext(const QSurfaceFormat &format, QPlatformOpenGLContext *share,
                                         EGLDisplay display)
    : QEGLPlatformContext(format, share, display, 0)
{
    if (mirclientGraphics().isDebugEnabled()) {
        printEglConfig(display, eglConfig());
    }
}

static bool needsFBOReadBackWorkaround()
{
    static bool set = false;
    static bool needsWorkaround = false;

    if (Q_UNLIKELY(!set)) {
        const char *rendererString = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        needsWorkaround = qstrncmp(rendererString, "Mali-400", 8) == 0
                          || qstrncmp(rendererString, "Mali-T7", 7) == 0
                          || qstrncmp(rendererString, "PowerVR Rogue G6200", 19) == 0;
        set = true;
    }

    return needsWorkaround;
}

bool QMirClientOpenGLContext::makeCurrent(QPlatformSurface* surface)
{
    Q_ASSERT(surface->surface()->surfaceType() == QSurface::OpenGLSurface);

    if (surface->surface()->surfaceClass() == QSurface::Offscreen) {
        auto offscreen = static_cast<QMirClientOffscreenSurface *>(surface);
        if (!offscreen->buffer()) {
            auto buffer = new QOpenGLFramebufferObject(surface->surface()->size());
            offscreen->setBuffer(buffer);
        }
        return offscreen->buffer()->bind();
    } else {
        const bool ret = QEGLPlatformContext::makeCurrent(surface);

        if (Q_LIKELY(ret)) {
            QOpenGLContextPrivate *ctx_d = QOpenGLContextPrivate::get(context());
            if (!ctx_d->workaround_brokenFBOReadBack && needsFBOReadBackWorkaround()) {
                ctx_d->workaround_brokenFBOReadBack = true;
            }
        }

        return ret;
    }
}

// Following method used internally in the base class QEGLPlatformContext to access
// the egl surface of a QPlatformSurface/QMirClientWindow
EGLSurface QMirClientOpenGLContext::eglSurfaceForPlatformSurface(QPlatformSurface *surface)
{
    auto ubuntuWindow = static_cast<QMirClientWindow *>(surface);
    return ubuntuWindow->eglSurface();
}

void QMirClientOpenGLContext::swapBuffers(QPlatformSurface *surface)
{
    QEGLPlatformContext::swapBuffers(surface);

    // notify window on swap completion
    auto ubuntuWindow = static_cast<QMirClientWindow *>(surface);
    ubuntuWindow->onSwapBuffersDone();
}
