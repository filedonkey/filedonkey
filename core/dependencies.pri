win32 {
    WinFsp = $$system(echo %ProgramFiles(x86)%)\WinFsp

    INCLUDEPATH += "$${WinFsp}\inc\fuse3"
    LIBS += "$${WinFsp}\lib\winfsp-x64.lib"
    DEFINES += _FILE_OFFSET_BITS=64

    WINFSP_DLL = $$shell_path($${WinFsp}/bin/winfsp-x64.dll)
    CONFIG(debug, debug|release) {
        OUT_DIR = $$shell_path($$OUT_PWD/debug)
    } else {
        OUT_DIR = $$shell_path($$OUT_PWD/release)
    }

    QMAKE_POST_LINK += xcopy /Y /Q \"$${WINFSP_DLL}\" \"$${OUT_DIR}\\\" &
}

macx {
    INCLUDEPATH += "/Library/Application Support/fuse-t/include/fuse3"
    LIBS += -L"/Library/Application Support/fuse-t/lib" -lfuse3
    DEFINES += _FILE_OFFSET_BITS=64
}

linux {
    INCLUDEPATH += /usr/include/fuse3
    LIBS += -L/usr/lib/x86_64-linux-gnu -lfuse3 -lpthread -ldl
    DEFINES += _FILE_OFFSET_BITS=64
}
