#include "settingspage.h"

#include "autostart.h"
#include "elidedlabel.h"
#include "localnode.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

// The page's inset from the window's edges, what sets a label apart from the field beside it, and
// what sets one row apart from the next. The rows line up under each other by themselves - that is
// what the form layout is here for.
#define SETTINGS_MARGIN 18
#define SETTINGS_LABEL_GAP 14
#define SETTINGS_ROW_GAP 12

// Between a field and the note under it. Shorter than the gap between two rows, which is what makes
// the note read as part of the field above it rather than as a row of its own.
#define SETTINGS_HINT_GAP 6

// A name has to fit in a row of a window that cannot be resized, at both ends of the connection.
// Past this it is elided there anyway - see ElidedLabel in devicelist.cpp - so the field is the
// better place to say so, while it can still be seen what is being cut.
#define MACHINE_NAME_MAX 63

// Five digits and the field is full: a port is never longer, and a field the width of the one above
// it would say a port could be. The note beside it takes the rest of the row.
#define PORT_FIELD_WIDTH 64

// The third column: the button that puts a field back to its default. Square, and a touch smaller
// than the field it sits beside so it reads as something offered rather than as a second control of
// the same weight. The glyph is drawn under the arrow's own ends at this size, which is why it is
// not the full button.
#define REVERT_BUTTON_SIZE 22
#define REVERT_GLYPH       14

// Between a field and the button at the end of its row, and only where nothing else separates them:
// the port's row already has a note and the room left over between the two.
#define REVERT_GAP 8

// Around the rule that separates the fields above it from the switches below. The two halves are
// answered differently - one is typed into and committed, the other takes effect the moment it is
// pressed - and the rule is what says so before either is touched.
#define SETTINGS_SECTION_GAP 16

// How far the note under a switch is indented, so that it starts under the switch's own label
// rather than under the switch: the width of the pill, and the gap the stylesheet puts after it.
#define SWITCH_TEXT_INDENT 46

// Between a switch and the note under it. Tighter than under a field, because there is no border to
// hold the two apart and the label above is only one line of type.
#define SWITCH_NOTE_GAP 3

// What the field will accept, which is the range LocalNode stores - it is the one that decides, and
// these two numbers are here so that a port outside it is refused as it is typed rather than taken,
// rejected, and silently replaced with the default.
#define PORT_LOWEST  1024
#define PORT_HIGHEST 65535

namespace {

// The muted line that says something about the field it sits under or beside.
QLabel *noteLabel(QWidget *parent, const QString &text)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName("settingsHint");

    return label;
}

// A row's label, made the height of the field it names. That is what puts the two texts on one
// line: the field is the taller of the two - its stylesheet border and padding are worth more than
// a label has - and neither the form layout's vertical alignment nor its default puts a short label
// on the line the text beside it sits on. Left alone the label's baseline lands 2px above the
// field's, and asking for AlignVCenter moves it a further pixel the wrong way. Matched boxes centre
// their one line of text the same way, so the baselines coincide by construction.
//
// The height comes off the field rather than being written down here, so that a change to the
// padding in the stylesheet is followed rather than having to be answered with a number.
QLabel *rowLabel(QWidget *parent, const QString &text, const QWidget *field)
{
    QLabel *label = new QLabel(text, parent);
    label->setObjectName("settingsLabel");
    label->setFixedHeight(field->sizeHint().height());

    return label;
}

// The button at the end of a row, which turns a changed field back into the one this app ships
// with. Hidden until there is something to undo - bindRevert() is what shows and hides it.
//
// Its place in the row is kept while it is hidden. Without that the field beside it would grow and
// shrink by the button's width as the button came and went, and a name being typed would jump about
// under the cursor on the first character that differed from the host name.
QToolButton *revertButton(QWidget *parent, const QString &tooltip)
{
    QToolButton *button = new QToolButton(parent);
    button->setObjectName("settingsRevert");
    button->setIcon(QIcon(":/assets/reverse.svg"));
    button->setIconSize(QSize(REVERT_GLYPH, REVERT_GLYPH));
    button->setFixedSize(REVERT_BUTTON_SIZE, REVERT_BUTTON_SIZE);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(tooltip);
    button->hide();

    QSizePolicy policy = button->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    button->setSizePolicy(policy);

    return button;
}

} // namespace

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("settingsPage");

    nameEdit = new QLineEdit(LocalNode::machineName(), this);
    nameEdit->setObjectName("settingsInput");
    nameEdit->setMaxLength(MACHINE_NAME_MAX);

    // The field always holds the name peers are actually given, so the placeholder is only seen
    // while the field is being emptied - which is exactly when it is worth seeing: it is the name
    // that will be announced instead, in front of the user before they leave the field.
    nameEdit->setPlaceholderText(LocalNode::defaultMachineName());

    // The transfer port and no discovery port beside it, deliberately. A peer is told which port to
    // dial - every announcement carries it - so this machine can move it on its own and the rest
    // follow. Moving the discovery port is the opposite: it is the port a broadcast is sent to as
    // well as the one it is heard on, so a machine that moved it alone would announce itself into a
    // port nobody listens on and find nothing, which on this page looks exactly like a network with
    // no other machine running FileDonkey. It stays settable by hand for the network that has to
    // move it everywhere at once - see the note in localnode.h.
    transferEdit = new QLineEdit(QString::number(LocalNode::transferPort()), this);
    transferEdit->setObjectName("settingsInput");
    transferEdit->setFixedWidth(PORT_FIELD_WIDTH);
    transferEdit->setValidator(new QIntValidator(PORT_LOWEST, PORT_HIGHEST, transferEdit));

    // Return, and a field losing focus. hideEvent() covers the ways of leaving one that do neither.
    connect(nameEdit, &QLineEdit::editingFinished, this, &SettingsPage::commitMachineName);

    connect(transferEdit, &QLineEdit::editingFinished, this, [this]() {
        commitPort(transferEdit, &LocalNode::setTransferPort, &LocalNode::transferPort);
    });

    // The third column. Each button names the value it would bring back rather than saying
    // "default": the number is the whole answer to what pressing it does, and it is a number the
    // user has by then replaced and cannot read anywhere else on the page.
    const QString defaultName = LocalNode::defaultMachineName();
    const QString defaultPort = QString::number(LocalNode::defaultTransferPort());

    QToolButton *nameRevert     = revertButton(this, tr("Back to %1").arg(defaultName));
    QToolButton *transferRevert = revertButton(this, tr("Back to %1").arg(defaultPort));

    bindRevert(nameEdit, nameRevert, defaultName, [this]() {
        commitMachineName();
    });

    bindRevert(transferEdit, transferRevert, defaultPort, [this]() {
        commitPort(transferEdit, &LocalNode::setTransferPort, &LocalNode::transferPort);
    });

    // The shared root's own, which cannot go through bindRevert(): there is no field to read, and
    // nothing to watch for a change - the path only ever moves through showSharedRoot(), which is
    // what shows and hides this.
    sharedRootRevert = revertButton(this, tr("Back to %1").arg(LocalNode::defaultSharedRoot()));

    connect(sharedRootRevert, &QToolButton::clicked, this, [this]() {
        showSharedRoot(LocalNode::defaultSharedRoot());
    });

    // Said here rather than left to be found out. A peer writes a machine's name down once, when it
    // first hears from it, and shows that copy from then on - so a rename takes effect on the next
    // announcement but reaches a device already connected only when the connection is made again.
    // Without this the field looks like it did nothing: the machine that was renamed is the one
    // place the new name cannot be seen.
    QLabel *nameHint = noteLabel(this, tr("Devices already connected keep the old name until they "
                                          "connect again."));
    nameHint->setWordWrap(true);

    // The shared root's, and the one line on this page that says what the whole application does
    // with this machine's disk. The last sentence is ours rather than the design's: the backend is
    // handed the folder when it is built and serves that one until it is built again, so a folder
    // chosen here changes nothing a peer can see until then.
    QLabel *sharedRootHint = noteLabel(this, tr("The folder this PC exposes to other devices. "
                                                "Everything else stays invisible. Served from the "
                                                "next time the app starts."));
    sharedRootHint->setWordWrap(true);

    // The same for the field below it: the server is listening on the old port until the
    // application is started again, and nothing on this page would otherwise say so.
    QLabel *portHint = noteLabel(this, tr("The new port is taken up the next time the app "
                                          "starts."));
    portHint->setWordWrap(true);

    // The field and its button, on one line. The field takes what the button leaves - it is the
    // only thing here with a use for the width - so the buttons on the two rows end up under each
    // other, which is what makes them a column rather than two loose buttons.
    QWidget *nameLine = new QWidget(this);

    QHBoxLayout *nameLineLayout = new QHBoxLayout(nameLine);
    nameLineLayout->setContentsMargins(0, 0, 0, 0);
    nameLineLayout->setSpacing(REVERT_GAP);
    nameLineLayout->addWidget(nameEdit, 1);
    nameLineLayout->addWidget(nameRevert, 0, Qt::AlignVCenter);

    // The shared root's line: the folder, the button that changes it, and the one that puts it back.
    // The path is shown rather than typed - it is not something a user should have to spell - and it
    // is the one thing on the line with a use for the width, so it takes what the two buttons leave.
    sharedRootLbl = new ElidedLabel(this);
    sharedRootLbl->setObjectName("settingsPath");
    sharedRootLbl->setFixedHeight(nameEdit->sizeHint().height());

    QPushButton *chooseBtn = new QPushButton(tr("Choose…"), this);
    chooseBtn->setObjectName("settingsChooseBtn");
    chooseBtn->setCursor(Qt::PointingHandCursor);
    chooseBtn->setFocusPolicy(Qt::NoFocus);

    connect(chooseBtn, &QPushButton::clicked, this, &SettingsPage::chooseSharedRoot);

    QWidget *sharedRootLine = new QWidget(this);

    QHBoxLayout *sharedRootLineLayout = new QHBoxLayout(sharedRootLine);
    sharedRootLineLayout->setContentsMargins(0, 0, 0, 0);
    sharedRootLineLayout->setSpacing(REVERT_GAP);
    sharedRootLineLayout->addWidget(sharedRootLbl, 1);
    sharedRootLineLayout->addWidget(chooseBtn, 0, Qt::AlignVCenter);
    sharedRootLineLayout->addWidget(sharedRootRevert, 0, Qt::AlignVCenter);

    // Fills the line above, and decides whether the button at the end of it is there to be seen.
    showSharedRoot(LocalNode::sharedRoot());

    // The port's line: a field as wide as a port, the note saying which protocol it is, and the
    // room left over pushing the button out to the same end of the row the one above it is at.
    QLabel *protocolLbl = noteLabel(this, tr("TCP"));
    protocolLbl->setFixedHeight(transferEdit->sizeHint().height());

    QWidget *transferLine = new QWidget(this);

    QHBoxLayout *transferLineLayout = new QHBoxLayout(transferLine);
    transferLineLayout->setContentsMargins(0, 0, 0, 0);
    transferLineLayout->setSpacing(SETTINGS_LABEL_GAP);
    transferLineLayout->addWidget(transferEdit);
    transferLineLayout->addWidget(protocolLbl);
    transferLineLayout->addStretch(1);
    transferLineLayout->addWidget(transferRevert, 0, Qt::AlignVCenter);

    // A line and the note under it as one cell of the form, so the note sits under the field rather
    // than under the label beside it, and so the two are set apart by a gap of their own rather than
    // by the one the form puts between rows.
    QWidget *nameField = new QWidget(this);

    QVBoxLayout *nameColumn = new QVBoxLayout(nameField);
    nameColumn->setContentsMargins(0, 0, 0, 0);
    nameColumn->setSpacing(SETTINGS_HINT_GAP);
    nameColumn->addWidget(nameLine);
    nameColumn->addWidget(nameHint);

    QWidget *sharedRootField = new QWidget(this);

    QVBoxLayout *sharedRootColumn = new QVBoxLayout(sharedRootField);
    sharedRootColumn->setContentsMargins(0, 0, 0, 0);
    sharedRootColumn->setSpacing(SETTINGS_HINT_GAP);
    sharedRootColumn->addWidget(sharedRootLine);
    sharedRootColumn->addWidget(sharedRootHint);

    QWidget *transferField = new QWidget(this);

    QVBoxLayout *transferColumn = new QVBoxLayout(transferField);
    transferColumn->setContentsMargins(0, 0, 0, 0);
    transferColumn->setSpacing(SETTINGS_HINT_GAP);
    transferColumn->addWidget(transferLine);
    transferColumn->addWidget(portHint);

    // The rule under the form. A plain widget rather than a QFrame HLine, the way the status bar's
    // separator is one: a frame's line is drawn by the native style in the style's own colours, and
    // this one is the hairline grey the window's own borders use.
    QWidget *separator = new QWidget(this);
    separator->setObjectName("settingsSeparator");
    separator->setAttribute(Qt::WA_StyledBackground, true);
    separator->setFixedHeight(1);

    // What is under it answers at once rather than being typed and committed - there is nothing to
    // finish typing, and a switch that waited for the focus to move would be a switch that had not
    // done what it just showed.
    autostartBox = new QCheckBox(tr("Start FileDonkey when I sign in"), this);
    autostartBox->setObjectName("settingsSwitch");
    autostartBox->setCursor(Qt::PointingHandCursor);
    autostartBox->setChecked(Autostart::isEnabled());

    QLabel *autostartNote = noteLabel(this, tr("Runs in the tray, no window"));
    autostartNote->setContentsMargins(SWITCH_TEXT_INDENT, 0, 0, 0);

    connect(autostartBox, &QCheckBox::toggled, this, [this](bool enabled) {
        Autostart::setEnabled(enabled);

        // Read back, so the switch shows what the desktop actually has rather than what it was
        // asked for: writing the entry can fail, and a switch left sitting on a request nobody took
        // would have the user believe FileDonkey starts itself when it does not. Blocked, or setting
        // it here would come straight back through this lambda.
        const QSignalBlocker blocker(autostartBox);
        autostartBox->setChecked(Autostart::isEnabled());
    });

    QFormLayout *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(SETTINGS_LABEL_GAP);
    form->setVerticalSpacing(SETTINGS_ROW_GAP);

    // The field takes what is left of the row rather than the label being stretched to meet it: the
    // labels are a column of their own on the left, which is what makes a form of the rows.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    form->addRow(rowLabel(this, tr("This PC's name"), nameEdit), nameField);
    form->addRow(rowLabel(this, tr("Shared root"), sharedRootLbl), sharedRootField);
    form->addRow(rowLabel(this, tr("Transfer port"), transferEdit), transferField);

    // Everything the page shows, on one widget of its own. It is taller than the window as of the
    // shared root's three-line note, and the window cannot be resized - so it goes in a scroll area
    // rather than being squeezed, which is what the device list does with its rows for exactly the
    // same reason. The page's own margins live here, inside the scrolling part, so the last row is
    // clear of the status bar when it is scrolled to.
    QWidget *content = new QWidget(this);
    content->setObjectName("settingsContent");

    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(SETTINGS_MARGIN, SETTINGS_MARGIN, SETTINGS_MARGIN, SETTINGS_MARGIN);
    contentLayout->setSpacing(0);
    contentLayout->addLayout(form);

    contentLayout->addSpacing(SETTINGS_SECTION_GAP);
    contentLayout->addWidget(separator);
    contentLayout->addSpacing(SETTINGS_SECTION_GAP);

    contentLayout->addWidget(autostartBox);
    contentLayout->addSpacing(SWITCH_NOTE_GAP);
    contentLayout->addWidget(autostartNote);

    // Holds everything at the top of the page on a window with room to spare. Without it the rows
    // would be spread down it.
    contentLayout->addStretch(1);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setObjectName("settingsScroll");
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(scroll);
}

void SettingsPage::hideEvent(QHideEvent *event)
{
    commitMachineName();
    commitPort(transferEdit, &LocalNode::setTransferPort, &LocalNode::transferPort);

    QWidget::hideEvent(event);
}

void SettingsPage::commitMachineName()
{
    LocalNode::setMachineName(nameEdit->text());

    // Reads it back rather than assuming what was stored: setMachineName() trims what it is given
    // and keeps nothing at all for an empty name, and this is what puts the host name it then falls
    // back to in front of the user.
    nameEdit->setText(LocalNode::machineName());
}

void SettingsPage::chooseSharedRoot()
{
    // Opened on the folder that is shared now, so the dialog starts where the user last left this
    // rather than wherever the platform would have picked.
    const QString chosen = QFileDialog::getExistingDirectory(
        this, tr("Choose the folder FileDonkey shares"), LocalNode::sharedRoot(),
        QFileDialog::ShowDirsOnly);

    // Empty is the dialog being dismissed, which is not a choice of the root directory of the disk
    // or of anything else - the row keeps what it had.
    if (chosen.isEmpty()) return;

    showSharedRoot(chosen);
}

void SettingsPage::showSharedRoot(const QString &path)
{
    LocalNode::setSharedRoot(path);

    // Read back rather than shown as chosen, the way a committed field is: setSharedRoot() keeps
    // nothing for the folder this app would have picked anyway, and this is what then puts that
    // folder in the row instead of an empty line.
    const QString shared = LocalNode::sharedRoot();

    // In the separators of the platform it will be read on. QFileDialog answers in Qt's own, which
    // are forward slashes everywhere - a Windows user reading C:/Users/... beside the drive letters
    // the device list shows would be the odd one out.
    sharedRootLbl->setText(QDir::toNativeSeparators(shared));

    // The whole path, for when the row has not the width to draw it - see ElidedLabel.
    sharedRootLbl->setToolTip(QDir::toNativeSeparators(shared));

    sharedRootRevert->setVisible(shared != LocalNode::defaultSharedRoot());
}

void SettingsPage::bindRevert(QLineEdit *edit, QToolButton *revert, const QString &defaultText,
                              std::function<void()> commit)
{
    // On every change rather than on the ones the user typed: setText() is how a field is put back
    // and how a commit reads a stored value in, and the button has to answer those too or it would
    // be left offering to undo what is already undone.
    connect(edit, &QLineEdit::textChanged, revert, [revert, defaultText](const QString &text) {
        revert->setVisible(text.trimmed() != defaultText);
    });

    connect(revert, &QToolButton::clicked, edit, [edit, defaultText, commit]() {
        edit->setText(defaultText);

        // Stored as well as shown. Pressing this is the user being done with the field - there is
        // nothing left to type - so it does not wait for the focus to move on the way every other
        // change here does.
        commit();
    });

    // What the field arrived holding is a change like any other: a name set in a previous session
    // is still not the default one, and the button has to be there for it.
    revert->setVisible(edit->text().trimmed() != defaultText);
}

void SettingsPage::commitPort(QLineEdit *edit, void (*store)(int), int (*read)())
{
    // Half a port is not a port. The validator lets one be typed a digit at a time, which means the
    // field can be left holding something it would never have accepted whole - and being left is
    // exactly when this runs.
    if (edit->hasAcceptableInput()) store(edit->text().toInt());

    // Read back either way: what was stored may not be what was typed, and what was refused has to
    // be replaced with the port that is actually in force rather than left standing.
    edit->setText(QString::number(read()));
}
