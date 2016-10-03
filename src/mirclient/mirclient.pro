TARGET = qpa-ubuntumirclient
TEMPLATE = lib

QT -= gui
QT += core-private platformsupport-private dbus

CONFIG += plugin no_keywords qpa/genericunixfontdatabase

DEFINES += MESA_EGL_NO_X11_HEADERS
# CONFIG += c++11 # only enables C++0x
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden -std=c++11 -Werror -Wall
QMAKE_LFLAGS += -std=c++11 -Wl,-no-undefined

CONFIG += link_pkgconfig
PKGCONFIG += egl mirclient ubuntu-platform-api xkbcommon libcontent-hub

SOURCES = \
    qmirclientbackingstore.cpp \
    qmirclientclipboard.cpp \
    qmirclientcursor.cpp \
    qmirclientglcontext.cpp \
    qmirclientinput.cpp \
    qmirclientintegration.cpp \
    qmirclientnativeinterface.cpp \
    qmirclientoffscreensurface.cpp \
    qmirclientplatformservices.cpp \
    qmirclientplugin.cpp \
    qmirclientscreen.cpp \
    qmirclientscreenobserver.cpp \
    qmirclienttheme.cpp \
    qmirclientwindow.cpp

HEADERS = \
    qmirclientbackingstore.h \
    qmirclientclipboard.h \
    qmirclientcursor.h \
    qmirclientglcontext.h \
    qmirclientinput.h \
    qmirclientintegration.h \
    qmirclientlogging.h \
    qmirclientnativeinterface.h \
    qmirclientoffscreensurface.h \
    qmirclientorientationchangeevent_p.h \
    qmirclientplatformservices.h \
    qmirclientplugin.h \
    qmirclientscreenobserver.h \
    qmirclientscreen.h \
    qmirclienttheme.h \
    qmirclientwindow.h

# Installation path
target.path +=  $$[QT_INSTALL_PLUGINS]/platforms

INSTALLS += target
