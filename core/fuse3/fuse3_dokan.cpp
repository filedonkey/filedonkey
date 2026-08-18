// FUSE 3 on Dokan, with nothing in between.
//
// Dokany ships a FUSE wrapper of its own, dokanfuse2.dll, and this deliberately does not use it.
// That wrapper stops at FUSE 2.7 - no fuse_file_info on getattr, a four argument readdir filler,
// the older truncate and rename - so going through it would mean translating FUSE 3 down to
// FUSE 2 and letting the wrapper translate that to Win32, twice for every call. It is also C++
// built by MSVC, and it hands struct FUSE_STAT across the DLL boundary written in terms of the
// CRT's mode_t, which is two bytes under MinGW and four under MSVC; the struct is 120 bytes on
// this side and 128 on that one, and nothing says so at build time.
//
// dokan2.dll underneath it has neither problem. It is a flat C API, __stdcall, spelled entirely in
// Windows types that both compilers lay out identically, and it is the same interface Dokany's own
// C#, Delphi and Rust bindings are written against.
//
// WHAT WINDOWS ASKS FOR, AND WHERE IT GOES
//
//   ZwCreateFile ......... getattr, then create / mkdir / truncate, and open
//   Cleanup .............. unlink or rmdir, when the delete was accepted earlier
//   CloseFile ............ nothing
//   ReadFile ............. read
//   WriteFile ............ write
//   GetFileInformation ... getattr
//   FindFiles ............ readdir, and getattr per entry only if readdir gave no stat
//   DeleteFile ........... nothing yet - see DeleteFile below, Windows is asking permission
//   DeleteDirectory ...... readdir, to answer whether the directory is empty
//   MoveFile ............. rename
//   SetEndOfFile ......... truncate
//   SetAllocationSize .... truncate
//   GetDiskFreeSpace ..... statfs
//   GetVolumeInformation . the volname and fsname mount options
//   SetFileTime .......... utimens
//   SetFileAttributes .... chmod, for the read only bit alone
//   FlushFileBuffers ..... fsync
//
// Everything else Dokan can ask for - alternate data streams, byte range locks, security
// descriptors - is left unimplemented, which is a supported answer: Dokan falls back to its own
// default behaviour for each. Nothing in FileDonkey has anything to say about them.
//
// ONE MOUNT PER struct fuse, AND SEVERAL AT ONCE. FileDonkey mounts every peer on a thread of its
// own. Nothing here is file scope state for that reason - the fuse a callback belongs to travels
// in DOKAN_OPTIONS::GlobalContext, which Dokan hands back through DOKAN_FILE_INFO.

#include "fuse.h"

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

#include <dokan/dokan.h>

#include <errno.h>
#include <fcntl.h>

#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <vector>

// The file type and permission bits, spelled here rather than taken from <sys/stat.h>: MinGW's
// has no S_IFLNK at all, and its S_IF* live in a mode_t two bytes wide. These are POSIX's values.
#define FD_IFMT   0170000
#define FD_IFLNK  0120000
#define FD_IFREG  0100000
#define FD_IFDIR  0040000

// 100 nanosecond ticks between 1601-01-01 and 1970-01-01, which is the whole of the difference
// between a FILETIME and a POSIX timestamp.
static const int64_t EPOCH_TICKS = 116444736000000000LL;

// ---------------------------------------------------------------------------------------------
// The mount.
// ---------------------------------------------------------------------------------------------

struct fuse
{
    struct fuse_operations ops = {};
    void *private_data = nullptr;

    std::string  mountpoint;
    std::wstring mountpointW;

    // From -o volname= and -o fsname=. Windows shows the first in Explorer and reports the second
    // as the filesystem type, so both are worth carrying even though neither reaches the peer.
    std::wstring volumeName    = L"FileDonkey";
    std::wstring fileSystemName = L"FileDonkey";

    // From -o uid= and -o gid=, only so that fuse_get_context() has something to report. Dokan
    // does not work in POSIX ids - it decides access from the Windows token of the calling
    // process - so unlike WinFsp there is nothing here for these to configure.
    fuse_uid_t uid = 0;
    fuse_gid_t gid = 0;

    bool readOnly = false;

    DOKAN_OPTIONS    dokanOptions = {};
    DOKAN_OPERATIONS dokanOps     = {};
    DOKAN_HANDLE     instance     = nullptr;

    // fuse_exit runs on the GUI thread while fuse_loop is on the mount's own, and every caller
    // asks more than once - see the comment on VirtDisk::stop.
    std::mutex mutex;
    bool exiting = false;
};

// DokanInit and DokanShutdown are per process, not per mount, and every mount here is one of
// several. Counted so the first mount starts the library and the last one to go stops it.
static std::mutex g_libraryMutex;
static int        g_libraryUsers = 0;

static void LibraryAcquire()
{
    std::lock_guard<std::mutex> lock(g_libraryMutex);
    if (g_libraryUsers++ == 0) DokanInit();
}

static void LibraryRelease()
{
    std::lock_guard<std::mutex> lock(g_libraryMutex);
    if (--g_libraryUsers == 0) DokanShutdown();
}

// ---------------------------------------------------------------------------------------------
// Paths, times, modes, errors.
// ---------------------------------------------------------------------------------------------

// Windows hands paths down as UTF-16 with backslashes; the file system above, and the peer behind
// it, speak UTF-8 with forward slashes. The root arrives as a lone backslash and has to leave as
// "/", which is why the empty result is filled in rather than returned as it is.
static std::string ToPosixPath(LPCWSTR wide)
{
    if (!wide || !*wide) return "/";

    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "/";

    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], n, nullptr, nullptr);

    for (char &c : out)
        if (c == '\\') c = '/';

    if (out.empty()) return "/";
    return out;
}

static std::wstring ToWide(const char *utf8)
{
    if (!utf8 || !*utf8) return std::wstring();

    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 1) return std::wstring();

    std::wstring out(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out[0], n);
    return out;
}

static FILETIME ToFileTime(const struct fuse_timespec &ts)
{
    int64_t ticks = ts.tv_sec * 10000000LL + ts.tv_nsec / 100 + EPOCH_TICKS;
    if (ticks < 0) ticks = 0;

    FILETIME ft;
    ft.dwLowDateTime  = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return ft;
}

static void FromFileTime(const FILETIME *ft, struct fuse_timespec *ts)
{
    if (!ft || (ft->dwLowDateTime == 0 && ft->dwHighDateTime == 0))
    {
        // Windows sends a zero FILETIME for "leave this one alone", and 0xFFFFFFFF.. for "this
        // handle is busy, do not change it either". Both mean the same thing here.
        ts->tv_sec  = 0;
        ts->tv_nsec = 0;
        return;
    }

    int64_t ticks = (static_cast<int64_t>(ft->dwHighDateTime) << 32 | ft->dwLowDateTime)
                    - EPOCH_TICKS;

    ts->tv_sec  = ticks / 10000000LL;
    ts->tv_nsec = static_cast<int32_t>((ticks % 10000000LL) * 100);
}

static bool IsTimeSet(const FILETIME *ft)
{
    return ft && (ft->dwLowDateTime != 0 || ft->dwHighDateTime != 0)
              && !(ft->dwLowDateTime == 0xFFFFFFFFu && ft->dwHighDateTime == 0xFFFFFFFFu);
}

static DWORD ToFileAttributes(fuse_mode_t mode)
{
    DWORD attrs = 0;

    switch (mode & FD_IFMT)
    {
    case FD_IFDIR: attrs |= FILE_ATTRIBUTE_DIRECTORY;      break;
    case FD_IFLNK: attrs |= FILE_ATTRIBUTE_REPARSE_POINT;  break;
    default:                                               break;
    }

    // No owner write bit is the closest thing POSIX has to the read only attribute. Windows is
    // strict about this one: an attribute word of zero is not a valid answer, so NORMAL stands in
    // when nothing else applies.
    if ((mode & 0200) == 0) attrs |= FILE_ATTRIBUTE_READONLY;

    return attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
}

// What the file system returns is a negative errno, the way FUSE has always done it. Windows wants
// an NTSTATUS. Only the codes a file system actually produces are listed; anything else becomes a
// plain failure rather than something misleadingly specific.
static NTSTATUS ToNtStatus(int err)
{
    if (err >= 0) return STATUS_SUCCESS;

    switch (-err)
    {
    case ENOENT:        return STATUS_OBJECT_NAME_NOT_FOUND;
    case EACCES:        return STATUS_ACCESS_DENIED;
    case EPERM:         return STATUS_ACCESS_DENIED;
    case EEXIST:        return STATUS_OBJECT_NAME_COLLISION;
    case ENOTDIR:       return STATUS_NOT_A_DIRECTORY;
    case EISDIR:        return STATUS_FILE_IS_A_DIRECTORY;
    case ENOTEMPTY:     return STATUS_DIRECTORY_NOT_EMPTY;
    case ENOSPC:        return STATUS_DISK_FULL;
    case EROFS:         return STATUS_MEDIA_WRITE_PROTECTED;
    case ENAMETOOLONG:  return STATUS_NAME_TOO_LONG;
    case EINVAL:        return STATUS_INVALID_PARAMETER;
    case ENOSYS:        return STATUS_NOT_IMPLEMENTED;
    case ENOMEM:        return STATUS_INSUFFICIENT_RESOURCES;
    case EIO:           return STATUS_UNEXPECTED_IO_ERROR;
    case EBUSY:         return STATUS_DEVICE_BUSY;
    case EXDEV:         return STATUS_NOT_SAME_DEVICE;
    case ECONNABORTED:  return STATUS_CONNECTION_ABORTED;
    case ETIMEDOUT:     return STATUS_IO_TIMEOUT;
    default:            return STATUS_UNSUCCESSFUL;
    }
}

// ---------------------------------------------------------------------------------------------
// The context a callback runs in.
// ---------------------------------------------------------------------------------------------

// fuse_get_context() is how a FUSE filesystem reaches its own private_data, and it takes no
// argument - the answer has to be waiting on the calling thread before the callback runs. Dokan
// gives every callback the mount it belongs to, through GlobalContext, so each one opens with a
// Frame that puts it there and takes it away again on the way out. Nested frames are possible -
// FindFiles calls getattr - so the previous one is restored rather than cleared.
static thread_local struct fuse_context *tls_context = nullptr;

struct Frame
{
    struct fuse_context ctx;
    struct fuse_context *previous;

    Frame(struct fuse *f, PDOKAN_FILE_INFO info)
    {
        ctx.fuse         = f;
        ctx.uid          = f ? f->uid : 0;
        ctx.gid          = f ? f->gid : 0;
        ctx.pid          = info ? static_cast<int>(info->ProcessId) : 0;
        ctx.private_data = f ? f->private_data : nullptr;

        previous    = tls_context;
        tls_context = &ctx;
    }

    ~Frame() { tls_context = previous; }
};

static struct fuse *FuseOf(PDOKAN_FILE_INFO info)
{
    if (!info || !info->DokanOptions) return nullptr;
    return reinterpret_cast<struct fuse *>(info->DokanOptions->GlobalContext);
}

// ---------------------------------------------------------------------------------------------
// Dokan callbacks.
// ---------------------------------------------------------------------------------------------

// Windows has one entry point where POSIX has open, creat, mkdir and truncate, and it describes
// what it wants in NT terms rather than in open(2) flags. DokanMapKernelToUserCreateFileFlags
// turns the kernel's half of that back into the CreateFile arguments an application would have
// written, and the rest of this function is the step from there to POSIX.
static NTSTATUS DOKAN_CALLBACK FdCreateFile(LPCWSTR fileName,
                                            PDOKAN_IO_SECURITY_CONTEXT securityContext,
                                            ACCESS_MASK desiredAccess, ULONG fileAttributes,
                                            ULONG shareAccess, ULONG createDisposition,
                                            ULONG createOptions, PDOKAN_FILE_INFO info)
{
    (void)securityContext;
    (void)shareAccess;

    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;

    Frame frame(f, info);

    const std::string path = ToPosixPath(fileName);

    ACCESS_MASK genericAccess = 0;
    DWORD       creationFlags = 0;
    DWORD       disposition   = 0;

    DokanMapKernelToUserCreateFileFlags(desiredAccess, fileAttributes, createOptions,
                                        createDisposition, &genericAccess, &creationFlags,
                                        &disposition);

    const bool wantsWrite = (genericAccess & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA
                                              | GENERIC_ALL)) != 0;
    if (wantsWrite && f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    // Does it exist, and what is it? Every branch below turns on this one answer, and getattr is
    // one round trip to the peer either way, so it is asked once here rather than per branch.
    struct fuse_stat st = {};
    const int statRc = f->ops.getattr ? f->ops.getattr(path.c_str(), &st, nullptr) : -ENOSYS;

    const bool exists = statRc == 0;
    const bool isDir  = exists && (st.st_mode & FD_IFMT) == FD_IFDIR;

    // Windows opens a directory to enumerate it, to delete it, and on the way through to anything
    // inside it. FILE_DIRECTORY_FILE says a directory is what the caller meant; FILE_NON_DIRECTORY
    // _FILE says it explicitly is not, and is how "open X, and fail if X is a folder" is spelled.
    const bool directoryRequested = (createOptions & FILE_DIRECTORY_FILE) != 0;

    if (directoryRequested || (exists && isDir))
    {
        if (exists && !isDir) return STATUS_NOT_A_DIRECTORY;

        if (!exists)
        {
            if (disposition == CREATE_NEW || disposition == OPEN_ALWAYS ||
                disposition == CREATE_ALWAYS)
            {
                if (!f->ops.mkdir) return STATUS_NOT_IMPLEMENTED;

                int rc = f->ops.mkdir(path.c_str(), 0777);
                if (rc != 0) return ToNtStatus(rc);
            }
            else
            {
                return STATUS_OBJECT_NAME_NOT_FOUND;
            }
        }
        else if (disposition == CREATE_NEW)
        {
            return STATUS_OBJECT_NAME_COLLISION;
        }

        // The one field Dokan requires back from this callback. Get it wrong and the enumeration
        // that follows is never asked for.
        info->IsDirectory = TRUE;

        if (createOptions & FILE_DELETE_ON_CLOSE) info->DeletePending = TRUE;

        return STATUS_SUCCESS;
    }

    if (createOptions & FILE_NON_DIRECTORY_FILE && exists && isDir)
        return STATUS_FILE_IS_A_DIRECTORY;

    // From here it is a regular file, and what is left is the open(2) flags that describe the
    // same request. The file system gets them through create; open in FUSE 3 carries them too.
    int flags = 0;

    const bool wantsRead = (genericAccess & (GENERIC_READ | FILE_READ_DATA | GENERIC_ALL)) != 0;

    if (wantsRead && wantsWrite)  flags = O_RDWR;
    else if (wantsWrite)          flags = O_WRONLY;
    else                          flags = O_RDONLY;

    if ((genericAccess & FILE_APPEND_DATA) && !(genericAccess & FILE_WRITE_DATA))
        flags |= O_APPEND;

    bool create   = false;
    bool truncate = false;

    switch (disposition)
    {
    case CREATE_NEW:
        if (exists) return STATUS_OBJECT_NAME_COLLISION;
        create = true;
        flags |= O_CREAT | O_EXCL;
        break;

    case CREATE_ALWAYS:
        create   = !exists;
        truncate = exists;
        flags   |= O_CREAT | O_TRUNC;
        break;

    case OPEN_EXISTING:
        if (!exists) return STATUS_OBJECT_NAME_NOT_FOUND;
        break;

    case OPEN_ALWAYS:
        create = !exists;
        flags |= O_CREAT;
        break;

    case TRUNCATE_EXISTING:
        if (!exists) return STATUS_OBJECT_NAME_NOT_FOUND;
        truncate = true;
        flags   |= O_TRUNC;
        break;

    default:
        return STATUS_INVALID_PARAMETER;
    }

    struct fuse_file_info fi = {};
    fi.flags = flags;

    if (create)
    {
        if (!f->ops.create) return STATUS_NOT_IMPLEMENTED;

        int rc = f->ops.create(path.c_str(), 0666, &fi);
        if (rc != 0) return ToNtStatus(rc);
    }
    else
    {
        if (truncate && f->ops.truncate)
        {
            int rc = f->ops.truncate(path.c_str(), 0, nullptr);
            if (rc != 0) return ToNtStatus(rc);
        }

        if (f->ops.open)
        {
            int rc = f->ops.open(path.c_str(), &fi);
            if (rc != 0) return ToNtStatus(rc);
        }
    }

    // Whatever the file system put in fi.fh rides along in Dokan's own per handle slot, so a
    // filesystem that works in handles gets it back on every later call. FileDonkey is entirely
    // path based and leaves it zero.
    info->Context = fi.fh;

    if (createOptions & FILE_DELETE_ON_CLOSE) info->DeletePending = TRUE;

    // Windows expects to be told when a create turned into an open of something already there.
    // It is a success code, not an error, and applications test for it.
    if (exists && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS))
        return STATUS_OBJECT_NAME_COLLISION;

    return STATUS_SUCCESS;
}

// The deletion actually happens here rather than in DeleteFile, because that is where Windows puts
// it: DeleteFile is asked first, and only to find out whether the delete would be allowed - see
// the note there. By the time Cleanup runs the last handle is gone and the answer was yes.
static void DOKAN_CALLBACK FdCleanup(LPCWSTR fileName, PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f) return;

    Frame frame(f, info);

    if (!info->DeletePending) return;

    const std::string path = ToPosixPath(fileName);

    if (info->IsDirectory)
    {
        if (f->ops.rmdir) f->ops.rmdir(path.c_str());
    }
    else
    {
        if (f->ops.unlink) f->ops.unlink(path.c_str());
    }
}

static void DOKAN_CALLBACK FdCloseFile(LPCWSTR fileName, PDOKAN_FILE_INFO info)
{
    (void)fileName;

    // FUSE's release would go here. Nothing in this project opens a handle to release, and the
    // path based operations above hold nothing that needs letting go of.
    if (info) info->Context = 0;
}

static NTSTATUS DOKAN_CALLBACK FdReadFile(LPCWSTR fileName, LPVOID buffer, DWORD bufferLength,
                                          LPDWORD readLength, LONGLONG offset,
                                          PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.read) return STATUS_NOT_IMPLEMENTED;

    Frame frame(f, info);

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    int rc = f->ops.read(ToPosixPath(fileName).c_str(), static_cast<char *>(buffer), bufferLength,
                         offset, &fi);
    if (rc < 0) return ToNtStatus(rc);

    *readLength = static_cast<DWORD>(rc);

    // A short read is how the end of the file is reported, and Windows is content with that. Zero
    // at or past the end is the same answer.
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK FdWriteFile(LPCWSTR fileName, LPCVOID buffer,
                                           DWORD numberOfBytesToWrite,
                                           LPDWORD numberOfBytesWritten, LONGLONG offset,
                                           PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.write) return STATUS_NOT_IMPLEMENTED;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    Frame frame(f, info);

    const std::string path = ToPosixPath(fileName);

    // An append is a write with no offset of its own: Windows says where by saying nothing, and
    // the end of the file has to be looked up before the write can be placed.
    if (info->WriteToEndOfFile)
    {
        struct fuse_stat st = {};
        if (!f->ops.getattr) return STATUS_NOT_IMPLEMENTED;

        int rc = f->ops.getattr(path.c_str(), &st, nullptr);
        if (rc != 0) return ToNtStatus(rc);

        offset = st.st_size;
    }

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    int rc = f->ops.write(path.c_str(), static_cast<const char *>(buffer), numberOfBytesToWrite,
                          offset, &fi);
    if (rc < 0) return ToNtStatus(rc);

    *numberOfBytesWritten = static_cast<DWORD>(rc);
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK FdFlushFileBuffers(LPCWSTR fileName, PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;

    // Nothing to flush is a success, not a failure: a filesystem that writes straight through has
    // already done what this asks. Returning an error here fails the CloseHandle that triggered it.
    if (!f->ops.fsync) return STATUS_SUCCESS;

    Frame frame(f, info);

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    return ToNtStatus(f->ops.fsync(ToPosixPath(fileName).c_str(), 0, &fi));
}

static void FillFileInformation(const struct fuse_stat *st, LPBY_HANDLE_FILE_INFORMATION out)
{
    out->dwFileAttributes = ToFileAttributes(st->st_mode);

    out->ftCreationTime   = ToFileTime(st->st_birthtim.tv_sec ? st->st_birthtim : st->st_ctim);
    out->ftLastAccessTime = ToFileTime(st->st_atim);
    out->ftLastWriteTime  = ToFileTime(st->st_mtim);

    out->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(st->st_size) >> 32);
    out->nFileSizeLow  = static_cast<DWORD>(static_cast<uint64_t>(st->st_size) & 0xFFFFFFFFu);

    out->nNumberOfLinks = st->st_nlink ? st->st_nlink : 1;

    // The inode number, which Windows calls a file index and uses to tell two paths apart when it
    // wants to know whether they are the same file.
    out->nFileIndexHigh = static_cast<DWORD>(st->st_ino >> 32);
    out->nFileIndexLow  = static_cast<DWORD>(st->st_ino & 0xFFFFFFFFu);

    out->dwVolumeSerialNumber = 0;
}

static NTSTATUS DOKAN_CALLBACK FdGetFileInformation(LPCWSTR fileName,
                                                    LPBY_HANDLE_FILE_INFORMATION buffer,
                                                    PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.getattr) return STATUS_NOT_IMPLEMENTED;

    Frame frame(f, info);

    struct fuse_stat st = {};

    int rc = f->ops.getattr(ToPosixPath(fileName).c_str(), &st, nullptr);
    if (rc != 0) return ToNtStatus(rc);

    FillFileInformation(&st, buffer);
    return STATUS_SUCCESS;
}

// What readdir fills in, on its way to Dokan's own callback. The directory path is carried along
// because an entry that arrives without a stat has to be looked up by full path, and readdir only
// ever hands over the bare name.
struct FindContext
{
    struct fuse   *f;
    PFillFindData  fill;
    PDOKAN_FILE_INFO info;
    std::string    directory;
    LPCWSTR        pattern;
};

static int FdFillDir(void *buf, const char *name, const struct fuse_stat *stbuf, fuse_off_t off,
                     enum fuse_fill_dir_flags flags)
{
    (void)off;
    (void)flags;

    FindContext *c = static_cast<FindContext *>(buf);

    WIN32_FIND_DATAW findData = {};

    const std::wstring wide = ToWide(name);
    if (wide.empty() || wide.size() >= MAX_PATH) return 0;   // skip, do not abort the listing

    wcscpy_s(findData.cFileName, MAX_PATH, wide.c_str());

    // Dokan asks FindFilesWithPattern for a subset and expects us to do the matching. Its own
    // matcher is used rather than a hand written one so that the wildcard rules are Windows'.
    if (c->pattern && !DokanIsNameInExpression(c->pattern, findData.cFileName, TRUE))
        return 0;

    struct fuse_stat own = {};

    if (!stbuf)
    {
        // readdir gave a name and nothing else, which is FUSE 3 without FUSE_FILL_DIR_PLUS. The
        // stat has to be fetched per entry, and for a network filesystem that is a round trip per
        // name - which is exactly what the PLUS flag exists to avoid.
        if (!c->f->ops.getattr) return 0;

        std::string full = c->directory;
        if (full.empty() || full.back() != '/') full += '/';
        full += name;

        if (c->f->ops.getattr(full.c_str(), &own, nullptr) != 0) return 0;
        stbuf = &own;
    }

    findData.dwFileAttributes = ToFileAttributes(stbuf->st_mode);

    findData.ftCreationTime   = ToFileTime(stbuf->st_birthtim.tv_sec ? stbuf->st_birthtim
                                                                     : stbuf->st_ctim);
    findData.ftLastAccessTime = ToFileTime(stbuf->st_atim);
    findData.ftLastWriteTime  = ToFileTime(stbuf->st_mtim);

    findData.nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(stbuf->st_size) >> 32);
    findData.nFileSizeLow  = static_cast<DWORD>(static_cast<uint64_t>(stbuf->st_size) & 0xFFFFFFFFu);

    // Non-zero from Dokan's filler means its buffer is full. FUSE reads that same non-zero as
    // "stop", so it passes straight through.
    return c->fill(&findData, c->info);
}

static NTSTATUS FindFilesImpl(LPCWSTR fileName, LPCWSTR pattern, PFillFindData fill,
                              PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.readdir) return STATUS_NOT_IMPLEMENTED;

    Frame frame(f, info);

    FindContext c;
    c.f         = f;
    c.fill      = fill;
    c.info      = info;
    c.directory = ToPosixPath(fileName);
    c.pattern   = (pattern && wcscmp(pattern, L"*") != 0) ? pattern : nullptr;

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    int rc = f->ops.readdir(c.directory.c_str(), &c, FdFillDir, 0, &fi,
                            static_cast<enum fuse_readdir_flags>(0));

    return ToNtStatus(rc);
}

static NTSTATUS DOKAN_CALLBACK FdFindFiles(LPCWSTR fileName, PFillFindData fill,
                                           PDOKAN_FILE_INFO info)
{
    return FindFilesImpl(fileName, nullptr, fill, info);
}

static NTSTATUS DOKAN_CALLBACK FdFindFilesWithPattern(LPCWSTR pathName, LPCWSTR searchPattern,
                                                      PFillFindData fill, PDOKAN_FILE_INFO info)
{
    return FindFilesImpl(pathName, searchPattern, fill, info);
}

// Windows deletes in two steps, and this is the first: it is asking whether the delete is going to
// be allowed, not asking for it. Saying yes here and doing nothing is correct - Cleanup above does
// the unlink once the last handle has gone. Doing it here instead deletes the file while handles
// are still open on it, which is not what any Windows application expects.
static NTSTATUS DOKAN_CALLBACK FdDeleteFile(LPCWSTR fileName, PDOKAN_FILE_INFO info)
{
    (void)fileName;

    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
    if (!f->ops.unlink) return STATUS_NOT_IMPLEMENTED;

    return STATUS_SUCCESS;
}

// Counts what is in the directory, because the answer Windows wants from the same question for a
// directory is whether it is empty.
static int CountEntries(void *buf, const char *name, const struct fuse_stat *stbuf, fuse_off_t off,
                        enum fuse_fill_dir_flags flags)
{
    (void)stbuf;
    (void)off;
    (void)flags;

    if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) return 0;

    *static_cast<int *>(buf) += 1;
    return 1;   // one is enough to know it is not empty; stop the enumeration here
}

static NTSTATUS DOKAN_CALLBACK FdDeleteDirectory(LPCWSTR fileName, PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;
    if (!f->ops.rmdir) return STATUS_NOT_IMPLEMENTED;

    Frame frame(f, info);

    if (f->ops.readdir)
    {
        int count = 0;

        struct fuse_file_info fi = {};
        f->ops.readdir(ToPosixPath(fileName).c_str(), &count, CountEntries, 0, &fi,
                       static_cast<enum fuse_readdir_flags>(0));

        if (count > 0) return STATUS_DIRECTORY_NOT_EMPTY;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK FdMoveFile(LPCWSTR fileName, LPCWSTR newFileName,
                                          BOOL replaceIfExisting, PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.rename) return STATUS_NOT_IMPLEMENTED;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    Frame frame(f, info);

    const std::string from = ToPosixPath(fileName);
    const std::string to   = ToPosixPath(newFileName);

    // POSIX rename always replaces. Windows asks for that explicitly, so a move that did not ask
    // has to be refused here rather than quietly taking the destination with it. FUSE 3's
    // RENAME_NOREPLACE would say the same thing in one call, but it is optional and a filesystem
    // that ignores the flag would silently do the wrong thing - the check is done here instead.
    if (!replaceIfExisting && f->ops.getattr)
    {
        struct fuse_stat st = {};
        if (f->ops.getattr(to.c_str(), &st, nullptr) == 0) return STATUS_OBJECT_NAME_COLLISION;
    }

    return ToNtStatus(f->ops.rename(from.c_str(), to.c_str(), 0));
}

static NTSTATUS DOKAN_CALLBACK FdSetEndOfFile(LPCWSTR fileName, LONGLONG byteOffset,
                                              PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.truncate) return STATUS_NOT_IMPLEMENTED;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    Frame frame(f, info);

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    return ToNtStatus(f->ops.truncate(ToPosixPath(fileName).c_str(), byteOffset, &fi));
}

// Windows reserves space ahead of writing it. POSIX has no way to say that without also saying how
// long the file is, and shortening the file to the reservation would throw away data the caller
// never asked to lose - so this only ever grows, and never shrinks.
static NTSTATUS DOKAN_CALLBACK FdSetAllocationSize(LPCWSTR fileName, LONGLONG allocSize,
                                                   PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.truncate || !f->ops.getattr) return STATUS_NOT_IMPLEMENTED;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    Frame frame(f, info);

    const std::string path = ToPosixPath(fileName);

    struct fuse_stat st = {};
    int rc = f->ops.getattr(path.c_str(), &st, nullptr);
    if (rc != 0) return ToNtStatus(rc);

    if (allocSize <= st.st_size) return STATUS_SUCCESS;

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    return ToNtStatus(f->ops.truncate(path.c_str(), allocSize, &fi));
}

static NTSTATUS DOKAN_CALLBACK FdSetFileAttributes(LPCWSTR fileName, DWORD fileAttributes,
                                                   PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    // Of everything in a Windows attribute word, only the read only bit has a POSIX equivalent.
    // Hidden, system, archive and the rest have nowhere to go, and failing the call because of
    // them would break every copy that sets them - so an unsupported attribute is accepted and
    // dropped, which is what a filesystem without chmod does with all of them.
    if (!f->ops.chmod || fileAttributes == 0) return STATUS_SUCCESS;

    Frame frame(f, info);

    const std::string path = ToPosixPath(fileName);

    struct fuse_stat st = {};
    if (!f->ops.getattr || f->ops.getattr(path.c_str(), &st, nullptr) != 0) return STATUS_SUCCESS;

    fuse_mode_t mode = st.st_mode;
    if (fileAttributes & FILE_ATTRIBUTE_READONLY) mode &= ~static_cast<fuse_mode_t>(0222);
    else                                          mode |= 0200;

    if (mode == st.st_mode) return STATUS_SUCCESS;

    return ToNtStatus(f->ops.chmod(path.c_str(), mode, nullptr));
}

static NTSTATUS DOKAN_CALLBACK FdSetFileTime(LPCWSTR fileName, CONST FILETIME *creationTime,
                                             CONST FILETIME *lastAccessTime,
                                             CONST FILETIME *lastWriteTime, PDOKAN_FILE_INFO info)
{
    (void)creationTime;   // POSIX has no way to set a birth time

    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;
    if (f->readOnly) return STATUS_MEDIA_WRITE_PROTECTED;

    // Accepted and dropped when there is nowhere to put it, for the same reason as the attributes
    // above: Explorer sets timestamps at the end of every copy and fails the whole copy if the
    // call is refused.
    if (!f->ops.utimens) return STATUS_SUCCESS;

    Frame frame(f, info);

    // UTIME_OMIT is how FUSE says "leave this one", and it is what a zero FILETIME means. Spelled
    // out rather than included, because MinGW's <sys/stat.h> does not define it.
    const int64_t UTIME_OMIT_NSEC = (1LL << 30) - 2LL;

    struct fuse_timespec ts[2] = {};

    if (IsTimeSet(lastAccessTime)) FromFileTime(lastAccessTime, &ts[0]);
    else                           ts[0].tv_nsec = static_cast<int32_t>(UTIME_OMIT_NSEC);

    if (IsTimeSet(lastWriteTime))  FromFileTime(lastWriteTime, &ts[1]);
    else                           ts[1].tv_nsec = static_cast<int32_t>(UTIME_OMIT_NSEC);

    struct fuse_file_info fi = {};
    fi.fh = info->Context;

    return ToNtStatus(f->ops.utimens(ToPosixPath(fileName).c_str(), ts, &fi));
}

static NTSTATUS DOKAN_CALLBACK FdGetDiskFreeSpace(PULONGLONG freeBytesAvailable,
                                                  PULONGLONG totalNumberOfBytes,
                                                  PULONGLONG totalNumberOfFreeBytes,
                                                  PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f || !f->ops.statfs) return STATUS_NOT_IMPLEMENTED;

    Frame frame(f, info);

    struct fuse_statvfs vfs = {};

    int rc = f->ops.statfs("/", &vfs);
    if (rc != 0) return ToNtStatus(rc);

    const uint64_t unit = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;

    if (totalNumberOfBytes)     *totalNumberOfBytes     = unit * vfs.f_blocks;
    if (totalNumberOfFreeBytes) *totalNumberOfFreeBytes = unit * vfs.f_bfree;
    if (freeBytesAvailable)     *freeBytesAvailable     = unit * vfs.f_bavail;

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK FdGetVolumeInformation(LPWSTR volumeNameBuffer, DWORD volumeNameSize,
                                                      LPDWORD volumeSerialNumber,
                                                      LPDWORD maximumComponentLength,
                                                      LPDWORD fileSystemFlags,
                                                      LPWSTR fileSystemNameBuffer,
                                                      DWORD fileSystemNameSize,
                                                      PDOKAN_FILE_INFO info)
{
    struct fuse *f = FuseOf(info);
    if (!f) return STATUS_INVALID_DEVICE_REQUEST;

    wcscpy_s(volumeNameBuffer, volumeNameSize, f->volumeName.c_str());
    wcscpy_s(fileSystemNameBuffer, fileSystemNameSize, f->fileSystemName.c_str());

    if (volumeSerialNumber)     *volumeSerialNumber     = 0x46440000;   // "FD"
    if (maximumComponentLength) *maximumComponentLength = 255;

    if (fileSystemFlags)
    {
        *fileSystemFlags = FILE_CASE_PRESERVED_NAMES | FILE_UNICODE_ON_DISK;

        // Not FILE_PERSISTENT_ACLS: nothing here implements GetFileSecurity, so claiming to keep
        // ACLs would have Windows ask for them and get nothing back.
        if (f->readOnly) *fileSystemFlags |= FILE_READ_ONLY_VOLUME;
    }

    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------------------------
// The FUSE 3 entry points.
// ---------------------------------------------------------------------------------------------

// -o name=value, the handful of them that mean anything on this platform. Everything else is
// accepted and ignored, the way libfuse ignores options meant for a different backend: subtype and
// auto_cache reach us from the shared mount code and have no counterpart in Dokan.
static void ApplyOption(struct fuse *f, const std::string &opt)
{
    auto valueOf = [&opt](const char *key) -> const char * {
        size_t n = std::strlen(key);
        if (opt.size() > n && opt.compare(0, n, key) == 0 && opt[n] == '=')
            return opt.c_str() + n + 1;
        return nullptr;
    };

    if (const char *v = valueOf("volname")) { f->volumeName     = ToWide(v); return; }
    if (const char *v = valueOf("fsname"))  { f->fileSystemName = ToWide(v); return; }

    // Reported through fuse_get_context() and nothing else - see the note on the fields. Accepted
    // so that a mount line written for WinFsp still parses.
    if (const char *v = valueOf("uid")) { f->uid = static_cast<fuse_uid_t>(std::strtol(v, nullptr, 10)); return; }
    if (const char *v = valueOf("gid")) { f->gid = static_cast<fuse_gid_t>(std::strtol(v, nullptr, 10)); return; }

    if (opt == "ro" || opt == "rdonly") { f->readOnly = true; return; }
}

extern "C" struct fuse *fuse_new(struct fuse_args *args, const struct fuse_operations *op,
                                 size_t op_size, void *private_data)
{
    if (!op) return nullptr;

    struct fuse *f = new (std::nothrow) struct fuse;
    if (!f) return nullptr;

    // op_size is what the filesystem was compiled against, which may be smaller than what this
    // layer knows about. Copy that much and leave the rest null, the way libfuse does.
    std::memcpy(&f->ops, op, op_size > sizeof(f->ops) ? sizeof(f->ops) : op_size);
    f->private_data = private_data;

    // -o takes a comma separated list, and each element is an option in its own right.
    for (int i = 0; args && i < args->argc; ++i)
    {
        if (!args->argv[i] || std::strcmp(args->argv[i], "-o") != 0) continue;
        if (i + 1 >= args->argc || !args->argv[i + 1]) continue;

        const std::string list = args->argv[++i];

        size_t start = 0;
        while (start <= list.size())
        {
            size_t comma = list.find(',', start);
            if (comma == std::string::npos) comma = list.size();

            if (comma > start) ApplyOption(f, list.substr(start, comma - start));
            start = comma + 1;
        }
    }

    LibraryAcquire();
    return f;
}

// FUSE 3 hands out a session before the mount exists, and this layer only uses it for the signal
// handlers below. There is nothing to point at yet, so the fuse itself stands in for one.
extern "C" struct fuse_session *fuse_get_session(struct fuse *f)
{
    return reinterpret_cast<struct fuse_session *>(f);
}

extern "C" int fuse_mount(struct fuse *f, const char *mountpoint)
{
    if (!f || !mountpoint || !*mountpoint) return -1;

    f->mountpoint  = mountpoint;
    f->mountpointW = ToWide(mountpoint);
    if (f->mountpointW.empty()) return -1;

    // A bare drive letter is what VirtDisk chooses, and Dokan wants it with the separator on.
    if (f->mountpointW.size() == 2 && f->mountpointW[1] == L':') f->mountpointW += L'\\';

    f->dokanOptions.Version       = DOKAN_VERSION;
    f->dokanOptions.MountPoint    = f->mountpointW.c_str();
    f->dokanOptions.GlobalContext = reinterpret_cast<ULONG64>(f);

    // The single request in flight the file system above is written for. FUSE's own fuse_loop is
    // single threaded too - fuse_loop_mt is the one that is not - so this is what the FUSE 3 call
    // it comes from already promises, and FUSEClient's blocking socket depends on it.
    f->dokanOptions.SingleThread = TRUE;

    if (f->readOnly) f->dokanOptions.Options |= DOKAN_OPTION_WRITE_PROTECT;

    // A plain drive letter, the same thing WinFsp gave us. DOKAN_OPTION_NETWORK would suit what
    // this actually is, and would keep Windows from writing a recycle bin and a System Volume
    // Information folder onto the peer's disk, but it needs Dokan's network provider installed and
    // a UNC name to go with it - without both the mount does not come up. Worth revisiting once
    // there is a machine with the provider on it to try it against.
    f->dokanOptions.UNCName = nullptr;

    f->dokanOps.ZwCreateFile          = FdCreateFile;
    f->dokanOps.Cleanup               = FdCleanup;
    f->dokanOps.CloseFile             = FdCloseFile;
    f->dokanOps.ReadFile              = FdReadFile;
    f->dokanOps.WriteFile             = FdWriteFile;
    f->dokanOps.FlushFileBuffers      = FdFlushFileBuffers;
    f->dokanOps.GetFileInformation    = FdGetFileInformation;
    f->dokanOps.FindFiles             = FdFindFiles;
    f->dokanOps.FindFilesWithPattern  = FdFindFilesWithPattern;
    f->dokanOps.SetFileAttributes     = FdSetFileAttributes;
    f->dokanOps.SetFileTime           = FdSetFileTime;
    f->dokanOps.DeleteFile            = FdDeleteFile;
    f->dokanOps.DeleteDirectory       = FdDeleteDirectory;
    f->dokanOps.MoveFile              = FdMoveFile;
    f->dokanOps.SetEndOfFile          = FdSetEndOfFile;
    f->dokanOps.SetAllocationSize     = FdSetAllocationSize;
    f->dokanOps.GetDiskFreeSpace      = FdGetDiskFreeSpace;
    f->dokanOps.GetVolumeInformation  = FdGetVolumeInformation;

    // Left null on purpose. Dokan takes a null callback as "not implemented" and falls back to its
    // own handling, which for locks and security descriptors is better than anything this layer
    // could invent from a filesystem that has no opinion on either.
    f->dokanOps.LockFile        = nullptr;
    f->dokanOps.UnlockFile      = nullptr;
    f->dokanOps.GetFileSecurity = nullptr;
    f->dokanOps.SetFileSecurity = nullptr;
    f->dokanOps.FindStreams     = nullptr;
    f->dokanOps.Mounted         = nullptr;
    f->dokanOps.Unmounted       = nullptr;

    // Nothing is mounted yet. Dokan takes the drive letter in fuse_loop, where the filesystem is
    // created - which is the same order libfuse's own fuse_mount and fuse_loop end up in.
    return 0;
}

extern "C" int fuse_loop(struct fuse *f)
{
    if (!f || f->mountpointW.empty()) return -1;

    {
        std::lock_guard<std::mutex> lock(f->mutex);
        if (f->exiting) return 0;
    }

    // Not DokanMain, which does the create and the wait in one blocking call and hands back no
    // handle in between. Split, there is a moment where the filesystem exists and is known here,
    // which is what lets fuse_exit below be reliable rather than a race: before this returns there
    // is nothing to unmount, and after it there always is.
    DOKAN_HANDLE instance = nullptr;

    int rc = DokanCreateFileSystem(&f->dokanOptions, &f->dokanOps, &instance);
    if (rc != DOKAN_SUCCESS) return rc;

    bool stopNow = false;
    {
        std::lock_guard<std::mutex> lock(f->mutex);
        f->instance = instance;

        // Asked to stop while the filesystem was coming up. It exists now, so it can be taken
        // straight back down; the wait below then returns at once.
        stopNow = f->exiting;
    }

    // Outside the lock, for the reason given on fuse_exit.
    if (stopNow) DokanRemoveMountPoint(f->mountpointW.c_str());

    DokanWaitForFileSystemClosed(instance, INFINITE);

    {
        std::lock_guard<std::mutex> lock(f->mutex);
        f->instance = nullptr;
    }

    DokanCloseHandle(instance);
    return 0;
}

// Ends the loop by taking the mount point away, which is the only thing that will: the loop is
// inside Dokan waiting on the filesystem, and it comes back when the filesystem is gone. Safe to
// call before the mount is up, safe to call twice, and safe to call from another thread - which is
// what VirtDisk::stop does, more than once, from the GUI thread.
extern "C" void fuse_exit(struct fuse *f)
{
    if (!f) return;

    bool mounted = false;
    {
        std::lock_guard<std::mutex> lock(f->mutex);

        f->exiting = true;
        mounted    = f->instance != nullptr;
    }

    // Never with the lock held. How long DokanRemoveMountPoint takes to come back is the driver's
    // business, and fuse_loop wants the same lock the moment its wait ends - holding it across a
    // call that is trying to make exactly that happen is how the two would sit on each other.
    // mountpointW has not changed since fuse_mount, so reading it here needs no lock of its own.
    if (mounted) DokanRemoveMountPoint(f->mountpointW.c_str());
}

extern "C" void fuse_unmount(struct fuse *f)
{
    fuse_exit(f);
}

extern "C" void fuse_destroy(struct fuse *f)
{
    if (!f) return;

    LibraryRelease();
    delete f;
}

// Deliberately nothing. libfuse installs handlers for SIGINT, SIGHUP and SIGTERM that exit one
// session, chosen by a file scope pointer that the last caller wins - already the wrong answer for
// a process holding a mount per peer, and doubly so for a GUI process with no console to send it a
// signal from. FileDonkey brings a mount down through VirtDisk::stop, which is fuse_exit above.
// Returning success keeps the FUSE 3 call sites unchanged.
extern "C" int fuse_set_signal_handlers(struct fuse_session *se)
{
    (void)se;
    return 0;
}

extern "C" void fuse_remove_signal_handlers(struct fuse_session *se)
{
    (void)se;
}

extern "C" struct fuse_context *fuse_get_context(void)
{
    return tls_context;
}

// The argument vector this layer reads in fuse_new is the caller's own, built by FUSE_ARGS_INIT
// from a plain array, and everything taken from it was copied. There is nothing allocated to give
// back. Kept because a FUSE 3 filesystem is right to call it.
extern "C" void fuse_opt_free_args(struct fuse_args *args)
{
    if (args) *args = { 0, nullptr, 0 };
}
