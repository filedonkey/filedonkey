win32 {
    # Dokany replaces WinFsp here, and only its core library is used: dokan2.dll, the flat C API.
    # Dokany's own FUSE wrapper, dokanfuse2.dll, is deliberately not linked - what the rest of the
    # project includes as <fuse.h> is the FUSE 3 implementation in fuse3/, which sits straight on
    # top of dokan2. See the header of fuse3/fuse3_dokan.cpp for why.
    #
    # DOKAN_DIR points at the SDK, whose directory carries its version in the name. Override it on
    # the qmake command line when the guess below is wrong:
    #
    #   qmake DOKAN_DIR="C:/Program Files/Dokan/DokanLibrary-2.3.1"
    isEmpty(DOKAN_DIR) {
        # clean_path, not a hand rolled replace(): the environment gives this back with
        # backslashes, and the escaping a regex needs to match one of those in a .pri file is
        # easy to get subtly wrong - qmake reports the bad pattern and then quietly carries on
        # with an empty result.
        DOKAN_PF = $$clean_path($$(ProgramFiles))
        isEmpty(DOKAN_PF): DOKAN_PF = C:/Program Files

        DOKAN_FOUND = $$files("$${DOKAN_PF}/Dokan/DokanLibrary-*")
        !isEmpty(DOKAN_FOUND): DOKAN_DIR = $$last(DOKAN_FOUND)
    }

    !exists("$${DOKAN_DIR}/include/dokan/dokan.h") {
        error("Dokany SDK not found under \"$${DOKAN_DIR}\" - install it from https://dokan-dev.github.io, or pass DOKAN_DIR=<path> to qmake")
    }

    # dokan2.lib, the SDK's own import library, in preference to reaching for the DLL directly.
    # It is an ordinary ar archive carrying __imp_ symbols, and x64 has no __stdcall decoration to
    # disagree about, so MinGW consumes the MSVC built one without complaint. Binding to it rather
    # than to C:/Windows/System32/dokan2.dll also keeps the import matched to the same SDK the
    # headers above came from, instead of to whichever Dokany happens to be installed at the time.
    #
    # Nothing needs copying next to the .exe either way: the installer puts dokan2.dll in system32,
    # which is where the loader will find it - unlike winfsp-x64.dll, which had to be shipped.
    isEmpty(DOKAN_LIB): DOKAN_LIB = $${DOKAN_DIR}/lib/dokan2.lib

    !exists("$${DOKAN_LIB}"): error("dokan2.lib not found at \"$${DOKAN_LIB}\" - reinstall Dokany, or pass DOKAN_LIB=<path> to qmake")

    # Ours first, and it has to stay that way: the Dokany SDK ships a <fuse.h> of its own - the
    # FUSE 2 header of the wrapper this project does not use - one directory above <dokan/dokan.h>.
    # Whichever include directory comes first is the fuse.h virtdisk.cpp gets.
    INCLUDEPATH += $$PWD/fuse3
    INCLUDEPATH += "$${DOKAN_DIR}/include"

    # Quoted, not $$shell_quote: INCLUDEPATH and LIBS are lists, and qmake splits an unquoted
    # value on whitespace long before any shell sees it - which turns "C:/Program Files/..." into
    # two entries and leaves the second one looking like a stray input file.
    LIBS += "$${DOKAN_LIB}"
    LIBS += -lws2_32

    DEFINES += _FILE_OFFSET_BITS=64
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
