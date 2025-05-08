#ifndef DOKANBACKEND_H
#define DOKANBACKEND_H

#define __int64 long long
typedef unsigned __int64 ULONGLONG;
typedef ULONGLONG *PULONGLONG;

#define __LONG32 long
typedef unsigned __LONG32 DWORD,ULONG;
typedef DWORD *LPDWORD;

typedef wchar_t WCHAR;
typedef WCHAR *NWPSTR,*LPWSTR,*PWSTR;
typedef WCHAR *PWCHAR,*LPWCH,*PWCH;

typedef const WCHAR *LPCWSTR,*PCWSTR;

typedef void *HANDLE;

struct FindFilesResult
{
    long ntStatus;
    void *findData;
    unsigned int dataSize;
    unsigned int count;
};

class DokanBackend
{
public:
    static long FD_GetDiskFreeSpace(PULONGLONG FreeBytesAvailable,
                                    PULONGLONG TotalNumberOfBytes,
                                    PULONGLONG TotalNumberOfFreeBytes);

    static long FD_GetVolumeInformation(LPWSTR VolumeNameBuffer,
                                        DWORD VolumeNameSize,
                                        LPDWORD VolumeSerialNumber,
                                        LPDWORD MaximumComponentLength,
                                        LPDWORD FileSystemFlags,
                                        LPWSTR FileSystemNameBuffer,
                                        DWORD FileSystemNameSize);

    static long FD_GetFileInformation(LPCWSTR FileName,
                                      HANDLE HandleFileInformationU,
                                      HANDLE handle);

    static void FD_CloseFile(LPCWSTR FileName,
                             HANDLE FileInfo);

    static void FD_Cleanup(LPCWSTR FileName,
                           HANDLE FileInfo);

    static long FD_CreateFile(LPCWSTR FileName,
                              HANDLE SecurityContextU,
                              DWORD DesiredAccess,
                              ULONG FileAttributes,
                              ULONG ShareAccess,
                              ULONG CreateDisposition,
                              ULONG CreateOptions,
                              HANDLE FileInfo);

    static long FD_FindStreams(LPCWSTR FileName,
                               HANDLE FillFindStreamDataU,
                               HANDLE FindStreamContext);

    static FindFilesResult *FD_FindFiles(LPCWSTR FileName);
};

#endif // DOKANBACKEND_H
