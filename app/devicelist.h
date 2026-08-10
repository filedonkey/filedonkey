#ifndef DEVICELIST_H
#define DEVICELIST_H

#include "connection.h"

#include <QMap>
#include <QWidget>

class QLabel;
class QVBoxLayout;

class DeviceRow;

// The window's content: one row per peer, in the order they arrived, and an empty state in their
// place while there are none.
//
// It keeps no state of its own beyond those rows. Everything it shows arrives through the slots
// below, one per LocalNode signal, and nothing here calls back into the node: a mounted row opens
// itself in the desktop's file manager when clicked, which asks the node for nothing. The design's
// per-device actions - Mount, Unmount, Retry - have nothing behind them yet, and a button that
// looks live and does nothing is worse than no button.
class DeviceList : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceList(QWidget *parent = nullptr);

    // A state dot and a running count of what has been found, for the window to hand to its status
    // bar. Built here because everything it says is this list's own state - where it goes is the
    // window's business, and whoever takes it becomes its parent.
    QWidget *summaryWidget() const { return summary; }

public slots:
    // A peer has answered a broadcast and its mount has been started. Wired to LocalNode::peerAdded.
    void onPeerAdded(const Connection &conn);

    // That mount is up, on the drive letter or directory the peer's VirtDisk picked for it.
    void onPeerMounted(const QString &machineId, const QString &mountPoint);

    // Running totals for one peer's mount, which is where they belong: every device moves its own
    // bytes over its own connection, and the one pair of labels in the status bar could only ever
    // show whichever of them had moved some last.
    void onPeerUploaded(const QString &machineId, u64 uploaded);
    void onPeerDownloaded(const QString &machineId, u64 downloaded);

    // The peer's VirtDisk has stopped - it went away, or the mount never came up. Either way the
    // row goes: LocalNode has forgotten the peer too, so the next broadcast starts it over.
    void onPeerRemoved(const QString &machineId);

private:
    void refreshSummary();

    QWidget     *summary    = nullptr;
    QLabel      *summaryDot = nullptr;
    QLabel      *summaryLbl = nullptr;
    QWidget     *emptyState = nullptr;
    QVBoxLayout *rowsLayout = nullptr;

    // Keyed the way the signals above address a row. The order they are shown in is the layout's
    // business, not this map's.
    QMap<QString, DeviceRow *> rows;
};

#endif // DEVICELIST_H
