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
