# qmake for Qt 5.12+ and Qt 6.2+. Use a separate build directory per Qt kit.
# In RetroShare/plugins/RemoteDesktop, inherit the same configuration as sibling
# plugins. CONFIG+=desktop_standalone opts out for independent/shadow builds.
!contains(CONFIG, desktop_standalone) {
    exists($$PWD/../Common/retroshare_plugin.pri):exists($$PWD/../../libretroshare/src/retroshare/rsplugin.h) {
        !include($$PWD/../Common/retroshare_plugin.pri): error("Could not include RetroShare common plugin configuration")
        CONFIG += desktop_in_tree
        isEmpty(RETROSHARE_SOURCE_DIR): RETROSHARE_SOURCE_DIR = $$clean_path($$PWD/../..)
        isEmpty(RETROSHARE_QT_MAJOR): RETROSHARE_QT_MAJOR = $$QT_MAJOR_VERSION
    }
}
QT += core gui widgets network
CONFIG += c++17
CONFIG -= debug_and_release
INCLUDEPATH += $$PWD/src $$PWD/viewer
# Sharing engine (loopback VNC server + native input).
SOURCES += $$PWD/src/VncWorker.cpp $$PWD/src/NativeInput.cpp
HEADERS += $$PWD/src/VncWorker.h $$PWD/src/NativeInput.h $$PWD/src/RsVncProtocol.h
OBJECTS_DIR = obj
MOC_DIR = moc
DESTDIR = lib

lessThan(QT_MAJOR_VERSION, 5): error("Qt 5.12+ or Qt 6.2+ is required")
equals(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 12): error("Qt 5.12+ is required")
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 2): error("Qt 6.2+ is required")

# Installed LibVNC packages are the default. Set LIBVNC_BUILD_DIR only for
# an optional source build; no third_party checkout is needed in package mode.
!isEmpty(LIBVNC_BUILD_DIR) {
    isEmpty(LIBVNC_SOURCE_DIR): LIBVNC_SOURCE_DIR = $$PWD/third_party/libvncserver
    !exists($$LIBVNC_BUILD_DIR/include/rfb/rfbconfig.h): error("LIBVNC_BUILD_DIR must contain generated include/rfb/rfbconfig.h")
    INCLUDEPATH += $$LIBVNC_SOURCE_DIR/include $$LIBVNC_BUILD_DIR/include
    LIBS += -L$$quote($$LIBVNC_BUILD_DIR) -lvncclient -lvncserver
    # Override for a LibVNC build with different optional compression/crypto backends.
    isEmpty(LIBVNC_EXTRA_LIBS): LIBVNC_EXTRA_LIBS = -lssl -lcrypto -ljpeg -lpng16 -llzo2 -lz
    LIBS += $$LIBVNC_EXTRA_LIBS
} else {
    CONFIG += link_pkgconfig
    PKGCONFIG += libvncclient libvncserver
}
win32 {
    DEFINES += WIN32_LEAN_AND_MEAN
    LIBS += -luser32 -lws2_32
    win32-g++: LIBS += -lwinpthread
} else:macx {
    LIBS += -framework ApplicationServices
} else {
    LIBS += -lX11 -lXtst -ldl
}

TEMPLATE = lib
CONFIG += plugin
TARGET = RemoteDesktop
isEmpty(RETROSHARE_SOURCE_DIR): error("Set RETROSHARE_SOURCE_DIR to the host source checkout")
!exists($$RETROSHARE_SOURCE_DIR/libretroshare/src/retroshare/rsplugin.h): error("Invalid RETROSHARE_SOURCE_DIR")
isEmpty(RETROSHARE_QT_MAJOR): error("Set RETROSHARE_QT_MAJOR=5 or 6 to declare the host Qt ABI")
!equals(RETROSHARE_QT_MAJOR, $$QT_MAJOR_VERSION): error("This qmake Qt major does not match the RetroShare host")
INCLUDEPATH += $$RETROSHARE_SOURCE_DIR/libretroshare/src \
               $$RETROSHARE_SOURCE_DIR/retroshare-gui/src \
               $$RETROSHARE_SOURCE_DIR/supportlibs/rapidjson/include
DEFINES += $$RETROSHARE_DEFINES
SOURCES += $$PWD/src/Plugin.cpp $$PWD/src/Service.cpp $$PWD/src/SessionPage.cpp \
           $$PWD/src/HostDialogs.cpp $$PWD/src/ChatWindow.cpp $$PWD/src/DesktopView.cpp \
           $$PWD/viewer/ViewerWindow.cpp $$PWD/viewer/VncClient.cpp $$PWD/viewer/SystemKeys.cpp
RESOURCES += $$PWD/src/images.qrc
HEADERS += $$PWD/src/Service.h $$PWD/src/SessionPage.h $$PWD/src/HostDialogs.h $$PWD/src/ChatWindow.h \
           $$PWD/src/DesktopView.h $$PWD/viewer/ViewerWindow.h $$PWD/viewer/VncClient.h $$PWD/viewer/SystemKeys.h
win32:!contains(CONFIG, desktop_in_tree) {
    DEFINES += WINDOWS_SYS
    isEmpty(RETROSHARE_CORE_LIBRARY): error("Set RETROSHARE_CORE_LIBRARY to the matching core import library")
    isEmpty(RETROSHARE_GUI_LIBRARY): error("Set RETROSHARE_GUI_LIBRARY to the matching GUI import library")
    LIBS += $$quote($$RETROSHARE_CORE_LIBRARY) $$quote($$RETROSHARE_GUI_LIBRARY)
}
macx:!contains(CONFIG, desktop_in_tree): QMAKE_LFLAGS += -undefined dynamic_lookup
# Old RetroShare headers contain deprecated APIs and GCC 15 template diagnostics.
gcc {
    QMAKE_CXXFLAGS += -Wno-deprecated-declarations -Wno-cpp
    greaterThan(QMAKE_GCC_MAJOR_VERSION, 14): QMAKE_CXXFLAGS += -Wno-error=template-body
}
