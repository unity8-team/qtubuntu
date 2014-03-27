TARGET = qubuntumirserver
TEMPLATE = lib

QT += gui-private platformsupport-private sensors

DEFINES += MESA_EGL_NO_X11_HEADERS
QMAKE_CXXFLAGS += -std=c++11
QMAKE_LFLAGS += -Wl,-no-undefined

CONFIG(debug) {
  QMAKE_CXXFLAGS_DEBUG += -Werror
}

SOURCES = main.cc \  
    integration.cc \
    input.cc

CONFIG += plugin link_prl link_pkgconfig

PKGCONFIG += egl
INCLUDEPATH += ../../../ ../../ ../
LIBS += -Wl,--whole-archive -L../../../base -lubuntubase -L../../ubuntucommon -lqubuntucommon  -L../ubuntumircommon -lqubuntumircommon -Wl,--no-whole-archive -lubuntu_application_api_mirserver

OTHER_FILES += ubuntu.json

target.path += $$[QT_INSTALL_PLUGINS]/platforms

HEADERS += \
    integration.h \
    input.h

install_headers.path = /usr/include/qtubuntu
install_headers.files = ../../../base/pluggableinputfilter.h

### Generate pkg-config file
CONFIG += create_prl no_install_prl

QMAKE_PKGCONFIG_NAME = QtUbuntu
QMAKE_PKGCONFIG_DESCRIPTION = Interface into the QPA plugin of Mir
QMAKE_PKGCONFIG_PREFIX = $$[INSTALLBASE]]
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_INCDIR = $$installheaders.path
QMAKE_PKGCONFIG_VERSION = 0.1
QMAKE_PKGCONFIG_REQUIRES = ubuntu-platform-api mirclient mirserver Qt5Core
QMAKE_PKGCONFIG_DESTDIR = $$[QT_INSTALL_LIBS]/pkgconfig

INSTALLS += target install_headers

