QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    virtdisk_linux.cpp \
    virtdisk_macos.cpp \
    virtdisk_win32.cpp

HEADERS += \
    connection.h \
    mainwindow.h \
    virtdisk.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    NetDonkey_en_US.ts
CONFIG += lrelease
CONFIG += embed_translations

win32 {
    RC_ICONS += assets/donkey-dark-icon.ico
    INCLUDEPATH += "$$(ProgramFiles)/Dokan/Dokan Library-2.2.1/include"
    LIBS += "$$(ProgramFiles)/Dokan/Dokan Library-2.2.1/lib/dokan2.lib"
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
