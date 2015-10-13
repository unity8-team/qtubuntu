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

// Local
#include "clipboard.h"
#include "input.h"
#include "window.h"
#include "screen.h"
#include "utils.h"
#include "logging.h"

// Qt
#include <qpa/qwindowsysteminterface.h>
#include <qpa/qwindowsysteminterface.h>
#include <QMutex>
#include <QMutexLocker>
#include <QSize>
#include <QtMath>

// Platform API
#include <ubuntu/application/instance.h>

#include <EGL/egl.h>

#define IS_OPAQUE_FLAG 1

/*
 * Note: all geometry is in device-independent pixels, except that contained in variables with the
 * suffix "Px" - whose units are (physical) pixels
 */

namespace
{
MirSurfaceState qtWindowStateToMirSurfaceState(Qt::WindowState state)
{
    switch (state) {
    case Qt::WindowNoState:
        return mir_surface_state_restored;

    case Qt::WindowFullScreen:
        return mir_surface_state_fullscreen;

    case Qt::WindowMaximized:
        return mir_surface_state_maximized;

    case Qt::WindowMinimized:
        return mir_surface_state_minimized;

    default:
        LOG("Unexpected Qt::WindowState: %d", state);
        return mir_surface_state_restored;
    }
}

#if !defined(QT_NO_DEBUG)
const char *qtWindowStateToStr(Qt::WindowState state)
{
    switch (state) {
    case Qt::WindowNoState:
        return "NoState";

    case Qt::WindowFullScreen:
        return "FullScreen";

    case Qt::WindowMaximized:
        return "Maximized";

    case Qt::WindowMinimized:
        return "Minimized";

    default:
        return "!?";
    }
}
#endif

} // anonymous namespace

class UbuntuWindowPrivate
{
public:
    void createEGLSurface(EGLNativeWindowType nativeWindow);
    void destroyEGLSurface();
    int panelHeight();

    UbuntuScreen* screen;
    EGLSurface eglSurface;
    WId id;
    UbuntuInput* input;
    Qt::WindowState state;
    MirConnection *connection;
    MirSurface* surface;
    QSize bufferSizePx;
    QMutex mutex;
    QSharedPointer<UbuntuClipboard> clipboard;
    int resizeCatchUpAttempts;
#if !defined(QT_NO_DEBUG)
    int frameNumber;
#endif
};

static void eventCallback(MirSurface* surface, const MirEvent *event, void* context)
{
    (void) surface;
    DASSERT(context != NULL);
    UbuntuWindow* platformWindow = static_cast<UbuntuWindow*>(context);
    platformWindow->priv()->input->postEvent(platformWindow, event);
}

static void surfaceCreateCallback(MirSurface* surface, void* context)
{
    DASSERT(context != NULL);
    UbuntuWindow* platformWindow = static_cast<UbuntuWindow*>(context);
    platformWindow->priv()->surface = surface;

    mir_surface_set_event_handler(surface, eventCallback, context);
}

UbuntuWindow::UbuntuWindow(QWindow* w, QSharedPointer<UbuntuClipboard> clipboard, UbuntuScreen* screen,
                           UbuntuInput* input, MirConnection* connection)
    : QObject(nullptr), QPlatformWindow(w)
{
    DASSERT(screen != NULL);

    d = new UbuntuWindowPrivate;
    d->screen = screen;
    d->eglSurface = EGL_NO_SURFACE;
    d->input = input;
    d->state = window()->windowState();
    d->connection = connection;
    d->clipboard = clipboard;
    d->resizeCatchUpAttempts = 0;

    static int id = 1;
    d->id = id++;

#if !defined(QT_NO_DEBUG)
    d->frameNumber = 0;
#endif

    // Use client geometry if set explicitly, use available screen geometry otherwise.
    QPlatformWindow::setGeometry(window()->geometry() != screen->geometry() ?
        window()->geometry() : screen->availableGeometry());
    createWindow();
    DLOG("UbuntuWindow::UbuntuWindow (this=%p, w=%p, screen=%p, input=%p)", this, w, screen, input);
}

UbuntuWindow::~UbuntuWindow()
{
    DLOG("UbuntuWindow::~UbuntuWindow");
    d->destroyEGLSurface();

    mir_surface_release_sync(d->surface);

    delete d;
}

void UbuntuWindowPrivate::createEGLSurface(EGLNativeWindowType nativeWindow)
{
  DLOG("UbuntuWindowPrivate::createEGLSurface (this=%p, nativeWindow=%p)",
          this, reinterpret_cast<void*>(nativeWindow));

  eglSurface = eglCreateWindowSurface(screen->eglDisplay(), screen->eglConfig(),
          nativeWindow, nullptr);

  DASSERT(eglSurface != EGL_NO_SURFACE);
}

void UbuntuWindowPrivate::destroyEGLSurface()
{
    DLOG("UbuntuWindowPrivate::destroyEGLSurface (this=%p)", this);
    if (eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(screen->eglDisplay(), eglSurface);
        eglSurface = EGL_NO_SURFACE;
    }
}

// FIXME - in order to work around https://bugs.launchpad.net/mir/+bug/1346633
// we need to guess the panel height (3GU + 2DP)
int UbuntuWindowPrivate::panelHeight()
{
    const int defaultGridUnit = 8;
    int gridUnit = defaultGridUnit;
    QByteArray gridUnitString = qgetenv("GRID_UNIT_PX");
    if (!gridUnitString.isEmpty()) {
        bool ok;
        gridUnit = gridUnitString.toInt(&ok);
        if (!ok) {
            gridUnit = defaultGridUnit;
        }
    }
    qreal densityPixelRatio = static_cast<qreal>(gridUnit) / defaultGridUnit;
    return gridUnit * 3 + qFloor(densityPixelRatio) * 2;
}

namespace
{
static MirPixelFormat
mir_choose_default_pixel_format(MirConnection *connection)
{
    MirPixelFormat format[mir_pixel_formats];
    unsigned int nformats;

    mir_connection_get_available_surface_formats(connection,
        format, mir_pixel_formats, &nformats);

    return format[0];
}
}

void UbuntuWindow::createWindow()
{
    DLOG("UbuntuWindow::createWindow (this=%p)", this);

    // FIXME: remove this remnant of an old platform-api enum - needs ubuntu-keyboard update
    const int SCREEN_KEYBOARD_ROLE = 7;
    // Get surface role and flags.
    QVariant roleVariant = window()->property("role");
    int role = roleVariant.isValid() ? roleVariant.toUInt() : 1;  // 1 is the default role for apps.
    QVariant opaqueVariant = window()->property("opaque");
    uint flags = opaqueVariant.isValid() ?
        opaqueVariant.toUInt() ? static_cast<uint>(IS_OPAQUE_FLAG) : 0 : 0;

    // FIXME(loicm) Opaque flag is forced for now for non-system sessions (applications) for
    //     performance reasons.
    flags |= static_cast<uint>(IS_OPAQUE_FLAG);

    const QByteArray title = (!window()->title().isNull()) ? window()->title().toUtf8() : "Window 1"; // legacy title
    const int panelHeight = d->panelHeight();

#if !defined(QT_NO_DEBUG)
    LOG("panelHeight: '%d'", panelHeight);
    LOG("role: '%d'", role);
    LOG("flags: '%s'", (flags & static_cast<uint>(1)) ? "Opaque" : "NotOpaque");
    LOG("title: '%s'", title.constData());
#endif

    // Get surface geometry.
    QRect geometry;
    if (d->state == Qt::WindowFullScreen) {
        printf("UbuntuWindow - fullscreen geometry\n");
        geometry = screen()->geometry();
    } else if (d->state == Qt::WindowMaximized) {
        printf("UbuntuWindow - maximized geometry\n");
        geometry = screen()->availableGeometry();
        /*
         * FIXME: Autopilot relies on being able to convert coordinates relative of the window
         * into absolute screen coordinates. Mir does not allow this, see bug lp:1346633
         * Until there's a correct way to perform this transformation agreed, this horrible hack
         * guesses the transformation heuristically.
         *
         * Assumption: this method only used on phone devices!
         */
        geometry.setY(panelHeight);
    } else {
        printf("UbuntuWindow - regular geometry\n");
        geometry = this->geometry();
        geometry.setY(panelHeight);
    }

    // Convert to physical pixels when talking with Mir
    d->bufferSizePx.setWidth(geometry.width() * devicePixelRatio());
    d->bufferSizePx.setHeight(geometry.height() * devicePixelRatio());

    DLOG("[ubuntumirclient QPA] creating surface at (%d, %d) device-independent pixels with physicsl pixel size (%d, %d) and title '%s'\n",
            geometry.x(), geometry.y(), d->bufferSizePx.width(), d->bufferSizePx.height(), title.data());

    MirSurfaceSpec *spec;
    if (role == SCREEN_KEYBOARD_ROLE)
    {
        spec = mir_connection_create_spec_for_input_method(d->connection, d->bufferSizePx.width(),
            d->bufferSizePx.height(), mir_choose_default_pixel_format(d->connection));
    }
    else
    {
        spec = mir_connection_create_spec_for_normal_surface(d->connection, d->bufferSizePx.width(),
            d->bufferSizePx.height(), mir_choose_default_pixel_format(d->connection));
    }
    mir_surface_spec_set_name(spec, title.data());

    // Create platform window
    mir_wait_for(mir_surface_create(spec, surfaceCreateCallback, this));
    mir_surface_spec_release(spec);

    DASSERT(d->surface != NULL);
    d->createEGLSurface((EGLNativeWindowType)mir_buffer_stream_get_egl_native_window(mir_surface_get_buffer_stream(d->surface)));

    if (d->state == Qt::WindowFullScreen) {
    // TODO: We could set this on creation once surface spec supports it (mps already up)
        mir_wait_for(mir_surface_set_state(d->surface, mir_surface_state_fullscreen));
    }

    // Window manager can give us a final size different from what we asked for
    // so let's check what we ended up getting
    {
        MirSurfaceParameters parameters;
        mir_surface_get_parameters(d->surface, &parameters);

        geometry.setWidth(divideAndRoundUp(parameters.width, devicePixelRatio()));
        geometry.setHeight(divideAndRoundUp(parameters.height, devicePixelRatio()));

        // Assume that the buffer size matches the surface size at creation time
        d->bufferSizePx.setWidth(parameters.width);
        d->bufferSizePx.setHeight(parameters.height);
    }

    DLOG("[ubuntumirclient QPA] created surface has physical pixel size (%d, %d) and device-independent pixel size (%d, %d)",
            d->bufferSizePx.width(), d->bufferSizePx.height(), geometry.width(), geometry.height());

    // Tell Qt about the geometry.
    QWindowSystemInterface::handleGeometryChange(window(), geometry);
    QPlatformWindow::setGeometry(geometry);
}

void UbuntuWindow::moveResize(const QRect& rect)
{
    (void) rect;
    // TODO: Not yet supported by mir.
}

void UbuntuWindow::handleSurfaceResize(int widthPx, int heightPx)
{
    DLOG("UbuntuWindow::handleSurfaceResize(widthPx=%d, heightPx=%d) [%d]", widthPx, heightPx,
        d->frameNumber);
    QMutexLocker(&d->mutex);

    // The current buffer size hasn't actually changed. so just render on it and swap
    // buffers in the hope that the next buffer will match the surface size advertised
    // in this event.
    // But since this event is processed by a thread different from the one that swaps
    // buffers, you can never know if this information is already outdated as there's
    // no synchronicity whatsoever between the processing of resize events and the
    // consumption of buffers.
    if (d->bufferSizePx.width() != widthPx || d->bufferSizePx.height() != heightPx) {
        // if the next buffer doesn't have a different size, try some
        // more
        // FIXME: This is working around a mir bug! We really shound't have to
        // swap more than once to get a buffer with the new size!
        d->resizeCatchUpAttempts = 2;

        QWindowSystemInterface::handleExposeEvent(window(), geometry());
        QWindowSystemInterface::flushWindowSystemEvents();
    }
}

void UbuntuWindow::handleSurfaceFocusChange(bool focused)
{
    LOG("UbuntuWindow::handleSurfaceFocusChange(focused=%s)", focused ? "true" : "false");
    QWindow *activatedWindow = focused ? window() : nullptr;

    // System clipboard contents might have changed while this window was unfocused and wihtout
    // this process getting notified about it because it might have been suspended (due to
    // application lifecycle policies), thus unable to listen to any changes notified through
    // D-Bus.
    // Therefore let's ensure we are up to date with the system clipboard now that we are getting
    // focused again.
    if (focused) {
        d->clipboard->requestDBusClipboardContents();
    }

    QWindowSystemInterface::handleWindowActivated(activatedWindow, Qt::ActiveWindowFocusReason);
}

void UbuntuWindow::setWindowState(Qt::WindowState state)
{
    QMutexLocker(&d->mutex);
    DLOG("UbuntuWindow::setWindowState (this=%p, %s)", this,  qtWindowStateToStr(state));

    if (state == d->state)
        return;

    // TODO: Perhaps we should check if the states are applied?
    mir_wait_for(mir_surface_set_state(d->surface, qtWindowStateToMirSurfaceState(state)));
    d->state = state;
}

void UbuntuWindow::setGeometry(const QRect& rect)
{
    DLOG("UbuntuWindow::setGeometry (this=%p)", this);

    bool doMoveResize;

    {
        QMutexLocker(&d->mutex);
        QPlatformWindow::setGeometry(rect);
        doMoveResize = d->state != Qt::WindowFullScreen && d->state != Qt::WindowMaximized;
    }

    if (doMoveResize) {
        moveResize(rect);
    }
}

void UbuntuWindow::setVisible(bool visible)
{
    QMutexLocker(&d->mutex);
    DLOG("UbuntuWindow::setVisible (this=%p, visible=%s)", this, visible ? "true" : "false");

    if (visible) {
        mir_wait_for(mir_surface_set_state(d->surface, qtWindowStateToMirSurfaceState(d->state)));

        QWindowSystemInterface::handleExposeEvent(window(), QRect());
        QWindowSystemInterface::flushWindowSystemEvents();
    } else {
        // TODO: Use the new mir_surface_state_hidden state instead of mir_surface_state_minimized.
        //       Will have to change qtmir and unity8 for that.
        mir_wait_for(mir_surface_set_state(d->surface, mir_surface_state_minimized));
    }
}

qreal UbuntuWindow::devicePixelRatio() const
{
    return screen() ? screen()->devicePixelRatio() : 1.0; // not impossible a Window has no attached Screen
}

void* UbuntuWindow::eglSurface() const
{
    return d->eglSurface;
}

WId UbuntuWindow::winId() const
{
    return d->id;
}

void UbuntuWindow::onBuffersSwapped_threadSafe(int newBufferWidthPx, int newBufferHeightPx)
{
    QMutexLocker(&d->mutex);

    bool sizeKnown = newBufferWidthPx > 0 && newBufferHeightPx > 0;

#if !defined(QT_NO_DEBUG)
    ++d->frameNumber;
#endif

    if (sizeKnown && (d->bufferSizePx.width() != newBufferWidthPx ||
                d->bufferSizePx.height() != newBufferHeightPx)) {
        d->resizeCatchUpAttempts = 0;

        DLOG("UbuntuWindow::onBuffersSwapped_threadSafe [%d] - buffer size changed from (%d,%d) to (%d,%d)"
               " resizeCatchUpAttempts=%d",
               d->frameNumber, d->bufferSizePx.width(), d->bufferSizePx.height(), newBufferWidthPx, newBufferHeightPx,
               d->resizeCatchUpAttempts);

        d->bufferSizePx.rwidth() = newBufferWidthPx;
        d->bufferSizePx.rheight() = newBufferHeightPx;

        QRect newGeometry(geometry());
        newGeometry.setWidth(divideAndRoundUp(d->bufferSizePx.width(), devicePixelRatio()));
        newGeometry.setHeight(divideAndRoundUp(d->bufferSizePx.height(), devicePixelRatio()));

        QPlatformWindow::setGeometry(newGeometry);
        QWindowSystemInterface::handleGeometryChange(window(), newGeometry);
    } else if (d->resizeCatchUpAttempts > 0) {
        --d->resizeCatchUpAttempts;
        DLOG("UbuntuWindow::onBuffersSwapped_threadSafe [%d] - buffer size (%d,%d). Redrawing to catch up a resized buffer."
               " resizeCatchUpAttempts=%d",
               d->frameNumber, d->bufferSizePx.width(), d->bufferSizePx.height(), d->resizeCatchUpAttempts);
        QWindowSystemInterface::handleExposeEvent(window(), geometry());
    } else {
        DLOG("UbuntuWindow::onBuffersSwapped_threadSafe [%d] - buffer size (%d,%d). resizeCatchUpAttempts=%d",
               d->frameNumber, d->bufferSizePx.width(), d->bufferSizePx.height(), d->resizeCatchUpAttempts);
    }
}