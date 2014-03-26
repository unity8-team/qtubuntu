TARGET = ubuntubase
TEMPLATE = lib

QT += core-private gui-private platformsupport-private

DEFINES += MESA_EGL_NO_X11_HEADERS
QMAKE_CXXFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden -std=c++11
QMAKE_LFLAGS += -Wl,-no-undefined

CONFIG(debug) {
  QMAKE_CXXFLAGS_DEBUG += -Werror
}

SOURCES = integration.cc \
          backing_store.cc \
          native_interface.cc \
          context.cc \
          screen.cc \
          window.cc \
          input.cc \
          theme.cc \
          platformservices.cc \
          clipboard.cc \
          pluggableinputfilter.cpp

HEADERS = integration.h \
          backing_store.h \
          native_interface.h \
          context.h \
          screen.h \
          window.h \
          input.h \
          logging.h \
          theme.h \
          platformservices.h \
          clipboard.h \
          pluggableinputfilter.h

CONFIG += static create_prl egl qpa/genericunixfontdatabase

INCLUDEPATH += .. /usr/include/hybris

install_headers.path = /usr/include/qtubuntu
install_headers.files = pluggableinputfilter.h

INSTALLS += install_headers
