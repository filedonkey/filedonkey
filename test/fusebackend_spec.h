#include "fusebackend.h"

#include <QtTest>
#include <QDir>
#include <QLoggingCategory>
#include <QFile>

class FUSEBackend_spec : public QObject
{
    Q_OBJECT

private slots:
    void Returns_correct_GetattrResult()
    {
        QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, false);

        QString appIconPath = QDir::currentPath() + "/assets/filedonkey_app_icon.ico";
        Ref<GetattrResult> result = FUSEBackend::FD_getattr(appIconPath.toStdString().c_str());

        QFile appIcon = QFile(appIconPath);
        qint64 fileAccessTime = appIcon.fileTime(QFileDevice::FileAccessTime).toSecsSinceEpoch();
        qint64 fileModificationTime = appIcon.fileTime(QFileDevice::FileModificationTime).toSecsSinceEpoch();
        qint64 fileStatusChangeTime = appIcon.fileTime(QFileDevice::FileBirthTime).toSecsSinceEpoch();

        QCOMPARE(appIcon.exists(), true);

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

        // Platfrom specific
        // QCOMPARE(result->st_dev, 0);
        // QCOMPARE(result->st_ino, 0);
        // QCOMPARE(result->st_uid, 0);
        // QCOMPARE(result->st_gid, 0);

        // Qt does not support ns, only ms
        // QCOMPARE(result->st_atim.tv_nsec, 0);
        // QCOMPARE(result->st_mtim.tv_nsec, 0);
        // QCOMPARE(result->st_ctim.tv_nsec, 0);
    }

    void Returns_correct_ReadResult()
    {
        const u32 BLOCK_SIZE = 65535;
        QString appIconPath = QDir::currentPath() + "/assets/filedonkey_app_icon.ico";
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
};
