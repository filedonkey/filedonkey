#ifndef DEVICELIST_H
#define DEVICELIST_H

#include "connection.h"

#include <QList>
#include <QMap>
#include <QWidget>

class QLabel;
class QVBoxLayout;

class DeviceRow;

// The window's content: one row per peer, in the order they arrived, an empty state in their place
// while there are none, and a footer offering the one way into this list that does not depend on a
// peer having been found.
//
// It keeps no state of its own beyond those rows. Everything it shows arrives through the slots
// below, one per LocalNode signal, and nothing here calls back into the node: a mounted row opens
// itself in the desktop's file manager when clicked, which asks the node for nothing, and the
// footer's button asks for an address and hands it out as the signal below rather than dialling it
// itself. The design's per-device actions - Mount, Unmount, Retry - have nothing behind them yet,
// and a button that looks live and does nothing is worse than no button.
class DeviceList : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceList(QWidget *parent = nullptr);

    // A state dot and a running count of what has been found, for the window to hand to its status
    // bar. Built here because everything it says is this list's own state - where it goes is the
    // window's business, and whoever takes it becomes its parent.
    QWidget *summaryWidget() const { return summary; }

    // One peer as anything outside this list sees it: the name it is shown under, and where its
    // mount came up. The tray menu is what asks - see MainWindow::refreshTrayDevices().
    struct Device
    {
        QString name;

        // Empty while the mount is still coming up, which is what tells the two states apart -
        // the menu picks its dot by it, the way a row picks the colour of its own.
        QString mountPoint;
    };

    // Read off the rows rather than kept alongside them, so there is no second copy of who is
    // connected to fall out of step. In the order the rows are shown in, which is the order the
    // peers arrived - not the order the map below holds them in.
    QList<Device> devices() const;

signals:
    // An address and a port the user typed into the manual connect dialog, on their way to
    // LocalNode::connectManually(). The window is what carries them there - this list has never
    // held a node and does not start now.
    void manualConnectRequested(const QString &address, int port);

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

    // The footer's button. Asks for an address and emits it; nothing here waits on the answer.
    void openManualConnect();

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
