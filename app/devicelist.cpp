#include "devicelist.h"

#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

// The row, in the order it is built: a platform badge, the machine name with its state dot beside
// it, and a mono line under them for where the peer has been mounted. These are here rather than in
// the stylesheet because QSS sizes the content box - the badge carries a 1px border, so a min-width
// there would have to be this number minus two, kept in step by hand.
#define BADGE_SIZE 30
#define DOT_SIZE    6

namespace {

// A stylesheet rule that selects on a property only takes hold once the style has looked at the
// widget again, and setProperty() alone does not ask it to.
void restyle(QWidget *widget, const char *name, const QVariant &value)
{
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

// A label that shortens what it draws rather than asking the layout for room it cannot have. The
// window is a fixed size and the list has no horizontal scrollbar, so a long machine name or mount
// path would otherwise be cut off mid-letter at the edge of the row.
//
// Elided at paint time and never through setText(): shortening the text would shrink the label's
// own size hint, the layout would hand it less room next time round, and it would elide further on
// every pass. text() stays the full string, so it is what each repaint - and the tooltip - starts
// from.
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    // Right for a name, middle for a mount point: the tail of a path is the part worth keeping.
    Qt::TextElideMode elideMode = Qt::ElideRight;

    // What lets the layout shrink it at all. QLabel's own minimum for one unwrapped line is the
    // whole string, and that would push the row - and with it the window - wider.
    QSize minimumSizeHint() const override
    {
        return QSize(0, QLabel::minimumSizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);

        // The stylesheet's `color:` reaches a label through its palette, and QPainter's default pen
        // does not read it - drawing without this would put every label back to black.
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(rect(), alignment(), fontMetrics().elidedText(text(), elideMode, width()));
    }
};

} // namespace

// One peer. Built once, from the connection that announced it, and changed only by setMounted()
// when its mount finally comes up. Named in devicelist.h so the list can hold rows by pointer;
// there is nothing else to it worth putting in a header.
//
// No Q_OBJECT and so no tr() of its own - the two strings it shows are translated in DeviceList's
// context, where the rest of this file's are.
class DeviceRow : public QWidget
{
public:
    explicit DeviceRow(const Connection &conn, QWidget *parent = nullptr);

    void setMounted(const QString &mountPoint);
    bool isMounted() const { return mounted; }

private:
    void refreshToolTip();

    Connection   conn;
    QLabel      *dotLbl    = nullptr;
    ElidedLabel *detailLbl = nullptr;

    QString mountPoint;
    bool    mounted = false;
};

DeviceRow::DeviceRow(const Connection &conn, QWidget *parent)
    : QWidget(parent)
    , conn(conn)
{
    setObjectName("deviceRow");

    // A QWidget subclass draws no stylesheet background unless it is asked to, and without one
    // there is nothing for the hover rule to fill.
    setAttribute(Qt::WA_StyledBackground, true);

    // The badge is the first three letters of what the peer broadcast as its QSysInfo::productType(),
    // which is the tag the design asks for on every platform this runs on: windows -> win,
    // macos -> mac, android -> and, ios -> ios, and a Linux distribution reads as itself - ubu, deb,
    // arc. A peer from before the broadcast carried the field at all sends nothing.
    QString badge = conn.machineOs.left(3).toLower();
    if (badge.isEmpty()) badge = "?";

    QLabel *badgeLbl = new QLabel(badge, this);
    badgeLbl->setObjectName("deviceBadge");
    badgeLbl->setAlignment(Qt::AlignCenter);
    badgeLbl->setFixedSize(BADGE_SIZE, BADGE_SIZE);

    ElidedLabel *nameLbl = new ElidedLabel(conn.machineName, this);
    nameLbl->setObjectName("deviceName");

    dotLbl = new QLabel(this);
    dotLbl->setObjectName("deviceDot");
    dotLbl->setFixedSize(DOT_SIZE, DOT_SIZE);

    detailLbl = new ElidedLabel(this);
    detailLbl->setObjectName("deviceDetail");
    detailLbl->elideMode = Qt::ElideMiddle;

    QHBoxLayout *titleLine = new QHBoxLayout;
    titleLine->setContentsMargins(0, 0, 0, 0);
    titleLine->setSpacing(7);
    titleLine->addWidget(nameLbl);

    // Aligned explicitly, or the layout leaves a widget this much shorter than its row sitting at
    // the top of it and the dot rides above the name's cap height.
    titleLine->addWidget(dotLbl, 0, Qt::AlignVCenter);
    titleLine->addStretch(1);

    QVBoxLayout *textColumn = new QVBoxLayout;
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(3);
    textColumn->addLayout(titleLine);
    textColumn->addWidget(detailLbl);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 9, 10, 9);
    layout->setSpacing(11);
    layout->addWidget(badgeLbl, 0, Qt::AlignVCenter);
    layout->addLayout(textColumn, 1);

    // Where every row starts: LocalNode emits peerAdded with the mount already under way, so there
    // is no moment at which a peer is known but nothing is being done about it.
    restyle(dotLbl, "state", "mounting");
    detailLbl->setText(DeviceList::tr("mounting · %1").arg(conn.machineAddress));

    refreshToolTip();
}

void DeviceRow::setMounted(const QString &mountPoint)
{
    mounted = true;
    this->mountPoint = mountPoint;

    restyle(dotLbl, "state", "mounted");
    restyle(detailLbl, "state", "mounted");

    // What fuse_mount was handed on Windows is the bare drive, "D:". The design writes it the way
    // Explorer does; elsewhere the mount point is a path already and this leaves it alone.
    QString shown = mountPoint;
    if (shown.endsWith(':')) shown += '\\';

    detailLbl->setText(shown);

    refreshToolTip();
}

// Everything the row knows, for the rows too narrow to show all of it - which on a window this
// width is most of them once a mount point is a full path.
void DeviceRow::refreshToolTip()
{
    QString text = QString("%1\n%2:%3").arg(conn.machineName, conn.machineAddress).arg(conn.machinePort);
    if (mounted) text += "\n" + mountPoint;

    setToolTip(text);
}

DeviceList::DeviceList(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("deviceList");
    setAttribute(Qt::WA_StyledBackground, true);

    QLabel *headerLbl = new QLabel(tr("Devices").toUpper(), this);
    headerLbl->setObjectName("deviceHeaderLbl");

    // The same micro-label treatment as "Network:" in the status bar, and tracked out here for the
    // same reason: there is no letter-spacing in QSS.
    QFont microLabel = headerLbl->font();
    microLabel.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    headerLbl->setFont(microLabel);

    // The whole list's state in one dot, the same three colours the rows use.
    countDot = new QLabel(this);
    countDot->setObjectName("deviceDot");
    countDot->setFixedSize(DOT_SIZE, DOT_SIZE);

    countLbl = new QLabel(this);
    countLbl->setObjectName("deviceCountLbl");

    QHBoxLayout *header = new QHBoxLayout;
    header->setContentsMargins(14, 11, 14, 10);
    header->setSpacing(6);
    header->addWidget(headerLbl);
    header->addStretch(1);
    header->addWidget(countDot, 0, Qt::AlignVCenter);
    header->addWidget(countLbl);

    QFrame *headerRule = new QFrame(this);
    headerRule->setObjectName("deviceHeaderRule");
    headerRule->setFixedHeight(1);

    QLabel *emptyTitle = new QLabel(tr("No devices yet"), this);
    emptyTitle->setObjectName("deviceEmptyTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);

    QLabel *emptyText = new QLabel(tr("FileDonkey is listening for other machines on this network. "
                                      "Start it on one of them and it shows up here."), this);
    emptyText->setObjectName("deviceEmptyText");
    emptyText->setAlignment(Qt::AlignCenter);
    emptyText->setWordWrap(true);

    emptyState = new QWidget(this);

    QVBoxLayout *emptyLayout = new QVBoxLayout(emptyState);
    emptyLayout->setContentsMargins(28, 30, 28, 26);
    emptyLayout->setSpacing(8);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);

    // The rows and the empty state share one column: they are never both wanted, and this way the
    // scroll area has a single widget to size itself against.
    QWidget *rowsHost = new QWidget(this);
    rowsHost->setObjectName("deviceRows");

    rowsLayout = new QVBoxLayout(rowsHost);
    rowsLayout->setContentsMargins(6, 6, 6, 6);
    rowsLayout->setSpacing(2);
    rowsLayout->addWidget(emptyState);
    rowsLayout->addStretch(1);

    // The window cannot be resized, so this is what makes the fourth device onwards reachable.
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setObjectName("deviceScroll");
    scroll->setWidget(rowsHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(headerRule);
    layout->addWidget(scroll, 1);

    refreshHeader();
}

void DeviceList::onPeerAdded(const Connection &conn)
{
    // LocalNode ignores a machine it already has, so this is a peer we have never seen - unless a
    // row was left behind by a peerRemoved that never came, and then this would show it twice.
    if (rows.contains(conn.machineId)) return;

    DeviceRow *row = new DeviceRow(conn);
    rows.insert(conn.machineId, row);

    // Before the stretch that holds the rows at the top, and after the empty state, which is
    // hidden from here on.
    rowsLayout->insertWidget(rowsLayout->count() - 1, row);

    refreshHeader();
}

void DeviceList::onPeerMounted(const QString &machineId, const QString &mountPoint)
{
    DeviceRow *row = rows.value(machineId, nullptr);
    if (!row) return;

    row->setMounted(mountPoint);

    refreshHeader();
}

void DeviceList::onPeerRemoved(const QString &machineId)
{
    DeviceRow *row = rows.take(machineId);
    if (!row) return;

    // Not deleteLater(): this arrives on the GUI thread from LocalNode, with nothing of the row's
    // own on the stack.
    delete row;

    refreshHeader();
}

void DeviceList::refreshHeader()
{
    emptyState->setVisible(rows.isEmpty());

    countLbl->setText(rows.size() == 1 ? tr("1 device") : tr("%1 devices").arg(rows.size()));

    // Green as soon as one mount is up, amber while they are all still coming up, and the default
    // grey of the stylesheet's dot rule when there is nothing to report.
    QString state;
    for (const DeviceRow *row : std::as_const(rows))
    {
        state = "mounting";
        if (row->isMounted())
        {
            state = "mounted";
            break;
        }
    }

    restyle(countDot, "state", state);
}
