#include "devicelist.h"

#include <QDesktopServices>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
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
#define BADGE_SIZE  34
#define BADGE_GLYPH 22
#define DOT_SIZE     6

// How far the status bar's count has to rise to look level with the dot beside it, measured against
// the 11px the stylesheet gives #deviceCountLbl. An optical correction, not a layout one - see where
// it is applied in the DeviceList constructor for why a font makes text centred by its box sit low.
#define DOT_TEXT_LIFT 1

// Between the mount point and each counter on the second line. One number for the whole line, so
// the gap after the mount point is the gap between the counters - they used to be set apart by
// spaces inside a single label, which made one of those two gaps the width of three mono spaces
// and the other whatever the layout said.
#define DETAIL_GAP 18

namespace {

// The badge artwork for a peer, or an empty string where there is none and the row has to name the
// platform in text instead.
//
// Matched on the QSysInfo::productType() the peer broadcast, which names the two desktop platforms
// we have artwork for outright and every Linux distribution as itself - ubuntu, arch, debian, one
// per distribution and no list to check against. So Linux is what is left after the names that are
// known: the two mobile platforms, which have no artwork of their own, and the "unknown" QSysInfo
// answers with when it cannot tell. A BSD would be handed the penguin by that rule, which is a
// better answer than three letters and not one FileDonkey runs on anyway.
QString badgeIcon(const QString &productType)
{
    if (productType == "windows") return ":/assets/windows.svg";
    if (productType == "macos")   return ":/assets/macos.svg";

    if (productType.isEmpty())     return QString();
    if (productType == "unknown")  return QString();
    if (productType == "android")  return QString();
    if (productType == "ios")      return QString();

    return ":/assets/linux.svg";
}

// What the row calls the platform beside the name, from the same QSysInfo::productType() the badge
// is chosen by. The four names with artwork or a maker's spelling of their own are given it; every
// other answer is a Linux distribution naming itself, and a capital is all that needs. Empty for
// the answers that name nothing - a row with room for one word should not spend it on "unknown".
QString osName(const QString &productType)
{
    if (productType == "windows") return "Windows";
    if (productType == "macos")   return "macOS";
    if (productType == "android") return "Android";
    if (productType == "ios")     return "iOS";

    if (productType.isEmpty())    return QString();
    if (productType == "unknown") return QString();

    return productType.left(1).toUpper() + productType.mid(1);
}

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
    QLabel      *dotLbl        = nullptr;
    QLabel      *mountLbl      = nullptr;
    QLabel      *uploadedLbl   = nullptr;
    QLabel      *downloadedLbl = nullptr;
    ElidedLabel *detailLbl     = nullptr;

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

    QLabel *badgeLbl = new QLabel(this);
    badgeLbl->setObjectName("deviceBadge");
    badgeLbl->setAlignment(Qt::AlignCenter);
    badgeLbl->setFixedSize(BADGE_SIZE, BADGE_SIZE);

    const QString icon = badgeIcon(conn.machineOs);
    if (!icon.isEmpty())
    {
        // Through QIcon rather than QPixmap so the SVG is re-rendered at whatever the display's
        // scale factor is, the way the caption glyphs are - see the note in titlebar.cpp. The three
        // files are drawn in the badge's own #A9AEB6 already, so there is nothing to tint.
        badgeLbl->setPixmap(QIcon(icon).pixmap(BADGE_GLYPH, BADGE_GLYPH));
    }
    else
    {
        // Nothing drawn for this platform. The first three letters of what it called itself is what
        // the badge showed everywhere before there was artwork: android -> and, ios -> ios. A peer
        // from before the broadcast carried the field at all says nothing we can use.
        QString text = conn.machineOs.left(3).toLower();
        if (text.isEmpty()) text = "?";

        badgeLbl->setText(text);
    }

    ElidedLabel *nameLbl = new ElidedLabel(conn.machineName, this);
    nameLbl->setObjectName("deviceName");

    dotLbl = new QLabel(this);
    dotLbl->setObjectName("deviceDot");
    dotLbl->setFixedSize(DOT_SIZE, DOT_SIZE);

    // What the second line says before the mount is up. It and the two counters take turns: the
    // counters have nothing to report until there is a mount for bytes to cross.
    detailLbl = new ElidedLabel(this);
    detailLbl->setObjectName("deviceDetail");

    uploadedLbl = new QLabel(this);
    uploadedLbl->setObjectName("deviceUploaded");
    uploadedLbl->hide();

    downloadedLbl = new QLabel(this);
    downloadedLbl->setObjectName("deviceDownloaded");
    downloadedLbl->hide();

    QHBoxLayout *titleLine = new QHBoxLayout;
    titleLine->setContentsMargins(0, 0, 0, 0);
    titleLine->setSpacing(7);
    titleLine->addWidget(nameLbl);

    // Aligned explicitly, or the layout leaves a widget this much shorter than its row sitting at
    // the top of it and the dot rides above the name's cap height.
    titleLine->addWidget(dotLbl, 0, Qt::AlignVCenter);

    // The platform in words, beside the dot. The badge carries it as artwork already, but one glyph
    // stands for every Linux distribution, and this is where a peer says which one it is. Built only
    // when there is a name to draw - see osName() - so nothing takes the gap after the dot when a
    // peer says nothing about itself.
    const QString os = osName(conn.machineOs);
    if (!os.isEmpty())
    {
        QLabel *osLbl = new QLabel(os, this);
        osLbl->setObjectName("deviceOs");

        // Aligned as the dot is, and for the same reason: 11px type in a row sized by 13px would
        // otherwise sit at the top of it.
        titleLine->addWidget(osLbl, 0, Qt::AlignVCenter);
    }

    titleLine->addStretch(1);

    QHBoxLayout *detailLine = new QHBoxLayout;
    detailLine->setContentsMargins(0, 0, 0, 0);
    detailLine->setSpacing(DETAIL_GAP);

#if defined(Q_OS_WIN)
    // Windows only, because only here is a mount point something worth reading: it is a drive
    // letter, and it is where the user goes to find the device. Elsewhere it is a directory under
    // ~/.filedonkey named after the machine, which says nothing the row has not said already.
    mountLbl = new QLabel(this);
    mountLbl->setObjectName("deviceMount");
    mountLbl->hide();

    detailLine->addWidget(mountLbl);
#endif

    // A hidden widget takes neither room nor a gap of its own, so the three that are showing at any
    // one moment sit DETAIL_GAP apart whichever they are.
    detailLine->addWidget(uploadedLbl);
    detailLine->addWidget(downloadedLbl);
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

    detailLbl->hide();
    uploadedLbl->show();
    downloadedLbl->show();

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

    uploadedLbl->setText(QString("↑ %1").arg(locale.formattedDataSize(uploaded)));
    downloadedLbl->setText(QString("↓ %1").arg(locale.formattedDataSize(downloaded)));
}

// Everything the row knows, including the parts it has no room to draw - the address it dropped
// once the mount came up, the mount point itself where that is not shown at all, and the platform
// exactly as it named itself, which is what the label beside the dot is a tidied form of.
void DeviceRow::refreshToolTip()
{
    QString text = conn.machineName;
    if (!conn.machineOs.isEmpty()) text += QString(" · %1").arg(conn.machineOs);

    text += QString("\n\n%1:%2").arg(conn.machineAddress).arg(conn.machinePort);
    if (mounted) text += "\n\n" + mountPoint;

    setToolTip(text);
}

DeviceList::DeviceList(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("deviceList");
    setAttribute(Qt::WA_StyledBackground, true);

    // The whole list in one line, for the status bar: a count, and the same dot the rows carry. The
    // dot follows the words here, unlike on a row, because the line sits at the right-hand end of
    // the bar - the dot is what lands closest to the window's corner and reads as the full stop.
    summary = new QWidget(this);
    summary->setObjectName("deviceSummary");

    summaryDot = new QLabel(summary);
    summaryDot->setObjectName("deviceDot");
    summaryDot->setFixedSize(DOT_SIZE, DOT_SIZE);

    summaryLbl = new QLabel(summary);
    summaryLbl->setObjectName("deviceCountLbl");

    // Centring the two on the bar is not enough to make them look level. A QLabel centres the whole
    // line box, and a font keeps more room above its letters - for accents nothing here uses - than
    // below them for descenders, so text centred by its box always sits a little low. Margin at the
    // bottom takes that room back: the label grows downwards, so centring it puts the letters half
    // the margin higher, which is what brings them level with the dot.
    summaryLbl->setContentsMargins(0, 0, 0, DOT_TEXT_LIFT * 2);

    QHBoxLayout *summaryLayout = new QHBoxLayout(summary);
    summaryLayout->setContentsMargins(5, 5, 5, 5);
    summaryLayout->setSpacing(7);
    summaryLayout->addWidget(summaryLbl, 0, Qt::AlignVCenter);
    summaryLayout->addWidget(summaryDot, 0, Qt::AlignVCenter);
    summaryLayout->insertSpacing(-1, 4);

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

    // The same words the empty state above uses, so the bar and the list say one thing rather than
    // the bar counting zero of something while the list explains there is nothing to count.
    if (rows.isEmpty())        summaryLbl->setText(tr("No devices yet"));
    else if (rows.size() == 1) summaryLbl->setText(tr("1 device"));
    else                       summaryLbl->setText(tr("%1 devices").arg(rows.size()));

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
