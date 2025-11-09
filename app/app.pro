QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \

HEADERS += \
    mainwindow.h \

INCLUDEPATH += ../core

win32:CONFIG(release, debug|release): {
    LIBS += $$OUT_PWD/../core/release/libcore.a
    # Make sure the library is built before the test
    PRE_TARGETDEPS += $$OUT_PWD/../core/release/libcore.a
} else:win32:CONFIG(debug, debug|release): {
    LIBS += $$OUT_PWD/../core/debug/libcore.a
    PRE_TARGETDEPS += $$OUT_PWD/../core/debug/libcore.a
} else:unix: {
    LIBS += $$OUT_PWD/../core/libcore.a
    PRE_TARGETDEPS += $$OUT_PWD/../core/libcore.a
}

include(../core/dependencies.pri)

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    FileDonkey_en_US.ts

CONFIG += lrelease
CONFIG += embed_translations

win32 {
    # magick icon.png -define icon:auto-resize=16,32,48,64,96,128,256 -compress zip icon.ico
    RC_ICONS += ../assets/filedonkey_app_icon.ico
}

macx {
    # ICON
}

linux {
    # ICON
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    ../resources.qrc
