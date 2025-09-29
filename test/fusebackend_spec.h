#include "fusebackend.h"

#include <QTest>
#include <QDir>
#include <QLoggingCategory>
#include <QFile>
#include <QStorageInfo>

#include <filesystem>

class FUSEBackend_spec : public QObject
{
    Q_OBJECT

private:
    QString appIconPath = QDir::currentPath() + "/assets/filedonkey_app_icon.ico";

private slots:
    void Returns_correct_GetattrResult()
    {
        Ref<GetattrResult> result = FUSEBackend::FD_getattr(appIconPath.toStdString().c_str());

        QFile appIcon = QFile(appIconPath);
        qint64 fileAccessTime = appIcon.fileTime(QFileDevice::FileAccessTime).toSecsSinceEpoch();
        qint64 fileModificationTime = appIcon.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch();
        qint64 fileStatusChangeTime = appIcon.fileTime(QFileDevice::FileMetadataChangeTime).toSecsSinceEpoch();

        QVERIFY(appIcon.exists());

        QCOMPARE(result->status, 0);
        QCOMPARE(result->st_nlink, 1);
        QCOMPARE(result->st_mode, 33188);
        QCOMPARE(result->st_rdev, 0);
        QCOMPARE(result->st_size, 130121);
        QCOMPARE(result->st_blksize, 4096);
        QCOMPARE(result->st_blocks, 256);
        QCOMPARE(result->st_atim.tv_sec, fileAccessTime);
        QCOMPARE(result->st_mtim.tv_sec, fileModificationTime);
        QCOMPARE(result->st_ctim.tv_sec, fileStatusChangeTime);

        //----------------------------------------------------------------------
        // Platfrom specific
        //----------------------------------------------------------------------
        // QCOMPARE(result->st_dev, 0);
        // QCOMPARE(result->st_ino, 0);
        // QCOMPARE(result->st_uid, 0);
        // QCOMPARE(result->st_gid, 0);

        //----------------------------------------------------------------------
        // Qt does not support ns, only ms
        //----------------------------------------------------------------------
        // QCOMPARE(result->st_atim.tv_nsec, 0);
        // QCOMPARE(result->st_mtim.tv_nsec, 0);
        // QCOMPARE(result->st_ctim.tv_nsec, 0);
    }

    void Returns_correct_ReadResult()
    {
        const u32 BLOCK_SIZE = 65535;
        Ref<ReadResult> result = FUSEBackend::FD_read(appIconPath.toStdString().c_str(), BLOCK_SIZE, 0);

        QFile appIcon = QFile(appIconPath);
        appIcon.open(QIODeviceBase::ReadOnly);
        char fileData[BLOCK_SIZE];
        appIcon.read(fileData, BLOCK_SIZE);
        appIcon.close();

        QCOMPARE(result->status, BLOCK_SIZE);
        QCOMPARE(result->size, BLOCK_SIZE);
        QCOMPARE(memcmp(result->data, fileData, BLOCK_SIZE), 0);
    }

    void Returns_correct_StatfsResult()
    {
        Ref<StatfsResult> result = FUSEBackend::FD_statfs(QDir::currentPath().toStdString().c_str());
        QStorageInfo storageInfo(QDir::currentPath());

        QCOMPARE(result->status, 0);
        QCOMPARE(result->f_bsize, storageInfo.blockSize());

        QCOMPARE(result->f_blocks, storageInfo.bytesTotal() / storageInfo.blockSize());
        QCOMPARE(result->f_bfree, storageInfo.bytesFree() / storageInfo.blockSize());
        QCOMPARE(result->f_bavail, storageInfo.bytesAvailable() / storageInfo.blockSize());
        QCOMPARE(result->f_namemax, 255);

        //----------------------------------------------------------------------
        // Not available on Windows
        //----------------------------------------------------------------------
        // QCOMPARE(result->f_files, 0);
        // QCOMPARE(result->f_ffree, 0);
        // QCOMPARE(result->f_favail, 0);

        //----------------------------------------------------------------------
        // Different on each machine
        //----------------------------------------------------------------------
        // QCOMPARE(result->f_frsize, 4096);
        // QCOMPARE(result->f_fsid, 0);
        // QCOMPARE(result->f_flag, 0);
    }

    void Returns_correct_ReaddirResult()
    {
        QString assetsPath = QDir::currentPath() + "/assets/";
        Ref<ReaddirResult> result = FUSEBackend::FD_readdir(assetsPath.toStdString().c_str());
        FindData *fd = (FindData *)result->findData;

        QDir dir(assetsPath);
        static QRegularExpression re("[^\\.]");
        auto files = dir.entryList().filter(re); // We don't care about '.' and '..' files

        QCOMPARE(files.size(), 4);
        QVERIFY(files.contains("filedonkey_app_icon.ico"));
        QVERIFY(files.contains("filedonkey_tray_icon_dark.ico"));
        QVERIFY(files.contains("filedonkey_tray_icon_light.ico"));
        QVERIFY(files.contains("filedonkey_app_icon_symlink.ico"));

        QCOMPARE(result->status, 0);
        QCOMPARE(result->count, 4);
        QCOMPARE(result->dataSize, sizeof(FindData) * 4);

        QVERIFY(files.contains((fd + 0)->name));
        QVERIFY(files.contains((fd + 1)->name));
        QVERIFY(files.contains((fd + 2)->name));
        QVERIFY(files.contains((fd + 3)->name));

        // We do this because files order is different on different machines
        // and we don't know which one is a symlink.
        u16 entry0mode = strstr((fd + 0)->name, "_symlink") ? 40960 : 32768;
        u16 entry1mode = strstr((fd + 1)->name, "_symlink") ? 40960 : 32768;
        u16 entry2mode = strstr((fd + 2)->name, "_symlink") ? 40960 : 32768;
        u16 entry3mode = strstr((fd + 3)->name, "_symlink") ? 40960 : 32768;

        QCOMPARE((fd + 0)->st_mode, entry0mode);
        QCOMPARE((fd + 1)->st_mode, entry1mode);
        QCOMPARE((fd + 2)->st_mode, entry2mode);
        QCOMPARE((fd + 3)->st_mode, entry3mode);

        //----------------------------------------------------------------------
        // Different on each machine
        //----------------------------------------------------------------------
        // QCOMPARE((fd + 0)->st_ino, 0);
        // QCOMPARE((fd + 1)->st_ino, 0);
        // QCOMPARE((fd + 2)->st_ino, 0);
    }

    void Returns_correct_ReadlinkResult()
    {
        QString symLinkPath = QDir::currentPath() + "/assets/filedonkey_app_icon_symlink.ico";

        const u32 BLOCK_SIZE = 65535;
        Ref<ReadlinkResult> result = FUSEBackend::FD_readlink(symLinkPath.toStdString().c_str(), BLOCK_SIZE);

        std::filesystem::path appIconFSPath = appIconPath.toStdString();
        std::filesystem::path symLinkDataPath = QString(result->data).toStdString();

        QCOMPARE(result->status, 0);
        QCOMPARE(result->size, BLOCK_SIZE);
        QVERIFY(appIconFSPath == symLinkDataPath);
    }

    void Writes_data_to_file()
    {
        QString filePath = QDir::currentPath() + "/assets/test.txt";

        QFile tmp(filePath);
        if (QFile::exists(filePath)) QFile::remove(filePath);
        if (tmp.open(QIODevice::WriteOnly | QIODevice::Text)) tmp.close();

        const char *data = "Hello, World!";
        i32 result = FUSEBackend::FD_write(filePath.toStdString().c_str(), data, strlen(data), 0);

        QFile file = QFile(filePath);
        file.open(QIODeviceBase::ReadOnly);
        char fileData[strlen(data)];
        file.read(fileData, strlen(data));
        file.close();
        QFile::remove(filePath);
        fileData[strlen(data)] = '\0';

        QCOMPARE(result, strlen(data));
        QCOMPARE(strcmp(fileData, data), 0);
    }
};
