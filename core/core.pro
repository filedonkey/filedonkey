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

win32 {
    INCLUDEPATH += "$$(ProgramFiles)/Dokan/Dokan Library-2.2.1/include"
    LIBS += "$$(ProgramFiles)/Dokan/Dokan Library-2.2.1/lib/dokanfuse2.lib"
    QMAKE_CXXFLAGS += -D_FILE_OFFSET_BITS=64
}

macx {
    INCLUDEPATH += "/usr/local/include/fuse"
    LIBS += "/usr/local/lib/libfuse-t.dylib"
    QMAKE_CXXFLAGS += -D_FILE_OFFSET_BITS=64
}

linux {
    INCLUDEPATH += /usr/include/fuse3
    LIBS += -L/usr/lib/x86_64-linux-gnu -lfuse3 -lpthread -ldl
    QMAKE_CXXFLAGS += -D_FILE_OFFSET_BITS=64
}
