QT       += core network

TEMPLATE=lib
CONFIG += staticlib
CONFIG += c++20

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    dokanbackend_win32.cpp \
    fusebackend_linux.cpp \
    fusebackend_macos.cpp \
    fusebackend_win32.cpp \
    fuseclient.cpp \
    lstat_win32.cpp \
    pread_win32.cpp \
    pwrite_win32.cpp \
    readlink_win32.cpp \
    statvfs_win32.cpp \
    virtdisk_linux.cpp \
    virtdisk_macos.cpp \
    virtdisk_win32.cpp \
    virtdisk_fuse.cpp

HEADERS += \
    connection.h \
    core.h \
    dokanbackend.h \
    fusebackend.h \
    fusebackend_types.h \
    fuseclient.h \
    lstat_win32.h \
    pread_win32.h \
    pwrite_win32.h \
    readlink_win32.h \
    statvfs_win32.h \
    virtdisk.h

CONFIG += lrelease

include(./dependencies.pri)
