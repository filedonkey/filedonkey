#include "devicelist.h"

#include <QDesktopServices>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

// The row, in the order it is built: a platform badge, the machine name with its state dot beside
// it, and a mono line under them carrying the mount point and what has moved across it. These are
// here rather than in the stylesheet because QSS sizes the content box - the badge carries a 1px
// border, so a min-width there would have to be this number minus two, kept in step by hand.
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
// window is a fixed size and the list has no horizontal scrollbar, so a long machine name would
// otherwise be cut off mid-letter at the edge of the row.
//
// Elided at paint time and never through setText(): shortening the text would shrink the label's
// own size hint, the layout would hand it less room next time round, and it would elide further on
// every pass. text() stays the full string, so it is what each repaint - and the tooltip - starts
// from.
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

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
        painter.drawText(rect(), alignment(), fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

} // namespace

// One peer. Built once, from the connection that announced it, and changed after that only by the
// mount coming up and by the bytes it then moves. Named in devicelist.h so the list can hold rows
// by pointer; there is nothing else to it worth putting in a header.
//
// No Q_OBJECT and so no tr() of its own - the strings it shows are translated in DeviceList's
// context, where the rest of this file's are.
class DeviceRow : public QWidget
{
public:
    explicit DeviceRow(const Connection &conn, QWidget *parent = nullptr);

    void setMounted(const QString &mountPoint);
    void setUploaded(u64 uploaded);
    void setDownloaded(u64 downloaded);

    bool isMounted() const { return mounted; }

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void refreshDetail();
    void refreshToolTip();

    Connection   conn;
    QLabel      *dotLbl    = nullptr;
    QLabel      *mountLbl  = nullptr;
    ElidedLabel *detailLbl = nullptr;

    QString mountPoint;
    bool    mounted    = false;
    u64     uploaded   = 0;
    u64     downloaded = 0;
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

    QHBoxLayout *titleLine = new QHBoxLayout;
    titleLine->setContentsMargins(0, 0, 0, 0);
    titleLine->setSpacing(7);
    titleLine->addWidget(nameLbl);

    // Aligned explicitly, or the layout leaves a widget this much shorter than its row sitting at
    // the top of it and the dot rides above the name's cap height.
    titleLine->addWidget(dotLbl, 0, Qt::AlignVCenter);
    titleLine->addStretch(1);

    QHBoxLayout *detailLine = new QHBoxLayout;
    detailLine->setContentsMargins(0, 0, 0, 0);
    detailLine->setSpacing(9);

#if defined(Q_OS_WIN)
    // Windows only, because only here is a mount point something worth reading: it is a drive
    // letter, and it is where the user goes to find the device. Elsewhere it is a directory under
    // ~/.filedonkey named after the machine, which says nothing the row has not said already.
    mountLbl = new QLabel(this);
    mountLbl->setObjectName("deviceMount");
    mountLbl->hide();

    detailLine->addWidget(mountLbl);
#endif

    detailLine->addWidget(detailLbl);
    detailLine->addStretch(1);

    QVBoxLayout *textColumn = new QVBoxLayout;
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(3);
    textColumn->addLayout(titleLine);
    textColumn->addLayout(detailLine);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 9, 10, 9);
    layout->setSpacing(11);
    layout->addWidget(badgeLbl, 0, Qt::AlignVCenter);
    layout->addLayout(textColumn, 1);

    // Where every row starts: LocalNode emits peerAdded with the mount already under way, so there
    // is no moment at which a peer is known but nothing is being done about it.
    restyle(dotLbl, "state", "mounting");

    refreshDetail();
    refreshToolTip();
}

void DeviceRow::setMounted(const QString &mountPoint)
{
    mounted = true;
    this->mountPoint = mountPoint;

    // What fuse_mount was handed on Windows is the bare drive, "D:", and to Windows that is a path
    // relative to the current directory on D: rather than the root of it - which is both what the
    // design draws and the only thing worth opening. The separator is what makes it the root.
    if (this->mountPoint.endsWith(':')) this->mountPoint += '\\';

    restyle(dotLbl, "state", "mounted");

    // Only now, so the hand never appears over a row there is nothing behind yet. A row still
    // mounting keeps the ordinary arrow and lets its clicks through to the scroll area.
    setCursor(Qt::PointingHandCursor);

    if (mountLbl)
    {
        mountLbl->setText(this->mountPoint);
        mountLbl->show();
    }

    refreshDetail();
    refreshToolTip();
}

void DeviceRow::mousePressEvent(QMouseEvent *event)
{
    // Taken here or the release never arrives: whoever accepts the press is who Qt sends the rest
    // of the click to, and letting it through would hand both halves to the scroll area behind us.
    // The labels on top of the row need no such handling - a plain QLabel ignores a press, and Qt
    // walks an ignored event up to the parent, which is this.
    if (event->button() == Qt::LeftButton && mounted)
    {
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void DeviceRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !mounted)
    {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    // Only if the pointer is still on the row. Pressing and letting go somewhere else is how a
    // click is taken back, and this one opens a window.
    if (rect().contains(event->position().toPoint()))
    {
        // Whatever the desktop has registered for a directory: Explorer, Finder, or whichever file
        // manager the Linux session installed as the handler.
        QDesktopServices::openUrl(QUrl::fromLocalFile(mountPoint));
    }

    event->accept();
}

void DeviceRow::setUploaded(u64 uploaded)
{
    this->uploaded = uploaded;
    refreshDetail();
}

void DeviceRow::setDownloaded(u64 downloaded)
{
    this->downloaded = downloaded;
    refreshDetail();
}

// Where the peer is until it is mounted, and what it has moved once it is. The counters do not
// appear before the mount because nothing can have crossed it yet, and their arrival is what tells
// the two states apart on the platforms with no mount point to show.
void DeviceRow::refreshDetail()
{
    if (!mounted)
    {
        detailLbl->setText(DeviceList::tr("mounting · %1").arg(conn.machineAddress));
        return;
    }

    QLocale locale(QLocale::English, QLocale::UnitedStates);

    detailLbl->setText(QString("↑ %1   ↓ %2").arg(locale.formattedDataSize(uploaded),
                                                  locale.formattedDataSize(downloaded)));
}

// Everything the row knows, including the parts it has no room to draw - the address it dropped
// once the mount came up, and the mount point itself where that is not shown at all.
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

    // The whole list in one line, for the status bar: the same dot the rows carry, and a count.
    summary = new QWidget(this);
    summary->setObjectName("deviceSummary");

    summaryDot = new QLabel(summary);
    summaryDot->setObjectName("deviceDot");
    summaryDot->setFixedSize(DOT_SIZE, DOT_SIZE);

    summaryLbl = new QLabel(summary);
    summaryLbl->setObjectName("deviceCountLbl");

    QHBoxLayout *summaryLayout = new QHBoxLayout(summary);
    summaryLayout->setContentsMargins(5, 5, 5, 5);
    summaryLayout->setSpacing(7);
    summaryLayout->addWidget(summaryDot, 0, Qt::AlignVCenter);
    summaryLayout->addWidget(summaryLbl);

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
    emptyLayout->setContentsMargins(28, 34, 28, 26);
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

    // The window cannot be resized, so this is what makes the fifth device onwards reachable.
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setObjectName("deviceScroll");
    scroll->setWidget(rowsHost);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(scroll);

    refreshSummary();
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

    refreshSummary();
}

void DeviceList::onPeerMounted(const QString &machineId, const QString &mountPoint)
{
    DeviceRow *row = rows.value(machineId, nullptr);
    if (!row) return;

    row->setMounted(mountPoint);

    refreshSummary();
}

void DeviceList::onPeerUploaded(const QString &machineId, u64 uploaded)
{
    DeviceRow *row = rows.value(machineId, nullptr);
    if (!row) return;

    row->setUploaded(uploaded);
}

void DeviceList::onPeerDownloaded(const QString &machineId, u64 downloaded)
{
    DeviceRow *row = rows.value(machineId, nullptr);
    if (!row) return;

    row->setDownloaded(downloaded);
}

void DeviceList::onPeerRemoved(const QString &machineId)
{
    DeviceRow *row = rows.take(machineId);
    if (!row) return;

    // Not deleteLater(): this arrives on the GUI thread from LocalNode, with nothing of the row's
    // own on the stack.
    delete row;

    refreshSummary();
}

void DeviceList::refreshSummary()
{
    emptyState->setVisible(rows.isEmpty());

    summaryLbl->setText(rows.size() == 1 ? tr("1 device") : tr("%1 devices").arg(rows.size()));

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

    restyle(summaryDot, "state", state);
}
