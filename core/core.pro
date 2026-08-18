QT       += core network

TEMPLATE=lib
CONFIG += staticlib
CONFIG += c++20

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

macx {
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.15
}

SOURCES += \
    fusebackend.cpp \
    fuseclient.cpp \
    localnode.cpp \
    posix_win32.cpp \
    tcpkeepalive.cpp \
    virtdisk.cpp

# The FUSE 3 API on Windows, which has no libfuse of its own. Elsewhere <fuse.h> is the real thing.
win32 {
    SOURCES += fuse3/fuse3_dokan.cpp

    HEADERS += \
        fuse3/fuse.h \
        fuse3/fuse_win32.h
}

HEADERS += \
    connection.h \
    core.h \
    fusebackend.h \
    fusebackend_types.h \
    fuseclient.h \
    localnode.h \
    posix_win32.h \
    tcpkeepalive.h \
    virtdisk.h

CONFIG += lrelease

include(./dependencies.pri)
