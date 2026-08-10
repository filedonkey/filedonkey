#ifndef DEVICELIST_H
#define DEVICELIST_H

#include "connection.h"

#include <QMap>
#include <QWidget>

class QLabel;
class QVBoxLayout;

class DeviceRow;

// The window's content: a header counting what has been found, then one row per peer in the order
// they arrived, and an empty state in their place while there are none.
//
// It keeps no state of its own beyond those rows. Everything it shows arrives through the three
// slots below, one per LocalNode signal, and nothing here calls back into the node: the design's
// per-device actions - Mount, Unmount, Retry - have nothing behind them yet, and a button that
// looks live and does nothing is worse than no button.
class DeviceList : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceList(QWidget *parent = nullptr);

public slots:
    // A peer has answered a broadcast and its mount has been started. Wired to LocalNode::peerAdded.
    void onPeerAdded(const Connection &conn);

    // That mount is up, on the drive letter or directory the peer's VirtDisk picked for it.
    void onPeerMounted(const QString &machineId, const QString &mountPoint);

    // The peer's VirtDisk has stopped - it went away, or the mount never came up. Either way the
    // row goes: LocalNode has forgotten the peer too, so the next broadcast starts it over.
    void onPeerRemoved(const QString &machineId);

private:
    void refreshHeader();

    QLabel      *countDot   = nullptr;
    QLabel      *countLbl   = nullptr;
    QWidget     *emptyState = nullptr;
    QVBoxLayout *rowsLayout = nullptr;

    // Keyed the way the signals above address a row. The order they are shown in is the layout's
    // business, not this map's.
    QMap<QString, DeviceRow *> rows;
};

#endif // DEVICELIST_H
