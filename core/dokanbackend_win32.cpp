#define __FUSE__
#if defined(_WIN32) && !defined(__FUSE__)

#include "dokanbackend.h"

#include <dokan/dokan.h>
// #include <dokan/fileinfo.h>
#include <fileapi.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <wchar.h>

#define CASE_SENSITIVE true
#define IMPERSONATE_CALLER_USER false
#define DOKAN_MAX_PATH 32768

static WCHAR gRootDirectory[128] = L"D:";
static WCHAR gVolumeName[MAX_PATH + 1] = L"MacBook Pro";

static void GetFilePath(PWCHAR filePath, ULONG numberOfElements,
                        LPCWSTR FileName) {
    wcsncpy_s(filePath, numberOfElements, gRootDirectory, wcslen(gRootDirectory));
    wcsncat_s(filePath, numberOfElements, FileName, wcslen(FileName));
}

long DokanBackend::FD_GetDiskFreeSpace(PULONGLONG FreeBytesAvailable,
                                     PULONGLONG TotalNumberOfBytes,
                                     PULONGLONG TotalNumberOfFreeBytes)
{
    WCHAR DriveLetter[3] = {'C', ':', 0};
    PWCHAR RootPathName;

    if (gRootDirectory[0] == L'\\') { // UNC as Root
        RootPathName = gRootDirectory;
    } else {
        DriveLetter[0] = gRootDirectory[0];
        RootPathName = DriveLetter;
    }

    if (!GetDiskFreeSpaceExW(RootPathName,
                             (PULARGE_INTEGER)FreeBytesAvailable,
                             (PULARGE_INTEGER)TotalNumberOfBytes,
                             (PULARGE_INTEGER)TotalNumberOfFreeBytes)) {
        DWORD error = GetLastError();
        wprintf(L"GetDiskFreeSpaceEx failed for path %ws", RootPathName);
        return error;
    }

    return 0;
}

long DokanBackend::FD_GetVolumeInformation(LPWSTR VolumeNameBuffer,
                                         DWORD VolumeNameSize,
                                         LPDWORD VolumeSerialNumber,
                                         LPDWORD MaximumComponentLength,
                                         LPDWORD FileSystemFlags,
                                         LPWSTR FileSystemNameBuffer,
                                         DWORD FileSystemNameSize)
{
    WCHAR volumeRoot[4];
    DWORD fsFlags = 0;

    wcscpy_s(VolumeNameBuffer, VolumeNameSize, gVolumeName);

    if (VolumeSerialNumber)
        *VolumeSerialNumber = 0x19831116;
    if (MaximumComponentLength)
        *MaximumComponentLength = 255;
    if (FileSystemFlags) {
        *FileSystemFlags = FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK |
                           FILE_PERSISTENT_ACLS | FILE_NAMED_STREAMS;
        if (CASE_SENSITIVE)
            *FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES;
    }

    volumeRoot[0] = gRootDirectory[0];
    volumeRoot[1] = ':';
    volumeRoot[2] = '\\';
    volumeRoot[3] = '\0';

    if (GetVolumeInformation(volumeRoot, NULL, 0, NULL, MaximumComponentLength,
                             &fsFlags, FileSystemNameBuffer,
                             FileSystemNameSize)) {

        if (FileSystemFlags)
            *FileSystemFlags &= fsFlags;

        if (MaximumComponentLength) {
            wprintf(L"GetVolumeInformation: max component length %u\n",
                    *MaximumComponentLength);
        }
        if (FileSystemNameBuffer) {
            wprintf(L"GetVolumeInformation: file system name %s\n",
                    FileSystemNameBuffer);
        }
        if (FileSystemFlags) {
            wprintf(L"GetVolumeInformation: got file system flags 0x%08x,"
                    L" returning 0x%08x\n",
                    fsFlags, *FileSystemFlags);
        }
    } else {

        wprintf(L"GetVolumeInformation: unable to query underlying fs,"
                L" using defaults.  Last error = %u\n",
                GetLastError());

        // File system name could be anything up to 10 characters.
        // But Windows check few feature availability based on file system name.
        // For this, it is recommended to set NTFS or FAT here.
        wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");
    }

    return 0;
}

long DokanBackend::FD_GetFileInformation(LPCWSTR FileName,
                                       HANDLE HandleFileInformationU,
                                       HANDLE handle)
{
    LPBY_HANDLE_FILE_INFORMATION HandleFileInformation = (LPBY_HANDLE_FILE_INFORMATION)HandleFileInformationU;
    WCHAR filePath[DOKAN_MAX_PATH];
    BOOL opened = FALSE;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"GetFileInfo : %s\n", filePath);

    if (!handle || handle == INVALID_HANDLE_VALUE) {
        wprintf(L"\tinvalid handle, cleanuped?\n");
        handle = CreateFile(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            wprintf(L"\tCreateFile error : %d\n\n", error);
            return error;
        }
        opened = TRUE;
    }

    if (!GetFileInformationByHandle(handle, HandleFileInformation)) {
        wprintf(L"\terror code = %d\n", GetLastError());

        // FileName is a root directory
        // in this case, FindFirstFile can't get directory information
        if (wcslen(FileName) == 1) {
            wprintf(L"  root dir\n");
            HandleFileInformation->dwFileAttributes = GetFileAttributes(filePath);

        } else {
            WIN32_FIND_DATAW find;
            ZeroMemory(&find, sizeof(WIN32_FIND_DATAW));
            HANDLE findHandle = FindFirstFile(filePath, &find);
            if (findHandle == INVALID_HANDLE_VALUE) {
                DWORD error = GetLastError();
                wprintf(L"\tFindFirstFile error code = %d\n\n", error);
                if (opened)
                    CloseHandle(handle);
                return error;
            }
            HandleFileInformation->dwFileAttributes = find.dwFileAttributes;
            HandleFileInformation->ftCreationTime = find.ftCreationTime;
            HandleFileInformation->ftLastAccessTime = find.ftLastAccessTime;
            HandleFileInformation->ftLastWriteTime = find.ftLastWriteTime;
            HandleFileInformation->nFileSizeHigh = find.nFileSizeHigh;
            HandleFileInformation->nFileSizeLow = find.nFileSizeLow;
            wprintf(L"\tFindFiles OK, file size = %d\n", find.nFileSizeLow);
            FindClose(findHandle);
        }
    } else {
        wprintf(L"\tGetFileInformationByHandle success, file size = %d\n",
                HandleFileInformation->nFileSizeLow);
    }

    wprintf(L"FILE ATTRIBUTE  = %d\n", HandleFileInformation->dwFileAttributes);

    if (opened)
        CloseHandle(handle);

    return 0;
}

void DokanBackend::FD_CloseFile(LPCWSTR FileName,
                              HANDLE FileInfo)
{
    PDOKAN_FILE_INFO DokanFileInfo = (PDOKAN_FILE_INFO)FileInfo;
    WCHAR filePath[DOKAN_MAX_PATH];
    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    if (DokanFileInfo->Context) {
        wprintf(L"CloseFile: %s\n", filePath);
        wprintf(L"\terror : not cleanuped file\n\n");
        CloseHandle((HANDLE)DokanFileInfo->Context);
        DokanFileInfo->Context = 0;
    } else {
        wprintf(L"Close: %s\n\n", filePath);
    }
}

void DokanBackend::FD_Cleanup(LPCWSTR FileName,
                            HANDLE FileInfo)
{
    PDOKAN_FILE_INFO DokanFileInfo = (PDOKAN_FILE_INFO)FileInfo;
    WCHAR filePath[DOKAN_MAX_PATH];
    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    if (DokanFileInfo->Context) {
        wprintf(L"Cleanup: %s\n\n", filePath);
        CloseHandle((HANDLE)(DokanFileInfo->Context));
        DokanFileInfo->Context = 0;
    } else {
        wprintf(L"Cleanup: %s\n\tinvalid handle\n\n", filePath);
    }

    if (DokanFileInfo->DeleteOnClose) {
        // Should already be deleted by CloseHandle
        // if open with FILE_FLAG_DELETE_ON_CLOSE
        wprintf(L"\tDeleteOnClose\n");
        if (DokanFileInfo->IsDirectory) {
            wprintf(L"  DeleteDirectory ");
            if (!RemoveDirectory(filePath)) {
                wprintf(L"error code = %d\n\n", GetLastError());
            } else {
                wprintf(L"success\n\n");
            }
        } else {
            wprintf(L"  DeleteFile ");
            if (DeleteFile(filePath) == 0) {
                wprintf(L" error code = %d\n\n", GetLastError());
            } else {
                wprintf(L"success\n\n");
            }
        }
    }
}

void PrintUserName(PDOKAN_FILE_INFO DokanFileInfo)
{
    HANDLE handle;
    UCHAR buffer[1024];
    DWORD returnLength;
    WCHAR accountName[256];
    WCHAR domainName[256];
    DWORD accountLength = sizeof(accountName) / sizeof(WCHAR);
    DWORD domainLength = sizeof(domainName) / sizeof(WCHAR);
    PTOKEN_USER tokenUser;
    SID_NAME_USE snu;

    // if (!g_DebugMode)
    //     return;

    handle = DokanOpenRequestorToken(DokanFileInfo);
    if (handle == INVALID_HANDLE_VALUE) {
        wprintf(L"  DokanOpenRequestorToken failed\n");
        return;
    }

    if (!GetTokenInformation(handle, TokenUser, buffer, sizeof(buffer),
                             &returnLength)) {
        wprintf(L"  GetTokenInformaiton failed: %d\n", GetLastError());
        CloseHandle(handle);
        return;
    }

    CloseHandle(handle);

    tokenUser = (PTOKEN_USER)buffer;
    if (!LookupAccountSid(NULL, tokenUser->User.Sid, accountName, &accountLength,
                          domainName, &domainLength, &snu)) {
        wprintf(L"  LookupAccountSid failed: %d\n", GetLastError());
        return;
    }

    wprintf(L"  AccountName: %s, DomainName: %s\n", accountName, domainName);
}

#define MirrorCheckFlag(val, flag) if (val & flag) { qDebug() << "[MirrorCheckFlag] flag: " << #flag; }

long DokanBackend::FD_CreateFile(LPCWSTR FileName,
                               HANDLE SecurityContextU,
                               DWORD DesiredAccess,
                               ULONG FileAttributes,
                               ULONG ShareAccess,
                               ULONG CreateDisposition,
                               ULONG CreateOptions,
                               HANDLE FileInfo)
{
    PDOKAN_IO_SECURITY_CONTEXT SecurityContext = (PDOKAN_IO_SECURITY_CONTEXT)SecurityContextU;
    PDOKAN_FILE_INFO DokanFileInfo = (PDOKAN_FILE_INFO)FileInfo;

    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE handle;
    DWORD fileAttr;
    NTSTATUS status = STATUS_SUCCESS;
    DWORD creationDisposition;
    DWORD fileAttributesAndFlags;
    DWORD error = 0;
    SECURITY_ATTRIBUTES securityAttrib;
    ACCESS_MASK genericDesiredAccess;
    // userTokenHandle is for Impersonate Caller User Option
    HANDLE userTokenHandle = INVALID_HANDLE_VALUE;

    securityAttrib.nLength = sizeof(securityAttrib);
    securityAttrib.lpSecurityDescriptor =
        SecurityContext->AccessState.SecurityDescriptor;
    securityAttrib.bInheritHandle = FALSE;

    DokanMapKernelToUserCreateFileFlags(
        DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
        &genericDesiredAccess, &fileAttributesAndFlags, &creationDisposition);

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"CreateFile : %s\n", filePath);

    PrintUserName(DokanFileInfo);

    wprintf(L"\tShareMode = 0x%x\n", ShareAccess);

    MirrorCheckFlag(ShareAccess, FILE_SHARE_READ);
    MirrorCheckFlag(ShareAccess, FILE_SHARE_WRITE);
    MirrorCheckFlag(ShareAccess, FILE_SHARE_DELETE);

    wprintf(L"\tDesiredAccess = 0x%x\n", DesiredAccess);

    MirrorCheckFlag(DesiredAccess, GENERIC_READ);
    MirrorCheckFlag(DesiredAccess, GENERIC_WRITE);
    MirrorCheckFlag(DesiredAccess, GENERIC_EXECUTE);

    MirrorCheckFlag(DesiredAccess, DELETE);
    MirrorCheckFlag(DesiredAccess, FILE_READ_DATA);
    MirrorCheckFlag(DesiredAccess, FILE_READ_ATTRIBUTES);
    MirrorCheckFlag(DesiredAccess, FILE_READ_EA);
    MirrorCheckFlag(DesiredAccess, READ_CONTROL);
    MirrorCheckFlag(DesiredAccess, FILE_WRITE_DATA);
    MirrorCheckFlag(DesiredAccess, FILE_WRITE_ATTRIBUTES);
    MirrorCheckFlag(DesiredAccess, FILE_WRITE_EA);
    MirrorCheckFlag(DesiredAccess, FILE_APPEND_DATA);
    MirrorCheckFlag(DesiredAccess, WRITE_DAC);
    MirrorCheckFlag(DesiredAccess, WRITE_OWNER);
    MirrorCheckFlag(DesiredAccess, SYNCHRONIZE);
    MirrorCheckFlag(DesiredAccess, FILE_EXECUTE);
    MirrorCheckFlag(DesiredAccess, STANDARD_RIGHTS_READ);
    MirrorCheckFlag(DesiredAccess, STANDARD_RIGHTS_WRITE);
    MirrorCheckFlag(DesiredAccess, STANDARD_RIGHTS_EXECUTE);

    // When filePath is a directory, needs to change the flag so that the file can
    // be opened.
    fileAttr = GetFileAttributes(filePath);

    if (fileAttr != INVALID_FILE_ATTRIBUTES &&
        fileAttr & FILE_ATTRIBUTE_DIRECTORY) {
        if (CreateOptions & FILE_NON_DIRECTORY_FILE) {
            wprintf(L"\tCannot open a dir as a file\n");
            return STATUS_FILE_IS_A_DIRECTORY;
        }
        DokanFileInfo->IsDirectory = TRUE;
        // Needed by FindFirstFile to list files in it
        // TODO: use ReOpenFile in MirrorFindFiles to set share read temporary
        ShareAccess |= FILE_SHARE_READ;
    }

    wprintf(L"\tFlagsAndAttributes = 0x%x\n", fileAttributesAndFlags);

    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_ARCHIVE);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_COMPRESSED);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_DEVICE);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_DIRECTORY);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_ENCRYPTED);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_HIDDEN);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_INTEGRITY_STREAM);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NORMAL);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_NO_SCRUB_DATA);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_OFFLINE);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_READONLY);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_REPARSE_POINT);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_SPARSE_FILE);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_SYSTEM);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_TEMPORARY);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_ATTRIBUTE_VIRTUAL);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_WRITE_THROUGH);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_OVERLAPPED);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_NO_BUFFERING);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_RANDOM_ACCESS);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_SEQUENTIAL_SCAN);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_DELETE_ON_CLOSE);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_BACKUP_SEMANTICS);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_POSIX_SEMANTICS);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_OPEN_REPARSE_POINT);
    MirrorCheckFlag(fileAttributesAndFlags, FILE_FLAG_OPEN_NO_RECALL);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_ANONYMOUS);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_IDENTIFICATION);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_IMPERSONATION);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_DELEGATION);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_CONTEXT_TRACKING);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_EFFECTIVE_ONLY);
    MirrorCheckFlag(fileAttributesAndFlags, SECURITY_SQOS_PRESENT);

    if (CASE_SENSITIVE)
        fileAttributesAndFlags |= FILE_FLAG_POSIX_SEMANTICS;

    if (creationDisposition == CREATE_NEW) {
        wprintf(L"\tCREATE_NEW\n");
    } else if (creationDisposition == OPEN_ALWAYS) {
        wprintf(L"\tOPEN_ALWAYS\n");
    } else if (creationDisposition == CREATE_ALWAYS) {
        wprintf(L"\tCREATE_ALWAYS\n");
    } else if (creationDisposition == OPEN_EXISTING) {
        wprintf(L"\tOPEN_EXISTING\n");
    } else if (creationDisposition == TRUNCATE_EXISTING) {
        wprintf(L"\tTRUNCATE_EXISTING\n");
    } else {
        wprintf(L"\tUNKNOWN creationDisposition!\n");
    }

    if (IMPERSONATE_CALLER_USER) {
        userTokenHandle = DokanOpenRequestorToken(DokanFileInfo);

        if (userTokenHandle == INVALID_HANDLE_VALUE) {
            wprintf(L"  DokanOpenRequestorToken failed\n");
            // Should we return some error?
        }
    }

    if (DokanFileInfo->IsDirectory) {
        // It is a create directory request

        if (creationDisposition == CREATE_NEW ||
            creationDisposition == OPEN_ALWAYS) {

            if (IMPERSONATE_CALLER_USER && userTokenHandle != INVALID_HANDLE_VALUE) {
                // if IMPERSONATE_CALLER_USER option is on, call the ImpersonateLoggedOnUser function.
                if (!ImpersonateLoggedOnUser(userTokenHandle)) {
                    // handle the error if failed to impersonate
                    wprintf(L"\tImpersonateLoggedOnUser failed.\n");
                }
            }

            // We create folder
            if (!CreateDirectory(filePath, &securityAttrib)) {
                error = GetLastError();
                // Fail to create folder for OPEN_ALWAYS is not an error
                if (error != ERROR_ALREADY_EXISTS ||
                    creationDisposition == CREATE_NEW) {
                    wprintf(L"\terror code = %d\n\n", error);
                    status = DokanNtStatusFromWin32(error);
                }
            }

            if (IMPERSONATE_CALLER_USER && userTokenHandle != INVALID_HANDLE_VALUE) {
                // Clean Up operation for impersonate
                DWORD lastError = GetLastError();
                if (status != STATUS_SUCCESS) //Keep the handle open for CreateFile
                    CloseHandle(userTokenHandle);
                RevertToSelf();
                SetLastError(lastError);
            }
        }

        if (status == STATUS_SUCCESS) {

            //Check first if we're trying to open a file as a directory.
            if (fileAttr != INVALID_FILE_ATTRIBUTES &&
                !(fileAttr & FILE_ATTRIBUTE_DIRECTORY) &&
                (CreateOptions & FILE_DIRECTORY_FILE)) {
                return STATUS_NOT_A_DIRECTORY;
            }

            if (IMPERSONATE_CALLER_USER && userTokenHandle != INVALID_HANDLE_VALUE) {
                // if IMPERSONATE_CALLER_USER option is on, call the ImpersonateLoggedOnUser function.
                if (!ImpersonateLoggedOnUser(userTokenHandle)) {
                    // handle the error if failed to impersonate
                    wprintf(L"\tImpersonateLoggedOnUser failed.\n");
                }
            }

            // FILE_FLAG_BACKUP_SEMANTICS is required for opening directory handles
            handle =
                CreateFile(filePath, genericDesiredAccess, ShareAccess,
                           &securityAttrib, OPEN_EXISTING,
                           fileAttributesAndFlags | FILE_FLAG_BACKUP_SEMANTICS, NULL);

            if (IMPERSONATE_CALLER_USER && userTokenHandle != INVALID_HANDLE_VALUE) {
                // Clean Up operation for impersonate
                DWORD lastError = GetLastError();
                CloseHandle(userTokenHandle);
                RevertToSelf();
                SetLastError(lastError);
            }

            if (handle == INVALID_HANDLE_VALUE) {
                error = GetLastError();
                wprintf(L"\terror code = %d\n\n", error);

                status = DokanNtStatusFromWin32(error);
            } else {
                DokanFileInfo->Context =
                    (ULONG64)handle; // save the file handle in Context

                // Open succeed but we need to inform the driver
                // that the dir open and not created by returning STATUS_OBJECT_NAME_COLLISION
                if (creationDisposition == OPEN_ALWAYS &&
                    fileAttr != INVALID_FILE_ATTRIBUTES)
                    return STATUS_OBJECT_NAME_COLLISION;
            }
        }
    } else {
        // It is a create file request

        // Cannot overwrite a hidden or system file if flag not set
        if (fileAttr != INVALID_FILE_ATTRIBUTES &&
            ((!(fileAttributesAndFlags & FILE_ATTRIBUTE_HIDDEN) &&
              (fileAttr & FILE_ATTRIBUTE_HIDDEN)) ||
             (!(fileAttributesAndFlags & FILE_ATTRIBUTE_SYSTEM) &&
              (fileAttr & FILE_ATTRIBUTE_SYSTEM))) &&
            (creationDisposition == TRUNCATE_EXISTING ||
             creationDisposition == CREATE_ALWAYS))
            return STATUS_ACCESS_DENIED;

        // Cannot delete a read only file
        if ((fileAttr != INVALID_FILE_ATTRIBUTES &&
                 (fileAttr & FILE_ATTRIBUTE_READONLY) ||
             (fileAttributesAndFlags & FILE_ATTRIBUTE_READONLY)) &&
            (fileAttributesAndFlags & FILE_FLAG_DELETE_ON_CLOSE))
            return STATUS_CANNOT_DELETE;

        // Truncate should always be used with write access
        if (creationDisposition == TRUNCATE_EXISTING)
            genericDesiredAccess |= GENERIC_WRITE;

        if (IMPERSONATE_CALLER_USER && userTokenHandle != INVALID_HANDLE_VALUE) {
            // if IMPERSONATE_CALLER_USER option is on, call the ImpersonateLoggedOnUser function.
            if (!ImpersonateLoggedOnUser(userTokenHandle)) {
                // handle the error if failed to impersonate
                wprintf(L"\tImpersonateLoggedOnUser failed.\n");
            }
        }

        handle =
            CreateFile(filePath, genericDesiredAccess, ShareAccess, &securityAttrib,
                       creationDisposition, fileAttributesAndFlags, NULL);

        if (IMPERSONATE_CALLER_USER && userTokenHandle != INVALID_HANDLE_VALUE) {
            // Clean Up operation for impersonate
            DWORD lastError = GetLastError();
            CloseHandle(userTokenHandle);
            RevertToSelf();
            SetLastError(lastError);
        }

        if (handle == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            wprintf(L"\terror code = %d\n\n", error);

            status = DokanNtStatusFromWin32(error);
        } else {

            //Need to update FileAttributes with previous when Overwrite file
            if (fileAttr != INVALID_FILE_ATTRIBUTES &&
                creationDisposition == TRUNCATE_EXISTING) {
                SetFileAttributes(filePath, fileAttributesAndFlags | fileAttr);
            }

            DokanFileInfo->Context =
                (ULONG64)handle; // save the file handle in Context

            if (creationDisposition == OPEN_ALWAYS ||
                creationDisposition == CREATE_ALWAYS) {
                error = GetLastError();
                if (error == ERROR_ALREADY_EXISTS) {
                    wprintf(L"\tOpen an already existing file\n");
                    // Open succeed but we need to inform the driver
                    // that the file open and not created by returning STATUS_OBJECT_NAME_COLLISION
                    status = STATUS_OBJECT_NAME_COLLISION;
                }
            }
        }
    }

    wprintf(L"\n");
    return status;
}

long DokanBackend::FD_FindStreams(LPCWSTR FileName,
                                HANDLE FillFindStreamDataU,
                                HANDLE FindStreamContext)
{
    PFillFindStreamData FillFindStreamData = (PFillFindStreamData)FillFindStreamDataU;

    WCHAR filePath[DOKAN_MAX_PATH];
    HANDLE hFind;
    WIN32_FIND_STREAM_DATA findData;
    DWORD error;
    int count = 0;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"FindStreams :%s\n", filePath);

    hFind = FindFirstStreamW(filePath, FindStreamInfoStandard, &findData, 0);

    if (hFind == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        wprintf(L"\tinvalid file handle. Error is %u\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    BOOL bufferFull = FillFindStreamData(&findData, FindStreamContext);
    if (bufferFull) {
        count++;
        while (FindNextStreamW(hFind, &findData) != 0) {
            bufferFull = FillFindStreamData(&findData, FindStreamContext);
            if (!bufferFull)
                break;
            count++;
        }
    }

    error = GetLastError();
    FindClose(hFind);

    if (!bufferFull) {
        wprintf(L"\tFindStreams returned %d entries in %s with "
                L"STATUS_BUFFER_OVERFLOW\n\n",
                count, filePath);
        // https://msdn.microsoft.com/en-us/library/windows/hardware/ff540364(v=vs.85).aspx
        return STATUS_BUFFER_OVERFLOW;
    }

    if (error != ERROR_HANDLE_EOF) {
        wprintf(L"\tFindNextStreamW error. Error is %u\n\n", error);
        return DokanNtStatusFromWin32(error);
    }

    wprintf(L"\tFindStreams return %d entries in %s\n\n", count, filePath);

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

FindFilesResult *DokanBackend::FD_FindFiles(LPCWSTR FileName)
{
    FindFilesResult *result = (FindFilesResult *)malloc(sizeof(FindFilesResult));
    memset(result, 0, sizeof(FindFilesResult));

    std::vector<WIN32_FIND_DATAW> findDataList;

    WCHAR filePath[DOKAN_MAX_PATH];
    size_t fileLen;
    WIN32_FIND_DATAW findData;
    HANDLE hFind;
    DWORD error;

    GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

    wprintf(L"FindFiles : %s\n", filePath);

    fileLen = wcslen(filePath);
    if (filePath[fileLen - 1] != L'\\')
    {
        filePath[fileLen++] = L'\\';
    }
    if (fileLen + 1 >= DOKAN_MAX_PATH)
    {
        result->ntStatus = STATUS_BUFFER_OVERFLOW;
        return result;
    }
    filePath[fileLen] = L'*';
    filePath[fileLen + 1] = L'\0';

    hFind = FindFirstFile(filePath, &findData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        wprintf(L"\tinvalid file handle. Error is %u\n\n", error);
        result->ntStatus = DokanNtStatusFromWin32(error);
        return result;
    }

    qDebug() << "[FD_FindFiles] findData.cFileName: " << wchar_to_utf8(findData.cFileName);

    BOOLEAN rootFolder = (wcscmp(FileName, L"\\") == 0);
    do {
        if (!rootFolder || (wcscmp(findData.cFileName, L".") != 0 &&
                            wcscmp(findData.cFileName, L"..") != 0))
        {
            qDebug() << "[FD_FindFiles] findData.cFileName: " << wchar_to_utf8(findData.cFileName);
            findDataList.push_back(findData);
        }
    } while (FindNextFile(hFind, &findData) != 0);

    error = GetLastError();
    FindClose(hFind);

    if (error != ERROR_NO_MORE_FILES) {
        wprintf(L"\tFindNextFile error. Error is %u\n\n", error);
        result->ntStatus = DokanNtStatusFromWin32(error);
        return result;
    }

    result->ntStatus = STATUS_SUCCESS;
    result->count = findDataList.size();
    result->dataSize = sizeof(WIN32_FIND_DATAW) * result->count;
    result->findData = malloc(result->dataSize);
    memcpy(result->findData, findDataList.data(), result->dataSize);

    wprintf(L"\tFindFiles return %d entries in %s\n\n", result->count, filePath);

    qDebug() << "[FD_FindFiles] return";

    return result;
}

#endif
