#include "devicelist.h"

#include "elidedlabel.h"
#include "manualconnectdialog.h"

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
#include <QPushButton>
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

// The arrow beside each counter, and the little that separates it from its own number. Drawn a
// touch under the 11px the counters are set in: the artwork fills its viewBox top to bottom, so at
// the type's own size it would stand taller than the digits beside it rather than level with them.
#define ARROW_GLYPH 9
#define ARROW_GAP   4

// The strip under the list: the hairline that divides it from the rows, the line saying why anyone
// would connect a device by hand, and the button that opens the dialog for it.
//
// Both numbers are the settings page's, where they are SETTINGS_MARGIN and SETTINGS_SECTION_GAP -
// the inset that page's content keeps from the window's edges, and the room it leaves around the
// rule between its two halves. Repeated here rather than shared because the two pages are laid out
// by different files and neither owns the other's spacing, so a change to either there has to be
// answered here: the two rules are meant to be read as the same line seen on two pages, and they
// only are while they sit the same distance in from the same edges.
#define FOOTER_MARGIN 18
#define FOOTER_SECTION_GAP 12

// Between the line and the button. Wide enough that the two read as separate things - the line is
// a remark, not the button's label.
#define FOOTER_GAP 12

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

// A counter and the arrow that says which way it counts, as one widget - so the gap that sets the
// counters apart falls between the two pairs and not between an arrow and the number it belongs to.
//
// The arrow is artwork rather than a character, drawn through QIcon so it is re-rendered from the
// vector at the display's own scale factor the way the caption glyphs and the platform badges are.
// Its colour is baked into the file, in the counters' own #8D929A: an icon cannot take a colour
// from the stylesheet's `color:` - see the note on the badge in filedonkey.qss.
QWidget *counterBox(QWidget *parent, const QString &icon, QLabel *label)
{
    QWidget *box = new QWidget(parent);

    QLabel *arrowLbl = new QLabel(box);
    arrowLbl->setObjectName("deviceArrow");
    arrowLbl->setPixmap(QIcon(icon).pixmap(ARROW_GLYPH, ARROW_GLYPH));

    QHBoxLayout *layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ARROW_GAP);

    // Centred against the number rather than sitting on its baseline: the glyph is symmetrical and
    // has no baseline of its own to sit on. addWidget() reparents the label into the box for us.
    layout->addWidget(arrowLbl, 0, Qt::AlignVCenter);
    layout->addWidget(label,    0, Qt::AlignVCenter);

    return box;
}

// A stylesheet rule that selects on a property only takes hold once the style has looked at the
// widget again, and setProperty() alone does not ask it to.
void restyle(QWidget *widget, const char *name, const QVariant &value)
{
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

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
    void setFailed(const QString &reason);

    // Back to the state the row is built in. Pressing Retry is what asks for it: the mount starts
    // again at once and the row should say so rather than sitting on the failure it is leaving.
    void setMounting();

    void setUploaded(u64 uploaded);
    void setDownloaded(u64 downloaded);

    bool isMounted() const { return mounted; }
    bool hasFailed() const { return failed; }

    // The list wires this itself - see onPeerAdded, which is where a row is paired with the
    // machine id a retry has to name. Nothing else about the button is anyone's business.
    QPushButton *retryButton() const { return retryBtn; }

    const QString &name() const { return conn.machineName; }

    // Empty until the mount is up, and openable as it stands once it is - setMounted() is where
    // the drive letter is made into a path Windows will open.
    const QString &mount() const { return mountPoint; }

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

    // The arrow and the number together. What the row shows and hides - hiding the number alone
    // would leave its arrow behind on an unmounted row.
    QWidget     *uploadedBox   = nullptr;
    QWidget     *downloadedBox = nullptr;

    // Shown only in the failed state, and the one control a row has ever carried.
    QPushButton *retryBtn = nullptr;

    QString mountPoint;
    bool    mounted    = false;

    // The mount was tried and did not come up. Never true at the same time as mounted: nothing
    // reports a failure once the mount is up, and setMounted is the only thing that says it is.
    bool    failed     = false;

    // What to put on the second line while that is so, exactly as LocalNode phrased it.
    QString failureReason;

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

    downloadedLbl = new QLabel(this);
    downloadedLbl->setObjectName("deviceDownloaded");

    uploadedBox   = counterBox(this, ":/assets/arrow-up.svg",   uploadedLbl);
    downloadedBox = counterBox(this, ":/assets/arrow-down.svg", downloadedLbl);

    uploadedBox->hide();
    downloadedBox->hide();

    // Only ever seen on a row whose mount failed. On the first line rather than the second, where
    // the reason for the failure is and needs the whole width it can get.
    retryBtn = new QPushButton(DeviceList::tr("Retry"), this);
    retryBtn->setObjectName("deviceRetryBtn");
    retryBtn->setCursor(Qt::PointingHandCursor);

    // As the footer's own button is: nothing in this window takes focus by tabbing, and a focus
    // ring on one row's button would be the only one in the window.
    retryBtn->setFocusPolicy(Qt::NoFocus);

    retryBtn->hide();

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

    // After the stretch, so it sits at the right-hand end of the row and the name keeps its place
    // whether the button is there or not.
    titleLine->addWidget(retryBtn, 0, Qt::AlignVCenter);

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
    detailLine->addWidget(uploadedBox);
    detailLine->addWidget(downloadedBox);
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
    uploadedBox->show();
    downloadedBox->show();

    refreshDetail();
    refreshToolTip();
}

// The mount could not be brought up, and nothing is going to try again until the button below is
// pressed. The second line spends itself on why, which is the only thing worth saying here - and
// the counters go, having nothing to count.
void DeviceRow::setFailed(const QString &reason)
{
    failed        = true;
    mounted       = false;
    failureReason = reason;

    restyle(dotLbl,    "state", "failed");
    restyle(detailLbl, "state", "failed");

    // There is nothing behind the row now, so the hand a mounted row shows would be a promise it
    // cannot keep. The button is the only thing on it left to press.
    setCursor(Qt::ArrowCursor);

    if (mountLbl) mountLbl->hide();

    uploadedBox->hide();
    downloadedBox->hide();

    detailLbl->show();
    retryBtn->show();

    refreshDetail();
    refreshToolTip();
}

void DeviceRow::setMounting()
{
    failed = false;
    failureReason.clear();

    restyle(dotLbl, "state", "mounting");

    // Cleared rather than set to anything: an invalid QVariant takes the property off, which is
    // what puts the label back under the plain rule it is styled by the rest of the time.
    restyle(detailLbl, "state", QVariant());

    retryBtn->hide();

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
        // What says the row has taken the click, since the stylesheet cannot say it: Qt sets the
        // :pressed pseudo-state for the button classes and never for a plain QWidget, so the row
        // carries the state as a property the stylesheet selects on instead.
        restyle(this, "pressed", true);

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

    // Whether the click is taken or taken back, the row is no longer held. Qt keeps the mouse with
    // whoever accepted the press, so this arrives here even when the pointer has been dragged off
    // the row - which is the case the state would otherwise be left set in.
    restyle(this, "pressed", false);

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
    if (failed)
    {
        detailLbl->setText(failureReason);
        return;
    }

    if (!mounted)
    {
        detailLbl->setText(DeviceList::tr("mounting · %1").arg(conn.machineAddress));
        return;
    }

    QLocale locale(QLocale::English, QLocale::UnitedStates);

    // Just the number: which way it went is the arrow's job now - see counterBox().
    uploadedLbl->setText(locale.formattedDataSize(uploaded));
    downloadedLbl->setText(locale.formattedDataSize(downloaded));
}

// Everything the row knows, including the parts it has no room to draw - the address it dropped
// once the mount came up, the mount point itself where that is not shown at all, and the platform
// exactly as it named itself, which is what the label beside the dot is a tidied form of.
void DeviceRow::refreshToolTip()
{
    QString text = conn.machineName;
    if (!conn.machineOs.isEmpty()) text += QString(" · %1").arg(osName(conn.machineOs));

    text += QString("\n\n%1 : %2").arg(conn.machineAddress).arg(conn.machinePort);

    // The second line elides, and a reason is the one thing a row shows that is a sentence rather
    // than a label - so this is where it can be read in full.
    if (failed) text += QString("\n\n%1").arg(failureReason);

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
    emptyLayout->setContentsMargins(28, 64, 28, 26);
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

    // Under the list, and outside the scroll area on purpose: it is not one of the devices and must
    // not scroll away with them - a user who cannot find a device is exactly the user who has
    // scrolled to the bottom looking for it.
    //
    // The line beside the button says why rather than what: the button already says what pressing it
    // does, and the only thing it cannot say is that a silent list may be the network's doing rather
    // than a device that is switched off. Elided rather than wrapped, so a longer translation takes
    // the room it has and the footer stays one line tall - the window cannot grow to fit it.
    ElidedLabel *footerHint = new ElidedLabel(tr("Some networks block automatic discovery."), this);
    footerHint->setObjectName("deviceFooterHint");
    footerHint->setToolTip(footerHint->text());

    QPushButton *manualBtn = new QPushButton(tr("Connect by IP…"), this);
    manualBtn->setObjectName("manualConnectBtn");
    manualBtn->setCursor(Qt::PointingHandCursor);

    // As the settings page's Choose button is: nothing in this window takes focus by tabbing, and a
    // focus ring on the one button down here would be the only one in the window.
    manualBtn->setFocusPolicy(Qt::NoFocus);

    connect(manualBtn, &QPushButton::clicked, this, &DeviceList::openManualConnect);

    // The hint and the button on one line, so the rule above them can be a row of its own and still
    // span the width the two of them do.
    QWidget *footerLine = new QWidget(this);

    QHBoxLayout *footerLineLayout = new QHBoxLayout(footerLine);
    footerLineLayout->setContentsMargins(0, 0, 0, 0);
    footerLineLayout->setSpacing(FOOTER_GAP);

    // The hint takes what the button leaves, which is what puts the button in the corner and lets
    // the line elide instead of pushing it out of the window.
    footerLineLayout->addWidget(footerHint, 1, Qt::AlignVCenter);
    footerLineLayout->addWidget(manualBtn, 0, Qt::AlignVCenter);

    // A widget in the layout rather than a border on the footer, which is what insets it: a border
    // runs the full width of whatever carries it, edge to edge of the window, and that reads as a
    // second window frame rather than as a rule between two parts of one page. Built the way the
    // settings page builds its own - see separatorLine() there - down to sharing the stylesheet
    // rule that colours them.
    QWidget *footerSeparator = new QWidget(this);
    footerSeparator->setObjectName("deviceFooterSeparator");
    footerSeparator->setAttribute(Qt::WA_StyledBackground, true);
    footerSeparator->setFixedHeight(1);

    QWidget *footer = new QWidget(this);
    footer->setObjectName("deviceFooter");
    footer->setAttribute(Qt::WA_StyledBackground, true);

    // The rule, then the line of controls, with the same room around the rule that the settings
    // page leaves around its own and the same inset from the window's edges.
    QVBoxLayout *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(FOOTER_MARGIN, FOOTER_SECTION_GAP,
                                     FOOTER_MARGIN, FOOTER_SECTION_GAP);
    footerLayout->setSpacing(FOOTER_SECTION_GAP);
    footerLayout->addWidget(footerSeparator);
    footerLayout->addWidget(footerLine);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(scroll);
    layout->addWidget(footer);

    refreshSummary();
}

void DeviceList::openManualConnect()
{
    ManualConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;

    // Straight out again. Whether anything is at that address takes a moment to find out, and this
    // list is not where the finding out happens - see LocalNode::connectManually(), and the window
    // for what it does with a failure.
    emit manualConnectRequested(dialog.address(), dialog.port());
}

void DeviceList::onPeerAdded(const Connection &conn)
{
    // LocalNode ignores a machine it already has, so this is a peer we have never seen - unless a
    // row was left behind by a peerRemoved that never came, and then this would show it twice.
    if (rows.contains(conn.machineId)) return;

    DeviceRow *row = new DeviceRow(conn);
    rows.insert(conn.machineId, row);

    // The row knows nothing of machine ids past the one it was built from, and the node knows
    // nothing else, so the pairing is made here where both are in hand. The row goes back to
    // mounting on the spot rather than waiting to be told: the mount really has started again,
    // and leaving the failure up until something else says otherwise reads as a button that did
    // nothing.
    connect(row->retryButton(), &QPushButton::clicked, this, [this, row, id = conn.machineId]() {
        row->setMounting();
        refreshSummary();

        emit retryRequested(id);
    });

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

    // Off the row rather than off the argument: setMounted() is where a bare drive letter is made
    // into a path, and the notification should say the same thing the row does.
    emit deviceMounted(row->name(), row->mount());
}

void DeviceList::onPeerMountFailed(const QString &machineId, const QString &reason)
{
    DeviceRow *row = rows.value(machineId, nullptr);
    if (!row) return;

    row->setFailed(reason);

    refreshSummary();

    // Off the row rather than off the id, as the pair above are: a notification has to name the
    // machine the way the window does.
    emit deviceMountFailed(row->name(), reason);
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

    // Copied out while the row still exists - the name is a reference into the row's own connection
    // and the row is about to go. Whether it was mounted is read here for the same reason.
    const QString name      = row->name();
    const bool    wasMounted = row->isMounted();

    // Not deleteLater(): this arrives on the GUI thread from LocalNode, with nothing of the row's
    // own on the stack.
    delete row;

    refreshSummary();

    // Only for a mount that was actually up. A peer whose mount never came up goes the same way as
    // one that was unmounted, and announcing a drive that was never there would be a lie.
    if (wasMounted) emit deviceUnmounted(name);
}

QList<DeviceList::Device> DeviceList::devices() const
{
    QList<Device> found;

    // Walked over the layout rather than over the map, which is keyed by machine id and so hands
    // its rows back in an order nothing on screen is in. Only the rows and the empty state are
    // widgets in here; the stretch at the end has no widget of its own.
    for (int i = 0; i < rowsLayout->count(); ++i)
    {
        QWidget *widget = rowsLayout->itemAt(i)->widget();
        if (!widget || widget == emptyState) continue;

        const DeviceRow *row = static_cast<const DeviceRow *>(widget);
        found.append(Device{ row->name(), row->mount(), row->hasFailed() });
    }

    return found;
}

void DeviceList::refreshSummary()
{
    emptyState->setVisible(rows.isEmpty());

    // The same words the empty state above uses, so the bar and the list say one thing rather than
    // the bar counting zero of something while the list explains there is nothing to count.
    if (rows.isEmpty())        summaryLbl->setText(tr("No devices yet"));
    else if (rows.size() == 1) summaryLbl->setText(tr("1 device"));
    else                       summaryLbl->setText(tr("%1 devices").arg(rows.size()));

    // Green as soon as one mount is up, amber while any of them is still coming up, red when all
    // that is left is failures, and the default grey of the stylesheet's dot rule when there is
    // nothing to report. In that order because it is the best news that is worth reporting: one
    // device failing while another is mounted is the row's business, not the whole list's.
    bool anyMounted  = false;
    bool anyMounting = false;
    bool anyFailed   = false;

    for (const DeviceRow *row : std::as_const(rows))
    {
        if (row->isMounted())      anyMounted  = true;
        else if (row->hasFailed()) anyFailed   = true;
        else                       anyMounting = true;
    }

    QString state;
    if (anyMounted)       state = "mounted";
    else if (anyMounting) state = "mounting";
    else if (anyFailed)   state = "failed";

    restyle(summaryDot, "state", state);
}
