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

#include "glcontext.h"
#include "window.h"
#include "logging.h"

#include <QtGui/QScreen>
#include <QtGui/QOpenGLContext>
#include <QtPlatformSupport/private/qeglconvenience_p.h>

#if !defined(QT_NO_DEBUG)
static void printEglConfig(EGLDisplay display, EGLConfig config)
{
    DASSERT(display != EGL_NO_DISPLAY);
    DASSERT(config != nullptr);
    static const struct { const EGLint attrib; const char* name; } kAttribs[] = {
        { EGL_BUFFER_SIZE, "EGL_BUFFER_SIZE" },
        { EGL_ALPHA_SIZE, "EGL_ALPHA_SIZE" },
        { EGL_BLUE_SIZE, "EGL_BLUE_SIZE" },
        { EGL_GREEN_SIZE, "EGL_GREEN_SIZE" },
        { EGL_RED_SIZE, "EGL_RED_SIZE" },
        { EGL_DEPTH_SIZE, "EGL_DEPTH_SIZE" },
        { EGL_STENCIL_SIZE, "EGL_STENCIL_SIZE" },
        { EGL_CONFIG_CAVEAT, "EGL_CONFIG_CAVEAT" },
        { EGL_CONFIG_ID, "EGL_CONFIG_ID" },
        { EGL_LEVEL, "EGL_LEVEL" },
        { EGL_MAX_PBUFFER_HEIGHT, "EGL_MAX_PBUFFER_HEIGHT" },
        { EGL_MAX_PBUFFER_PIXELS, "EGL_MAX_PBUFFER_PIXELS" },
        { EGL_MAX_PBUFFER_WIDTH, "EGL_MAX_PBUFFER_WIDTH" },
        { EGL_NATIVE_RENDERABLE, "EGL_NATIVE_RENDERABLE" },
        { EGL_NATIVE_VISUAL_ID, "EGL_NATIVE_VISUAL_ID" },
        { EGL_NATIVE_VISUAL_TYPE, "EGL_NATIVE_VISUAL_TYPE" },
        { EGL_SAMPLES, "EGL_SAMPLES" },
        { EGL_SAMPLE_BUFFERS, "EGL_SAMPLE_BUFFERS" },
        { EGL_SURFACE_TYPE, "EGL_SURFACE_TYPE" },
        { EGL_TRANSPARENT_TYPE, "EGL_TRANSPARENT_TYPE" },
        { EGL_TRANSPARENT_BLUE_VALUE, "EGL_TRANSPARENT_BLUE_VALUE" },
        { EGL_TRANSPARENT_GREEN_VALUE, "EGL_TRANSPARENT_GREEN_VALUE" },
        { EGL_TRANSPARENT_RED_VALUE, "EGL_TRANSPARENT_RED_VALUE" },
        { EGL_BIND_TO_TEXTURE_RGB, "EGL_BIND_TO_TEXTURE_RGB" },
        { EGL_BIND_TO_TEXTURE_RGBA, "EGL_BIND_TO_TEXTURE_RGBA" },
        { EGL_MIN_SWAP_INTERVAL, "EGL_MIN_SWAP_INTERVAL" },
        { EGL_MAX_SWAP_INTERVAL, "EGL_MAX_SWAP_INTERVAL" },
        { -1, NULL }
    };
    LOG("EGL configuration attibutes:");
    for (int index = 0; kAttribs[index].attrib != -1; index++) {
        EGLint value;
        if (eglGetConfigAttrib(display, config, kAttribs[index].attrib, &value))
            LOG("  %s: %d", kAttribs[index].name, static_cast<int>(value));
    }
}
#endif

static EGLenum api_in_use()
{
#ifdef QTUBUNTU_USE_OPENGL
    return EGL_OPENGL_API;
#else
    return EGL_OPENGL_ES_API;
#endif
}

UbuntuOpenGLContext::UbuntuOpenGLContext(QOpenGLContext* context)
    : mSwapInterval(-1)
{
    mEglDisplay = static_cast<UbuntuScreen*>(context->screen()->handle())->eglDisplay();
    EGLConfig config = q_configFromGLFormat(mEglDisplay, context->format());
    mSurfaceFormat = q_glFormatFromConfig(mEglDisplay, config);
#if !defined(QT_NO_DEBUG)
    printEglConfig(mEglDisplay, config);
#endif

    QVector<EGLint> attribs;
    attribs.append(EGL_CONTEXT_CLIENT_VERSION);
    attribs.append(mSurfaceFormat.majorVersion());
    attribs.append(EGL_NONE);
    ASSERT(eglBindAPI(api_in_use()) == EGL_TRUE);

    UbuntuOpenGLContext* sharedContext = static_cast<UbuntuOpenGLContext*>(context->shareHandle());
    EGLContext sharedEglContext = sharedContext ? sharedContext->eglContext() : EGL_NO_CONTEXT;
    mEglContext = eglCreateContext(mEglDisplay, config, sharedEglContext, attribs.constData());
    DASSERT(mEglContext != EGL_NO_CONTEXT);
}

UbuntuOpenGLContext::~UbuntuOpenGLContext()
{
    ASSERT(eglDestroyContext(mEglDisplay, mEglContext) == EGL_TRUE);
}

bool UbuntuOpenGLContext::makeCurrent(QPlatformSurface* surface)
{
    DASSERT(surface->surface()->surfaceType() == QSurface::OpenGLSurface);
    EGLSurface eglSurface = static_cast<UbuntuWindow*>(surface)->eglSurface();

    eglBindAPI(api_in_use());
    const bool ok = eglMakeCurrent(mEglDisplay, eglSurface, eglSurface, mEglContext);
    if (ok) {
        const int requestedSwapInterval = surface->format().swapInterval();
        if (requestedSwapInterval >= 0 && mSwapInterval != requestedSwapInterval) {
            mSwapInterval = requestedSwapInterval;
            eglSwapInterval(mEglDisplay, mSwapInterval);
        }
    } else {
        qWarning("[ubuntumirclient QPA] makeCurrent() EGL error: %x", eglGetError());
    }
    return ok;
}

void UbuntuOpenGLContext::doneCurrent()
{
#if defined(QT_NO_DEBUG)
    eglBindAPI(api_in_use());
    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#else
    ASSERT(eglBindAPI(api_in_use()) == EGL_TRUE);
    ASSERT(eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) == EGL_TRUE);
#endif
}

void UbuntuOpenGLContext::swapBuffers(QPlatformSurface* surface)
{
    UbuntuWindow *ubuntuWindow = static_cast<UbuntuWindow*>(surface);

    EGLSurface eglSurface = ubuntuWindow->eglSurface();
#if defined(QT_NO_DEBUG)
    eglBindAPI(api_in_use());
    eglSwapBuffers(mEglDisplay, eglSurface);
#else
    ASSERT(eglBindAPI(api_in_use()) == EGL_TRUE);
    ASSERT(eglSwapBuffers(mEglDisplay, eglSurface) == EGL_TRUE);
#endif

    // "Technique" copied from mir, in examples/eglapp.c around line 96
    EGLint newBufferWidth = -1;
    EGLint newBufferHeight = -1;
    /*
     * Querying the surface (actually the current buffer) dimensions here is
     * the only truly safe way to be sure that the dimensions we think we
     * have are those of the buffer being rendered to. But this should be
     * improved in future; https://bugs.launchpad.net/mir/+bug/1194384
     */
    eglQuerySurface(mEglDisplay, eglSurface, EGL_WIDTH, &newBufferWidth);
    eglQuerySurface(mEglDisplay, eglSurface, EGL_HEIGHT, &newBufferHeight);

    ubuntuWindow->onBuffersSwapped_threadSafe(newBufferWidth, newBufferHeight);
}

void (*UbuntuOpenGLContext::getProcAddress(const QByteArray& procName)) ()
{
#if defined(QT_NO_DEBUG)
    eglBindAPI(api_in_use());
#else
    ASSERT(eglBindAPI(api_in_use()) == EGL_TRUE);
#endif
    return eglGetProcAddress(procName.constData());
}
