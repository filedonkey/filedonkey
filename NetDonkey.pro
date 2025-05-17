QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

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
    main.cpp \
    mainwindow.cpp \
    pread_win32.cpp \
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
    mainwindow.h \
    pread_win32.h \
    virtdisk.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    NetDonkey_en_US.ts

CONFIG += lrelease
CONFIG += embed_translations

win32 {
    # magick icon.png -define icon:auto-resize=16,32,48,64,96,128,256 -compress zip icon.ico
    RC_ICONS += assets/filedonkey_app_icon.ico
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
    CONFIG += link_pkgconfig
    PKGCONFIG += fuse
    INCLUDEPATH += /usr/include/fuse
    LIBS += -lfuse
    QMAKE_CXXFLAGS += -D_FILE_OFFSET_BITS=64
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
