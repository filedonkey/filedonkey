#define __FUSE__
// #define QT_NO_DEBUG_OUTPUT

#if defined(__WIN32) && !defined(__FUSE__)

#include "virtdisk.h"
#include "dokanbackend.h"

#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <dokan/dokan.h>
#include <dokan/fileinfo.h>
#include <thread>
#include <sys/timeb.h>

BOOL g_DebugMode;
BOOL g_CaseSensitive;
BOOL g_HasSeSecurityPrivilege;
BOOL g_ImpersonateCallerUser;
static WCHAR gRootDirectory[128] = L"D:";
static WCHAR gVolumeName[MAX_PATH + 1] = L"MacBook Pro";
static std::thread thread;

#define DOKAN_MAX_PATH 32768

VirtDisk::VirtDisk(const Connection& conn) : conn(conn)
{
}

VirtDisk::~VirtDisk()
{
    DokanRemoveMountPoint(L"M:\\"); //this->mountPoint.toStdWString().c_str());
    if (thread.joinable())
    {
        thread.join();
    }
}

static void GetFilePath(PWCHAR filePath, ULONG numberOfElements,
                        LPCWSTR FileName) {
    wcsncpy_s(filePath, numberOfElements, gRootDirectory, wcslen(gRootDirectory));
    wcsncat_s(filePath, numberOfElements, FileName, wcslen(FileName));
}

static BOOL AddSeSecurityNamePrivilege() {
    HANDLE token = 0;
    wprintf(
        L"## Attempting to add SE_SECURITY_NAME privilege to process token ##\n");
    DWORD err;
    LUID luid;
    if (!LookupPrivilegeValue(0, SE_SECURITY_NAME, &luid)) {
        err = GetLastError();
        if (err != ERROR_SUCCESS) {
            wprintf(L"  failed: Unable to lookup privilege value. error = %u\n",
                     err);
            return FALSE;
        }
    }

    LUID_AND_ATTRIBUTES attr;
    attr.Attributes = SE_PRIVILEGE_ENABLED;
    attr.Luid = luid;

    TOKEN_PRIVILEGES priv;
    priv.PrivilegeCount = 1;
    priv.Privileges[0] = attr;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        err = GetLastError();
        if (err != ERROR_SUCCESS) {
            wprintf(L"  failed: Unable obtain process token. error = %u\n", err);
            return FALSE;
        }
    }

    TOKEN_PRIVILEGES oldPriv;
    DWORD retSize;
    AdjustTokenPrivileges(token, FALSE, &priv, sizeof(TOKEN_PRIVILEGES), &oldPriv,
                          &retSize);
    err = GetLastError();
    if (err != ERROR_SUCCESS) {
        wprintf(L"  failed: Unable to adjust token privileges: %u\n", err);
        CloseHandle(token);
        return FALSE;
    }

    BOOL privAlreadyPresent = FALSE;
    for (unsigned int i = 0; i < oldPriv.PrivilegeCount; i++) {
        if (oldPriv.Privileges[i].Luid.HighPart == luid.HighPart &&
            oldPriv.Privileges[i].Luid.LowPart == luid.LowPart) {
            privAlreadyPresent = TRUE;
            break;
        }
    }
    wprintf(privAlreadyPresent ? L"  success: privilege already present\n"
                                : L"  success: privilege added\n");
    if (token)
        CloseHandle(token);
    return TRUE;
}

#define MirrorCheckFlag(val, flag) if (val & flag) { qDebug() << "[MirrorCheckFlag] flag: " << #flag; }

static NTSTATUS DOKAN_CALLBACK MirrorCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
                 ACCESS_MASK DesiredAccess, ULONG FileAttributes,
                 ULONG ShareAccess, ULONG CreateDisposition,
                 ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo)
{
    NTSTATUS result = DokanBackend::FD_CreateFile(FileName, (HANDLE)SecurityContext, DesiredAccess, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, (HANDLE)DokanFileInfo);

    if (result != STATUS_SUCCESS) {
        return DokanNtStatusFromWin32(result);
    }

    return STATUS_SUCCESS;
}

#pragma warning(push)
#pragma warning(disable : 4305)

static void DOKAN_CALLBACK MirrorCloseFile(LPCWSTR FileName,
                                           PDOKAN_FILE_INFO DokanFileInfo)
{

    DokanBackend::FD_CloseFile(FileName, (HANDLE)DokanFileInfo);
}

static void DOKAN_CALLBACK MirrorCleanup(LPCWSTR FileName,
                                         PDOKAN_FILE_INFO DokanFileInfo)
{
    DokanBackend::FD_Cleanup(FileName, (HANDLE)DokanFileInfo);
}

static NTSTATUS DOKAN_CALLBACK MirrorReadFile(LPCWSTR FileName, LPVOID Buffer,
                                              DWORD BufferLength,
                                              LPDWORD ReadLength,
                                              LONGLONG Offset,
                                              PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle = (HANDLE)DokanFileInfo->Context;
    ULONG offset = (ULONG)Offset;
    BOOL opened = FALSE;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"ReadFile : %s\n", filePath);

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle, cleanuped?\n");
        handle = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            wprintf(L"\tCreateFile error : %d\n\n", error);
            return DokanNtStatusFromWin32(error);
        }
        opened = TRUE;
    }

    OVERLAPPED overlap;
    memset(&overlap, 0, sizeof(OVERLAPPED));
    overlap.Offset = Offset & 0xFFFFFFFF;
    overlap.OffsetHigh = (Offset >> 32) & 0xFFFFFFFF;
    if (!ReadFile(handle, Buffer, BufferLength, ReadLength, &overlap)) {
        DWORD error = GetLastError();
        wprintf(L"\tread error = %u, buffer length = %d, read length = %d\n\n",
                 error, BufferLength, *ReadLength);
        if (opened)
            CloseHandle(handle);
        return DokanNtStatusFromWin32(error);

    } else {
        wprintf(L"\tByte to read: %d, Byte read %d, offset %d\n\n", BufferLength,
                 *ReadLength, offset);
    }

    if (opened)
        CloseHandle(handle);

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorWriteFile(LPCWSTR FileName, LPCVOID Buffer,
                                               DWORD NumberOfBytesToWrite,
                                               LPDWORD NumberOfBytesWritten,
                                               LONGLONG Offset,
                                               PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle = (HANDLE)DokanFileInfo->Context;
    BOOL opened = FALSE;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"WriteFile : %s, offset %I64d, length %d\n", filePath, Offset,
             NumberOfBytesToWrite);

    // reopen the file
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle, cleanuped?\n");
        handle = CreateFile(filePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            wprintf(L"\tCreateFile error : %d\n\n", error);
            return DokanNtStatusFromWin32(error);
        }
        opened = TRUE;
    }

    UINT64 fileSize = 0;
    DWORD fileSizeLow = 0;
    DWORD fileSizeHigh = 0;
    fileSizeLow = GetFileSize(handle, &fileSizeHigh);
    if (fileSizeLow == INVALID_FILE_SIZE) {
        DWORD error = GetLastError();
        wprintf(L"\tcan not get a file size error = %d\n", error);
        if (opened)
            CloseHandle(handle);
        return DokanNtStatusFromWin32(error);
    }

    fileSize = ((UINT64)fileSizeHigh << 32) | fileSizeLow;

    OVERLAPPED overlap;
    memset(&overlap, 0, sizeof(OVERLAPPED));
    if (DokanFileInfo->WriteToEndOfFile) {
        overlap.Offset = 0xFFFFFFFF;
        overlap.OffsetHigh = 0xFFFFFFFF;
    } else {
        // Paging IO cannot write after allocate file size.
        if (DokanFileInfo->PagingIo) {
            if ((UINT64)Offset >= fileSize) {
                *NumberOfBytesWritten = 0;
                if (opened)
                    CloseHandle(handle);
                return STATUS_SUCCESS;
            }

            if (((UINT64)Offset + NumberOfBytesToWrite) > fileSize) {
                UINT64 bytes = fileSize - Offset;
                if (bytes >> 32) {
                    NumberOfBytesToWrite = (DWORD)(bytes & 0xFFFFFFFFUL);
                } else {
                    NumberOfBytesToWrite = (DWORD)bytes;
                }
            }
        }

        if ((UINT64)Offset > fileSize) {
            // In the mirror sample helperZeroFileData is not necessary. NTFS will
            // zero a hole.
            // But if user's file system is different from NTFS( or other Windows's
            // file systems ) then  users will have to zero the hole themselves.
        }

        overlap.Offset = Offset & 0xFFFFFFFF;
        overlap.OffsetHigh = (Offset >> 32) & 0xFFFFFFFF;
    }

    if (!WriteFile(handle, Buffer, NumberOfBytesToWrite, NumberOfBytesWritten,
                   &overlap)) {
        DWORD error = GetLastError();
        wprintf(L"\twrite error = %u, buffer length = %d, write length = %d\n",
                 error, NumberOfBytesToWrite, *NumberOfBytesWritten);
        if (opened)
            CloseHandle(handle);
        return DokanNtStatusFromWin32(error);

    } else {
        wprintf(L"\twrite %d, offset %I64d\n\n", *NumberOfBytesWritten, Offset);
    }

    // close the file when it is reopened
    if (opened)
        CloseHandle(handle);

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle = (HANDLE)DokanFileInfo->Context;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"FlushFileBuffers : %s\n", filePath);

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_SUCCESS;
    }

    if (FlushFileBuffers(handle)) {
        return STATUS_SUCCESS;
    } else {
        DWORD error = GetLastError();
        wprintf(L"\tflush error code = %d\n", error);
        return DokanNtStatusFromWin32(error);
    }
}

static NTSTATUS DOKAN_CALLBACK MirrorGetFileInformation(
    LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
    PDOKAN_FILE_INFO DokanFileInfo) {

    NTSTATUS result = DokanBackend::FD_GetFileInformation(FileName, (HANDLE)HandleFileInformation, (HANDLE)DokanFileInfo->Context);

    if (result != STATUS_SUCCESS) {
        return DokanNtStatusFromWin32(result);
    }

    return STATUS_SUCCESS;
}

static char *wchar_to_utf8(const wchar_t *src) {
    if (src == nullptr)
        return nullptr;

    int ln = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    auto res = static_cast<char *>(malloc(sizeof(char) * ln));
    WideCharToMultiByte(CP_UTF8, 0, src, -1, res, ln, nullptr, nullptr);
    return res;
}

static NTSTATUS DOKAN_CALLBACK MirrorFindFiles(LPCWSTR FileName,
                PFillFindData FillFindData, // function pointer
                PDOKAN_FILE_INFO DokanFileInfo)
{
    qDebug() << "[MirrorFindFiles] FileName: " << wchar_to_utf8(FileName);

    FindFilesResult *result = DokanBackend::FD_FindFiles(FileName);

    if (result->ntStatus != STATUS_SUCCESS)
    {
        qDebug() << "[MirrorFindFiles] error result->ntStatus" << result->ntStatus;
        return result->ntStatus;
    }

    qDebug() << "[MirrorFindFiles] result->ntStatus OK";
    qDebug() << "[MirrorFindFiles] result->count" << result->count;
    qDebug() << "[MirrorFindFiles] result->dataSize" << result->dataSize << "bytes";

    for (unsigned int i = 0; i < result->count; ++i)
    {
        auto findData = (WIN32_FIND_DATAW *)result->findData + i;
        qDebug() << "[MirrorFindFiles] findData.cFileName: " << wchar_to_utf8(findData->cFileName);
        FillFindData(findData, DokanFileInfo);
    }

    return result->ntStatus;
}

static NTSTATUS DOKAN_CALLBACK MirrorDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle = (HANDLE)DokanFileInfo->Context;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
    wprintf(L"DeleteFile %s - %d\n", filePath, DokanFileInfo->DeleteOnClose);

    DWORD dwAttrib = GetFileAttributes(filePath);

    if (dwAttrib != INVALID_FILE_ATTRIBUTES &&
        (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
        return STATUS_ACCESS_DENIED;

    if (handle && handle != INVALID_HANDLE_VALUE) {
        FILE_DISPOSITION_INFO fdi;
        fdi.DeleteFile = DokanFileInfo->DeleteOnClose;
        if (!SetFileInformationByHandle(handle, FileDispositionInfo, &fdi,
                                        sizeof(FILE_DISPOSITION_INFO)))
            return DokanNtStatusFromWin32(GetLastError());
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    // HANDLE	handle = (HANDLE)DokanFileInfo->Context;
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    size_t fileLen;

    ZeroMemory(filePath, sizeof(filePath));
    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"DeleteDirectory %s - %d\n", filePath,
             DokanFileInfo->DeleteOnClose);

    if (!DokanFileInfo->DeleteOnClose)
        //Dokan notify that the file is requested not to be deleted.
        return STATUS_SUCCESS;

    fileLen = wcslen(filePath);
    if (filePath[fileLen - 1] != L'\\') {
        filePath[fileLen++] = L'\\';
    }
    if (fileLen + 1 >= DOKAN_MAX_PATH)
        return STATUS_BUFFER_OVERFLOW;
    filePath[fileLen] = L'*';
    filePath[fileLen + 1] = L'\0';

    hFind = FindFirstFile(filePath, &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        wprintf(L"\tDeleteDirectory error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    do {
        if (wcscmp(findData.cFileName, L"..") != 0 &&
            wcscmp(findData.cFileName, L".") != 0) {
            FindClose(hFind);
            wprintf(L"\tDirectory is not empty: %s\n", findData.cFileName);
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
    } while (FindNextFile(hFind, &findData) != 0);

    DWORD error = GetLastError();

    FindClose(hFind);

    if (error != ERROR_NO_MORE_FILES) {
        wprintf(L"\tDeleteDirectory error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorMoveFile(LPCWSTR FileName, // existing file name
               LPCWSTR NewFileName, BOOL ReplaceIfExisting,
               PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    WCHAR newFilePath[DOKAN_MAX_PATH];
    HANDLE handle;
    DWORD bufferSize;
    BOOL result;
    size_t newFilePathLen;

    PFILE_RENAME_INFO renameInfo = NULL;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);
    if (wcslen(NewFileName) && NewFileName[0] != ':') {
        GetFilePath(newFilePath, DOKAN_MAX_PATH, NewFileName);
    } else {
        // For a stream rename, FileRenameInfo expect the FileName param without the filename
        // like :<stream name>:<stream type>
        wcsncpy_s(newFilePath, DOKAN_MAX_PATH, NewFileName, wcslen(NewFileName));
    }

    wprintf(L"MoveFile %s -> %s\n\n", filePath, newFilePath);
    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    newFilePathLen = wcslen(newFilePath);

    // the PFILE_RENAME_INFO struct has space for one WCHAR for the name at
    // the end, so that
    // accounts for the null terminator

    bufferSize = (DWORD)(sizeof(FILE_RENAME_INFO) +
                          newFilePathLen * sizeof(newFilePath[0]));

    renameInfo = (PFILE_RENAME_INFO)malloc(bufferSize);
    if (!renameInfo) {
        return STATUS_BUFFER_OVERFLOW;
    }
    ZeroMemory(renameInfo, bufferSize);

    renameInfo->ReplaceIfExists =
        ReplaceIfExisting
            ? TRUE
            : FALSE; // some warning about converting BOOL to BOOLEAN
    renameInfo->RootDirectory = NULL; // hope it is never needed, shouldn't be
    renameInfo->FileNameLength =
        (DWORD)newFilePathLen *
        sizeof(newFilePath[0]); // they want length in bytes

    wcscpy_s(renameInfo->FileName, newFilePathLen + 1, newFilePath);

    result = SetFileInformationByHandle(handle, FileRenameInfo, renameInfo,
                                        bufferSize);

    free(renameInfo);

    if (result) {
        return STATUS_SUCCESS;
    } else {
        DWORD error = GetLastError();
        wprintf(L"\tMoveFile error = %u\n", error);
        return DokanNtStatusFromWin32(error);
    }
}

static NTSTATUS DOKAN_CALLBACK MirrorLockFile(LPCWSTR FileName,
                                              LONGLONG ByteOffset,
                                              LONGLONG Length,
                                              PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle;
    LARGE_INTEGER offset;
    LARGE_INTEGER length;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"LockFile %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (!LockFile(handle, offset.LowPart, offset.HighPart, length.LowPart,
                  length.HighPart)) {
        DWORD error = GetLastError();
        wprintf(L"\terror code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    wprintf(L"\tsuccess\n\n");
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetEndOfFile(
    LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle;
    LARGE_INTEGER offset;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"SetEndOfFile %s, %I64d\n", filePath, ByteOffset);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    offset.QuadPart = ByteOffset;
    if (!SetFilePointerEx(handle, offset, NULL, FILE_BEGIN)) {
        DWORD error = GetLastError();
        wprintf(L"\tSetFilePointer error: %d, offset = %I64d\n\n", error,
                 ByteOffset);
        return DokanNtStatusFromWin32(error);
    }

    if (!SetEndOfFile(handle)) {
        DWORD error = GetLastError();
        wprintf(L"\tSetEndOfFile error code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetAllocationSize(
    LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle;
    LARGE_INTEGER fileSize;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"SetAllocationSize %s, %I64d\n", filePath, AllocSize);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    if (GetFileSizeEx(handle, &fileSize)) {
        if (AllocSize < fileSize.QuadPart) {
            fileSize.QuadPart = AllocSize;
            if (!SetFilePointerEx(handle, fileSize, NULL, FILE_BEGIN)) {
                DWORD error = GetLastError();
                wprintf(L"\tSetAllocationSize: SetFilePointer eror: %d, "
                         L"offset = %I64d\n\n",
                         error, AllocSize);
                return DokanNtStatusFromWin32(error);
            }
            if (!SetEndOfFile(handle)) {
                DWORD error = GetLastError();
                wprintf(L"\tSetEndOfFile error code = %d\n\n", error);
                return DokanNtStatusFromWin32(error);
            }
        }
    } else {
        DWORD error = GetLastError();
        wprintf(L"\terror code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileAttributes(
    LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    WCHAR filePath[DOKAN_MAX_PATH];

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"SetFileAttributes %s 0x%x\n", filePath, FileAttributes);

    if (FileAttributes != 0) {
        if (!SetFileAttributes(filePath, FileAttributes)) {
            DWORD error = GetLastError();
            wprintf(L"\terror code = %d\n\n", error);
            return DokanNtStatusFromWin32(error);
        }
    } else {
        // case FileAttributes == 0 :
        // MS-FSCC 2.6 File Attributes : There is no file attribute with the value 0x00000000
        // because a value of 0x00000000 in the FileAttributes field means that the file attributes for this file MUST NOT be changed when setting basic information for the file
        wprintf(L"Set 0 to FileAttributes means MUST NOT be changed. Didn't call "
                 L"SetFileAttributes function. \n");
    }

    wprintf(L"\n");
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileTime(LPCWSTR FileName, CONST FILETIME *CreationTime,
                  CONST FILETIME *LastAccessTime, CONST FILETIME *LastWriteTime,
                  PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"SetFileTime %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    if (!SetFileTime(handle, CreationTime, LastAccessTime, LastWriteTime)) {
        DWORD error = GetLastError();
        wprintf(L"\terror code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    wprintf(L"\n");
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
                 PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle;
    LARGE_INTEGER length;
    LARGE_INTEGER offset;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"UnlockFile %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    length.QuadPart = Length;
    offset.QuadPart = ByteOffset;

    if (!UnlockFile(handle, offset.LowPart, offset.HighPart, length.LowPart,
                    length.HighPart)) {
        DWORD error = GetLastError();
        wprintf(L"\terror code = %d\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    wprintf(L"\tsuccess\n\n");
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorGetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength,
    PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo) {
    WCHAR filePath[DOKAN_MAX_PATH];
    BOOLEAN requestingSaclInfo;

    UNREFERENCED_PARAMETER(DokanFileInfo);

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"GetFileSecurity %s\n", filePath);

    MirrorCheckFlag(*SecurityInformation, FILE_SHARE_READ);
    MirrorCheckFlag(*SecurityInformation, OWNER_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, GROUP_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, DACL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, SACL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, LABEL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, ATTRIBUTE_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, SCOPE_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation,
                    PROCESS_TRUST_LABEL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, BACKUP_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, PROTECTED_DACL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, PROTECTED_SACL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, UNPROTECTED_DACL_SECURITY_INFORMATION);
    MirrorCheckFlag(*SecurityInformation, UNPROTECTED_SACL_SECURITY_INFORMATION);

    requestingSaclInfo = ((*SecurityInformation & SACL_SECURITY_INFORMATION) ||
                          (*SecurityInformation & BACKUP_SECURITY_INFORMATION));

    if (!g_HasSeSecurityPrivilege) {
        *SecurityInformation &= ~SACL_SECURITY_INFORMATION;
        *SecurityInformation &= ~BACKUP_SECURITY_INFORMATION;
    }

    wprintf(L"  Opening new handle with READ_CONTROL access\n");
    HANDLE handle = CreateFile(
        filePath,
        READ_CONTROL | ((requestingSaclInfo && g_HasSeSecurityPrivilege)
                            ? ACCESS_SYSTEM_SECURITY
                            : 0),
        FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
        NULL, // security attribute
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, // |FILE_FLAG_NO_BUFFERING,
        NULL);

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        int error = GetLastError();
        return DokanNtStatusFromWin32(error);
    }

    if (!GetUserObjectSecurity(handle, SecurityInformation, SecurityDescriptor,
                               BufferLength, LengthNeeded)) {
        int error = GetLastError();
        if (error == ERROR_INSUFFICIENT_BUFFER) {
            wprintf(L"  GetUserObjectSecurity error: ERROR_INSUFFICIENT_BUFFER\n");
            CloseHandle(handle);
            return STATUS_BUFFER_OVERFLOW;
        } else {
            wprintf(L"  GetUserObjectSecurity error: %d\n", error);
            CloseHandle(handle);
            return DokanNtStatusFromWin32(error);
        }
    }

    // Ensure the Security Descriptor Length is set
    DWORD securityDescriptorLength =
        GetSecurityDescriptorLength(SecurityDescriptor);
    wprintf(L"  GetUserObjectSecurity return true,  *LengthNeeded = "
             L"securityDescriptorLength \n");
    *LengthNeeded = securityDescriptorLength;

    CloseHandle(handle);

    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileSecurity(
    LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength,
    PDOKAN_FILE_INFO DokanFileInfo) {
    HANDLE handle;
    WCHAR filePath[DOKAN_MAX_PATH];

    UNREFERENCED_PARAMETER(SecurityDescriptorLength);

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"SetFileSecurity %s\n", filePath);

    handle = (HANDLE)DokanFileInfo->Context;
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle\n\n");
        return STATUS_INVALID_HANDLE;
    }

    if (!SetUserObjectSecurity(handle, SecurityInformation, SecurityDescriptor)) {
        int error = GetLastError();
        wprintf(L"  SetUserObjectSecurity error: %d\n", error);
        return DokanNtStatusFromWin32(error);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorDokanGetDiskFreeSpace(
    PULONGLONG FreeBytesAvailable,
    PULONGLONG TotalNumberOfBytes,
    PULONGLONG TotalNumberOfFreeBytes,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    // clock_t start = clock();
    timeb tstart;
    timeb tend;
    ftime(&tstart);

    const QString OPERATION_NAME = "getDiskFreeSpace";
    Connection *conn = (Connection*)DokanFileInfo->DokanOptions->GlobalContext;

    if (!conn)
    {
        qDebug() << "[MirrorDokanGetDiskFreeSpace] global context is empty";
        return STATUS_DEVICE_OFF_LINE;
    }

    QTcpSocket *socket = conn->socket;

    if (!socket)
    {
        qDebug() << "[MirrorDokanGetDiskFreeSpace] connection is invalid";
        return STATUS_DEVICE_OFF_LINE;
    }

    qDebug() << "[MirrorDokanGetDiskFreeSpace] machineId: " << conn->machineId;

    QJsonObject request;
    request["messageType"] = "request";
    request["operationName"] = OPERATION_NAME;
    QByteArray data = QJsonDocument(request).toJson(QJsonDocument::Compact);
    socket->write(data);

    qDebug() << "[MirrorDokanGetDiskFreeSpace] After write";

    socket->waitForReadyRead();

    qDebug() << "[MirrorDokanGetDiskFreeSpace] Before readAll";

    QByteArray buff = socket->readAll();
    QJsonDocument response = QJsonDocument::fromJson(buff);
    QString messageType = response["messageType"].toString();
    QString operationName = response["operationName"].toString();

    qDebug() << "[MirrorDokanGetDiskFreeSpace] response messageType: " << messageType;
    qDebug() << "[MirrorDokanGetDiskFreeSpace] response operationName: " << operationName;

    if (messageType != "response")
    {
        qDebug() << "[MirrorDokanGetDiskFreeSpace] response message type is invalid";
        return STATUS_DEVICE_OFF_LINE; // TODO: return correct status.
    }
    if (operationName != OPERATION_NAME)
    {
        qDebug() << "[MirrorDokanGetDiskFreeSpace] response operation is invalid";
        return STATUS_DEVICE_OFF_LINE; // TODO: return correct status.
    }

    // clock_t end = clock();
    // double diff = static_cast<double>((end - start)) / static_cast<double>((CLOCKS_PER_SEC)) * 1000;
    ftime(&tend);
    double diff = (tend.time * 1000 + tend.millitm) - (tstart.time * 1000 + tstart.millitm);

    qDebug() << "[MirrorDokanGetDiskFreeSpace] operation took:" << diff << "ms";

    qDebug() << "[MirrorDokanGetDiskFreeSpace] response freeBytesAvailable: " << response["freeBytesAvailable"].toInteger();
    qDebug() << "[MirrorDokanGetDiskFreeSpace] response totalNumberOfBytes: " << response["totalNumberOfBytes"].toInteger();
    qDebug() << "[MirrorDokanGetDiskFreeSpace] response totalNumberOfFreeBytes: " << response["totalNumberOfFreeBytes"].toInteger();

    *FreeBytesAvailable = response["freeBytesAvailable"].toInteger();         //(ULONGLONG)(18450636288);
    *TotalNumberOfBytes = response["totalNumberOfBytes"].toInteger();         //(ULONGLONG)(122747575603);
    *TotalNumberOfFreeBytes = response["totalNumberOfFreeBytes"].toInteger(); //(ULONGLONG)(18450636288);

    return STATUS_SUCCESS;
}

// static NTSTATUS DOKAN_CALLBACK MirrorDokanGetDiskFreeSpace(
//     PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
//     PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo)
// {
//     // clock_t start = clock();
//     timeb tstart;
//     timeb tend;
//     ftime(&tstart);

//     UNREFERENCED_PARAMETER(DokanFileInfo);

//     NTSTATUS result = FileSystem::FD_GetDiskFreeSpace(FreeBytesAvailable, TotalNumberOfBytes, TotalNumberOfFreeBytes);

//     if (result != STATUS_SUCCESS) {
//         return DokanNtStatusFromWin32(result);
//     }

//     // clock_t end = clock();
//     // double diff = static_cast<double>((end - start)) / static_cast<double>((CLOCKS_PER_SEC)) * 1000;
//     ftime(&tend);
//     double diff = (tend.time * 1000 + tend.millitm) - (tstart.time * 1000 + tstart.millitm);

//     qDebug() << "[MirrorDokanGetDiskFreeSpace] operation took:" << diff << "ms";

//     qDebug() << "[MirrorDokanGetDiskFreeSpace] FreeBytesAvailable: " << *FreeBytesAvailable;
//     qDebug() << "[MirrorDokanGetDiskFreeSpace] TotalNumberOfBytes: " << *TotalNumberOfBytes;
//     qDebug() << "[MirrorDokanGetDiskFreeSpace] TotalNumberOfFreeBytes: " << *TotalNumberOfFreeBytes;

//     return STATUS_SUCCESS;
// }

static NTSTATUS DOKAN_CALLBACK MirrorGetVolumeInformation(
    LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
    LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags,
    LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
    PDOKAN_FILE_INFO DokanFileInfo)
{
    UNREFERENCED_PARAMETER(DokanFileInfo);

    qDebug() << "[MirrorGetVolumeInformation] VolumeNameBuffer: " << VolumeNameBuffer;

    NTSTATUS result = DokanBackend::FD_GetVolumeInformation(VolumeNameBuffer, VolumeNameSize, VolumeSerialNumber, MaximumComponentLength, FileSystemFlags, FileSystemNameBuffer, FileSystemNameSize);

    if (result != STATUS_SUCCESS) {
        return DokanNtStatusFromWin32(result);
    }

    return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK MirrorFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData,
                  PVOID FindStreamContext,
                  PDOKAN_FILE_INFO DokanFileInfo)
{
    UNREFERENCED_PARAMETER(DokanFileInfo);

    qDebug() << "[MirrorFindStreams] FileName: " << FileName;

    return DokanBackend::FD_FindStreams(FileName, (HANDLE)FillFindStreamData, FindStreamContext);
}

static NTSTATUS DOKAN_CALLBACK MirrorMounted(LPCWSTR MountPoint,
                                             PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    wprintf(L"Mounted as %s\n", MountPoint);
    return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorUnmounted(PDOKAN_FILE_INFO DokanFileInfo) {
    UNREFERENCED_PARAMETER(DokanFileInfo);

    wprintf(L"Unmounted\n");
    return STATUS_SUCCESS;
}

static void Start(DOKAN_OPTIONS options, DOKAN_OPERATIONS operations)
{
    // Connection *newConn = (Connection*)options.GlobalContext;
    // newConn->socket = new QTcpSocket();
    // qDebug() << "[establishConnection] try to connect";
    // newConn->socket->connectToHost(QHostAddress(newConn->machineAddress), newConn->machinePort);
    // if (!newConn->socket->waitForConnected())
    // {
    //     qDebug()
    //         << "[establishConnection] socket connection error: "
    //         << newConn->socket->errorString();
    //     return;
    // }

    // qDebug() << "[establishConnection] socket connected";

    DokanInit();
    int status = DokanMain(&options, &operations);
    DokanShutdown();

    switch (status) {
    case DOKAN_SUCCESS:
        fprintf(stderr, "Success\n");
        break;
    case DOKAN_ERROR:
        fprintf(stderr, "Error\n");
        break;
    case DOKAN_DRIVE_LETTER_ERROR:
        fprintf(stderr, "Bad Drive letter\n");
        break;
    case DOKAN_DRIVER_INSTALL_ERROR:
        fprintf(stderr, "Can't install driver\n");
        break;
    case DOKAN_START_ERROR:
        fprintf(stderr, "Driver something wrong\n");
        break;
    case DOKAN_MOUNT_ERROR:
        fprintf(stderr, "Can't assign a drive letter\n");
        break;
    case DOKAN_MOUNT_POINT_ERROR:
        fprintf(stderr, "Mount point error\n");
        break;
    case DOKAN_VERSION_ERROR:
        fprintf(stderr, "Version error\n");
        break;
    default:
        fprintf(stderr, "Unknown error: %d\n", status);
        break;
    }
}

void VirtDisk::mount(const QString &mountPoint)
{
    this->mountPoint = mountPoint;

    DOKAN_OPERATIONS operations;
    DOKAN_OPTIONS options;

    ZeroMemory(&options, sizeof(DOKAN_OPTIONS));
    ZeroMemory(&operations, sizeof(DOKAN_OPERATIONS));

    options.Version = DOKAN_VERSION;
    options.MountPoint = L"M:\\"; //this->mountPoint.toStdWString().c_str();
    options.Options |= DOKAN_OPTION_ALT_STREAM;
    options.Options |= DOKAN_OPTION_REMOVABLE;
    options.GlobalContext = (ULONG64)&conn;

    operations.ZwCreateFile = MirrorCreateFile; // +
    operations.Cleanup = MirrorCleanup; // +
    operations.CloseFile = MirrorCloseFile; // +
    operations.ReadFile = MirrorReadFile;
    operations.WriteFile = MirrorWriteFile;
    operations.FlushFileBuffers = MirrorFlushFileBuffers;
    operations.GetFileInformation = MirrorGetFileInformation; // +
    operations.FindFiles = MirrorFindFiles;
    operations.FindFilesWithPattern = NULL;
    operations.SetFileAttributes = MirrorSetFileAttributes;
    operations.SetFileTime = MirrorSetFileTime;
    operations.DeleteFile = MirrorDeleteFile;
    operations.DeleteDirectory = MirrorDeleteDirectory;
    operations.MoveFile = MirrorMoveFile;
    operations.SetEndOfFile = MirrorSetEndOfFile;
    operations.SetAllocationSize = MirrorSetAllocationSize;
    operations.LockFile = MirrorLockFile;
    operations.UnlockFile = MirrorUnlockFile;
    operations.GetFileSecurity = MirrorGetFileSecurity;
    operations.SetFileSecurity = MirrorSetFileSecurity;
    operations.GetDiskFreeSpace = MirrorDokanGetDiskFreeSpace; // +
    operations.GetVolumeInformation = MirrorGetVolumeInformation; // +
    operations.Unmounted = MirrorUnmounted;
    operations.FindStreams = MirrorFindStreams; // +
    operations.Mounted = MirrorMounted;

    thread = std::thread(Start, options, operations);
}

#endif
