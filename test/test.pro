QT += testlib
QT -= gui
QT += core

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle
CONFIG += c++20

QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.15

TEMPLATE = app

HEADERS += \
    fusebackend_spec.h

SOURCES +=  \
    main.cpp

INCLUDEPATH += ../src

win32:CONFIG(release, debug|release): {
    LIBS += $$OUT_PWD/../src/release/libsrc.a
    # Make sure the library is built before the test
    PRE_TARGETDEPS += $$OUT_PWD/../src/release/libsrc.a
} else:win32:CONFIG(debug, debug|release): {
    LIBS += $$OUT_PWD/../src/debug/libsrc.a
    PRE_TARGETDEPS += $$OUT_PWD/../src/debug/libsrc.a
} else:unix: {
    LIBS += $$OUT_PWD/../src/libsrc.a
    PRE_TARGETDEPS += $$OUT_PWD/../src/libsrc.a
}

COPIES += myFilesToCopy
myFilesToCopy.files = $$files($${PWD}/../assets/*.ico) # List files to copy
myFilesToCopy.path = $$OUT_PWD/assets # Specify destination path


create_folder.target = create_assets_folder
win32 {
    create_folder.commands = if exist $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon.ico)) if not exist $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon_symlink.ico)) mklink $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon_symlink.ico)) $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon.ico))
} else {
    create_folder.commands = test -f $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon.ico)) && test ! -L $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon_symlink.ico)) && ln -sf $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon.ico)) $$shell_quote($$shell_path($$OUT_PWD/assets/filedonkey_app_icon_symlink.ico)) || true
}
create_folder.depends = FORCE
QMAKE_EXTRA_TARGETS += create_folder
POST_TARGETDEPS += create_assets_folder
