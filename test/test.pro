QT += testlib
QT -= gui
QT += core

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle
CONFIG += c++17

TEMPLATE = app

HEADERS += \
    fusebackend_spec.h

SOURCES +=  \
    main.cpp

INCLUDEPATH += ../src

win32:CONFIG(release, debug|release): {
    # Link against the static library
    LIBS += $$OUT_PWD/../src/release/libsrc.a
    # Make sure the library is built before the test
    PRE_TARGETDEPS += $$OUT_PWD/../src/release/libsrc.a
} else:win32:CONFIG(debug, debug|release): {
    # Link against the static library
    LIBS += $$OUT_PWD/../src/debug/libsrc.a
    # Make sure the library is built before the test
    PRE_TARGETDEPS += $$OUT_PWD/../src/debug/libsrc.a
}
# else:unix: LIBS += -L$$OUT_PWD/../src/ -lmylib
