#include "manualconnectdialog.h"

#include "localnode.h"
#include "revertbutton.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

// The address box: one field per octet, the dots between them, and the room the box keeps around
// the lot. The octet is wide enough for three digits of the 12px mono the fields are set in with a
// little to spare, and it is a number rather than a measurement of the font for the same reason
// every other size in this project is - see the note at the top of devicelist.cpp.
#define OCTET_WIDTH 28
#define IP_EDIT_PADDING 6
#define IP_DOT_WIDTH 7

// The dialog's own inset, and what separates its three parts: the paragraph that says why anyone
// would be here, the two fields, and the buttons.
#define DIALOG_MARGIN 18
#define DIALOG_SECTION_GAP 16
#define DIALOG_LABEL_GAP 14
#define DIALOG_ROW_GAP 12

// The bar the two buttons sit on, which is a surface of its own rather than part of the page above
// it - see the rule on #dialogButtonBar. It is inset from the sides by the same margin the content
// is, so the buttons line up with what they act on, and it keeps less room above and below them
// than the page keeps around its rows: the bar is as tall as what is on it and no taller.
#define DIALOG_BAR_PADDING 14

// Between Cancel and Connect. Tight, because they are one pair - wider and they would read as two
// unrelated buttons that happen to share an edge.
#define DIALOG_BUTTON_GAP 8

// Fixed, and narrow enough that the paragraph at the top breaks into a few short lines rather than
// one long one. Nothing in here has a use for extra width - the address box is a fixed size and the
// port is four digits.
#define DIALOG_WIDTH 380

// What the port field will accept. The same range LocalNode stores a port in, because a peer cannot
// be listening outside it: a port it refused to store is a port it never bound.
#define PORT_LOWEST  1024
#define PORT_HIGHEST 65535

// Five digits and the field is full - the same width the settings page gives its port.
#define PORT_FIELD_WIDTH 64

namespace {

// A stylesheet rule that selects on a property only takes hold once the style has looked at the
// widget again, and setProperty() alone does not ask it to. The device list keeps its own copy of
// this for its rows; five lines is less to carry than a header shared between the two.
void restyle(QWidget *widget, const char *name, const QVariant &value)
{
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

// What one of the four fields will take. A plain QIntValidator(0, 255) says the same thing about a
// number and is what this was, but it made the box impossible to paste an address into: a validator
// refuses the whole insertion, so "192.168.1.5" arriving in a field was rejected outright and the
// textEdited that would have spread it across the four never came. This lets a dotted run stand for
// the instant it takes IPv4Edit::spread() to deal it out, and nothing else.
//
// It is also what limits the length, in place of the setMaxLength(3) that used to: a maximum length
// truncates a paste rather than refusing it, so the same address arrived as "192" and the rest of it
// was silently dropped. Four digits are refused here because no number of them can be under 256,
// which is what a length of three said less directly.
class OctetValidator : public QValidator
{
public:
    using QValidator::QValidator;

    State validate(QString &input, int &) const override
    {
        // Not a rejection: it is a field on the way to being filled, and Invalid here would mean a
        // field could never be cleared and retyped.
        if (input.isEmpty()) return Intermediate;

        for (const QChar character : input)
        {
            if (!character.isDigit() && character != '.') return Invalid;
        }

        if (input.contains('.')) return Acceptable;

        // ok is false for a run of digits too long to be an int, which is the one way past the
        // range test below.
        bool ok = false;
        const int value = input.toInt(&ok);

        return (ok && value <= 255) ? Acceptable : Invalid;
    }
};

} // namespace

IPv4Edit::IPv4Edit(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("ipEdit");

    // A QWidget subclass draws no stylesheet background unless it is asked to, and the box's fill
    // and border are the whole point of it being a widget rather than a bare layout.
    setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(IP_EDIT_PADDING, 0, IP_EDIT_PADDING, 0);
    layout->setSpacing(0);

    for (int i = 0; i < 4; ++i)
    {
        QLineEdit *octet = new QLineEdit(this);
        octet->setObjectName("ipOctet");
        octet->setAlignment(Qt::AlignCenter);
        octet->setFixedWidth(OCTET_WIDTH);

        // An impossible octet cannot be typed rather than being typed and rejected afterwards: a
        // validator refuses the keystroke itself, so 2 and 5 are taken and the 6 that would make
        // 256 never lands. See OctetValidator for the two things it does that a plain
        // QIntValidator(0, 255) does not.
        octet->setValidator(new OctetValidator(octet));

        // Where every one of the behaviours below is caught: the keys that move between fields, and
        // the focus changes that draw the box around them as one.
        octet->installEventFilter(this);

        connect(octet, &QLineEdit::textEdited, this, [this, i](const QString &text) {
            // An address pasted - or typed - with dots in it belongs across the fields rather than
            // in this one. Nothing else to do afterwards: spread() fills them and reports it.
            if (spread(i, text)) return;

            // Full, or as full as it can get: 26 takes no third digit, since 260 is past 255 and
            // the validator would refuse it. Either way there is nothing more to type here, and
            // moving on is what saves a dot per octet.
            if (text.size() == 3 || text.toInt() * 10 > 255) focusNext(i);

            emit addressChanged();
        });

        octets[i] = octet;

        layout->addWidget(octet);

        // Three dots between four fields.
        if (i == 3) continue;

        QLabel *dot = new QLabel(".", this);
        dot->setObjectName("ipDot");
        dot->setAlignment(Qt::AlignCenter);
        dot->setFixedWidth(IP_DOT_WIDTH);

        layout->addWidget(dot);
    }
}

QString IPv4Edit::address() const
{
    QStringList parts;

    for (const QLineEdit *octet : octets)
    {
        // Not an address until all four are there. Treating a blank as a zero would dial an address
        // the user never typed.
        if (octet->text().isEmpty()) return QString();

        parts.append(octet->text());
    }

    return parts.join('.');
}

void IPv4Edit::setPrefixFrom(const QString &address)
{
    bool isIPv4 = false;
    const quint32 ipv4 = QHostAddress(address).toIPv4Address(&isIPv4);

    // No address to borrow from - a machine on IPv6 only, or one that could not say where it is.
    // The four fields stay empty and the user types the whole thing.
    if (!isIPv4) return;

    // The three that name the network this machine is on. The fourth is the machine within it, and
    // it is the one number that cannot be guessed from ours.
    defaults[0] = QString::number((ipv4 >> 24) & 0xFF);
    defaults[1] = QString::number((ipv4 >> 16) & 0xFF);
    defaults[2] = QString::number((ipv4 >>  8) & 0xFF);

    for (int i = 0; i < 4; ++i) octets[i]->setText(defaults[i]);

    emit addressChanged();
}

bool IPv4Edit::isDefault() const
{
    for (int i = 0; i < 4; ++i)
    {
        if (octets[i]->text() != defaults[i]) return false;
    }

    return true;
}

void IPv4Edit::revert()
{
    for (int i = 0; i < 4; ++i) octets[i]->setText(defaults[i]);

    emit addressChanged();

    // Where the dialog opened it. Pressing revert is meant to leave the box as it was found, and
    // the cursor sitting in the fourth octet is part of how it was found.
    focusFirstEmpty();
}

QString IPv4Edit::defaultText() const
{
    // The box opened blank - there was no address to borrow a network from, and there is nothing to
    // put back or to name. Tested on the first field because the three are filled together.
    if (defaults[0].isEmpty()) return QString();

    QStringList parts;
    for (const QString &part : defaults) parts.append(part);

    // The empty fourth is what leaves the trailing dot: "192.168.50."
    return parts.join('.');
}

void IPv4Edit::focusFirstEmpty()
{
    for (QLineEdit *octet : octets)
    {
        if (!octet->text().isEmpty()) continue;

        octet->setFocus();
        return;
    }

    octets[3]->setFocus();
    octets[3]->selectAll();
}

int IPv4Edit::indexOf(QObject *field) const
{
    for (int i = 0; i < 4; ++i)
    {
        if (octets[i] == field) return i;
    }

    return -1;
}

void IPv4Edit::focusPrevious(int index)
{
    if (index <= 0) return;

    QLineEdit *previous = octets[index - 1];
    previous->setFocus();

    // At the end of what is there, not over it: this is reached by backspacing or arrowing off the
    // front of the field after it, and both mean "carry on from where that one ends".
    previous->deselect();
    previous->setCursorPosition(previous->text().size());
}

void IPv4Edit::focusNext(int index)
{
    if (index >= 3) return;

    QLineEdit *next = octets[index + 1];
    next->setFocus();

    // Selected rather than appended to, so that arriving in a field that already has a number in it
    // and typing replaces it. Coming here from a full field is how the fourth octet gets retyped
    // after the prefix has been filled in.
    next->selectAll();
}

bool IPv4Edit::spread(int index, const QString &text)
{
    if (!text.contains('.')) return false;

    const QStringList parts = text.split('.');

    int last = index;
    for (int i = 0; i < parts.size() && index + i < 4; ++i)
    {
        QLineEdit *octet = octets[index + i];

        // Through the validator rather than around it: a paste can hold anything, and setText()
        // does not check. Emptied rather than left alone where a part is no octet - including the
        // field this arrived in, which is still holding the whole pasted address and must not be
        // left holding it.
        QString part = parts[i];
        int position = 0;

        const bool usable = octet->validator()->validate(part, position) == QValidator::Acceptable
                            && !part.contains('.');

        octet->setText(usable ? part : QString());

        if (usable) last = index + i;
    }

    // Where the typing carries on: after the last field the paste actually filled, or in that field
    // when it was the fourth. A trailing dot - "192.168.1." - leaves an empty part behind, and this
    // is what lands the cursor in the field it was meant to open.
    if (last < 3)
    {
        focusNext(last);
    }
    else
    {
        octets[3]->setFocus();
        octets[3]->selectAll();
    }

    emit addressChanged();

    return true;
}

void IPv4Edit::refreshFocused()
{
    bool focused = false;
    for (const QLineEdit *octet : octets)
    {
        focused = focused || octet->hasFocus();
    }

    restyle(this, "focused", focused);
}

bool IPv4Edit::eventFilter(QObject *watched, QEvent *event)
{
    const int index = indexOf(watched);
    if (index < 0) return QWidget::eventFilter(watched, event);

    QLineEdit *octet = octets[index];

    switch (event->type())
    {
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        {
            // Queued, both of them: during a FocusOut the field the focus is moving to has not got
            // it yet, so asking now would draw the box unfocused on the way between two of its own
            // fields and focused again a moment later.
            QMetaObject::invokeMethod(this, [this]() { refreshFocused(); }, Qt::QueuedConnection);

            // Arriving in a field selects what is in it, so typing replaces rather than appends -
            // which is what the Windows control does, and what makes the prefilled fourth octet
            // worth prefilling. Queued because a click sets the cursor after the focus lands, and
            // would otherwise undo this.
            if (event->type() == QEvent::FocusIn)
            {
                QTimer::singleShot(0, octet, &QLineEdit::selectAll);
            }

            break;
        }

        case QEvent::KeyPress:
        {
            QKeyEvent *key = static_cast<QKeyEvent *>(event);

            switch (key->key())
            {
                // The separator, typed where it is drawn. Both spellings: the key beside the digits
                // and the one on the numeric keypad, which some layouts send as a comma.
                //
                // It moves on only from a field the user has actually put something in. A field
                // still holding all of what it arrived with - empty, or with every character of it
                // selected - is one nothing has been typed into yet, and moving on from that is
                // what made a whole address unusable: "127" fills the first field and carries the
                // cursor into the second by itself, and the dot that follows it in "127.0.0.1"
                // would then skip that second field rather than separate the two numbers.
                case Qt::Key_Period:
                case Qt::Key_Comma:
                {
                    const bool untouched = octet->text().isEmpty()
                                           || octet->selectedText() == octet->text();

                    if (!untouched) focusNext(index);
                    return true;
                }

                // Off the front of a field and back to the end of the one before, rather than doing
                // nothing at all - which is what a backspace with nothing in front of it would
                // otherwise mean, and it is how a mistyped octet two fields back is reached.
                case Qt::Key_Backspace:
                    if (octet->cursorPosition() > 0 || octet->hasSelectedText()) break;

                    focusPrevious(index);
                    return true;

                case Qt::Key_Left:
                    if (octet->cursorPosition() > 0 || octet->hasSelectedText()) break;

                    focusPrevious(index);
                    return true;

                case Qt::Key_Right:
                    if (octet->cursorPosition() < octet->text().size() || octet->hasSelectedText()) break;

                    focusNext(index);
                    return true;

                default:
                    break;
            }

            break;
        }

        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}

ManualConnectDialog::ManualConnectDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("manualConnectDialog");
    setWindowTitle(tr("Connect to a Device"));

    // The native frame is kept, the way the close dialog keeps it - see the note at the end of
    // filedonkey.qss. What is dropped is the question mark beside it, which opens nothing.
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setFixedWidth(DIALOG_WIDTH);

    // Why anyone is reading this dialog at all. The last sentence is the useful half: the address
    // being asked for is the one the other machine shows in its own status bar, so there is
    // somewhere to read it off rather than a network setting to go hunting through.
    QLabel *intro = new QLabel(tr("FileDonkey finds devices by announcing itself over UDP. Some "
                                  "networks do not carry that, and then nothing is found on its "
                                  "own.\n\nType the other device's address instead - it is shown "
                                  "at the bottom of the FileDonkey window running there."), this);
    intro->setObjectName("dialogIntro");
    intro->setWordWrap(true);

    ipEdit = new IPv4Edit(this);

    // The three numbers this machine's own address starts with, which are the three the other
    // machine's almost certainly starts with too. The fourth is left empty and is where the cursor
    // opens - see the end of this constructor.
    ipEdit->setPrefixFrom(LocalNode::localAddress());

    // This machine's transfer port, not the number this app ships with: the two machines are a pair
    // of the same application, so a user who has moved the port here has almost certainly moved it
    // there as well. Where nothing has been moved the two are the same number anyway.
    defaultPort = QString::number(LocalNode::transferPort());

    portEdit = new QLineEdit(defaultPort, this);
    portEdit->setObjectName("dialogInput");
    portEdit->setFixedWidth(PORT_FIELD_WIDTH);
    portEdit->setValidator(new QIntValidator(PORT_LOWEST, PORT_HIGHEST, portEdit));

    // Which protocol the number names, in the same muted note the settings page puts beside its own
    // port field. It is worth saying here: the port that would be typed from memory is the UDP one
    // in the heading above, and this is the other.
    QLabel *protocolLbl = new QLabel(tr("TCP"), this);
    protocolLbl->setObjectName("dialogNote");

    connectBtn = new QPushButton(tr("Connect"), this);
    connectBtn->setObjectName("dialogConnectBtn");
    connectBtn->setDefault(true);

    QPushButton *cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName("dialogCancelBtn");

    // The hand every other button in this application answers the pointer with - the footer's, the
    // settings page's Choose, the revert buttons. Unlike those, these two keep their focus policy:
    // they are a dialog's buttons, and Tab reaching them is how a dialog is meant to be used.
    //
    // Connect's is not set here but in refreshControls(), which is where it is turned on and off
    // with the button: Qt shows a widget's cursor from the pointer's position alone and asks nothing
    // about whether the widget is enabled, so a hand set once would go on inviting a click at a
    // button that opens on refusing them.
    cancelBtn->setCursor(Qt::PointingHandCursor);

    // The third column, as on the settings page: one button per row, offering to put that row back
    // to what the dialog opened holding. Each names the value it would bring back rather than saying
    // "default" - it is the whole answer to what pressing it does, and by then it is a value the
    // user has replaced and cannot read anywhere else.
    //
    // The address has no such value to name where this machine could not say what network it is on:
    // the box opened blank, and what the button offers there is to empty it again.
    const QString defaultAddress = ipEdit->defaultText();

    ipRevert = revertButton(this, defaultAddress.isEmpty() ? tr("Clear the address")
                                                           : tr("Back to %1").arg(defaultAddress));

    portRevert = revertButton(this, tr("Back to %1").arg(defaultPort));

    connect(connectBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);

    connect(ipRevert, &QToolButton::clicked, ipEdit, &IPv4Edit::revert);

    // Nothing to commit on the way back, unlike the settings page's: this dialog holds what is typed
    // into it until Connect is pressed and stores none of it, so putting the field back is the whole
    // of what this button does.
    connect(portRevert, &QToolButton::clicked, this, [this]() {
        portEdit->setText(defaultPort);
    });

    connect(ipEdit,   &IPv4Edit::addressChanged, this, &ManualConnectDialog::refreshControls);
    connect(portEdit, &QLineEdit::textChanged,   this, &ManualConnectDialog::refreshControls);

    QWidget *portLine = new QWidget(this);

    QHBoxLayout *portLineLayout = new QHBoxLayout(portLine);
    portLineLayout->setContentsMargins(0, 0, 0, 0);
    portLineLayout->setSpacing(DIALOG_LABEL_GAP);
    portLineLayout->addWidget(portEdit);
    portLineLayout->addWidget(protocolLbl);
    portLineLayout->addStretch(1);
    portLineLayout->addWidget(portRevert, 0, Qt::AlignVCenter);

    // The address box is a fixed width and would otherwise be stretched across the row by the form
    // layout. Held to the left in a line of its own, so the two fields start at the same place.
    QWidget *ipLine = new QWidget(this);

    QHBoxLayout *ipLineLayout = new QHBoxLayout(ipLine);
    ipLineLayout->setContentsMargins(0, 0, 0, 0);
    ipLineLayout->setSpacing(0);
    ipLineLayout->addWidget(ipEdit);

    // The room between the field and the button is the stretch's, not a gap of its own: both rows
    // end in one, so the two buttons land against the same edge and read as a column.
    ipLineLayout->addStretch(1);
    ipLineLayout->addWidget(ipRevert, 0, Qt::AlignVCenter);

    QLabel *ipLbl   = new QLabel(tr("IP address"), this);
    QLabel *portLbl = new QLabel(tr("Transfer port"), this);

    ipLbl->setObjectName("dialogLabel");
    portLbl->setObjectName("dialogLabel");

    // Both labels made the height of the field beside them, which is what puts the two texts on one
    // line - the same trick the settings page's rowLabel() plays, and for the same reason.
    ipLbl->setFixedHeight(ipEdit->sizeHint().height());
    portLbl->setFixedHeight(portEdit->sizeHint().height());

    QFormLayout *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(DIALOG_LABEL_GAP);
    form->setVerticalSpacing(DIALOG_ROW_GAP);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(ipLbl, ipLine);
    form->addRow(portLbl, portLine);

    // The two buttons and the strip they sit on, which is a widget rather than a bare layout because
    // it carries a fill and a border of its own - the same pair the window's status bar carries, and
    // it closes this dialog the way that bar closes the window.
    //
    // It spans the whole width, edge to edge, which is why the dialog's own layout below keeps no
    // margins: a bar inset from the sides would read as a panel floating in the dialog rather than
    // as its floor. The inset the buttons need is the bar's own, and it is the margin the page above
    // keeps, so they end under the fields they act on.
    QWidget *buttons = new QWidget(this);
    buttons->setObjectName("dialogButtonBar");
    buttons->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *buttonsLayout = new QHBoxLayout(buttons);
    buttonsLayout->setContentsMargins(DIALOG_MARGIN, DIALOG_BAR_PADDING,
                                      DIALOG_MARGIN, DIALOG_BAR_PADDING);
    buttonsLayout->setSpacing(DIALOG_BUTTON_GAP);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(cancelBtn);
    buttonsLayout->addWidget(connectBtn);

    QWidget *separator = new QWidget(this);
    separator->setObjectName("settingsSeparator");
    separator->setAttribute(Qt::WA_StyledBackground, true);
    separator->setFixedHeight(1);

    // Everything above the bar, on a widget of its own so that it and not the dialog carries the
    // margins - the bar has to reach the dialog's three lower edges, and a margin on the dialog
    // would hold it off all of them.
    QWidget *content = new QWidget(this);
    content->setObjectName("dialogContent");

    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(DIALOG_MARGIN, DIALOG_MARGIN, DIALOG_MARGIN, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(intro);
    contentLayout->addSpacing(DIALOG_SECTION_GAP);
    contentLayout->addWidget(separator);
    contentLayout->addSpacing(DIALOG_SECTION_GAP);
    contentLayout->addLayout(form);
    contentLayout->addSpacing(DIALOG_SECTION_GAP);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(content);
    layout->addWidget(buttons);

    refreshControls();

    // Last, after every field has been filled: the cursor opens on the one number the dialog could
    // not fill in for the user, which is the fourth octet.
    ipEdit->focusFirstEmpty();
}

QString ManualConnectDialog::address() const
{
    return ipEdit->address();
}

int ManualConnectDialog::port() const
{
    return portEdit->text().toInt();
}

void ManualConnectDialog::refreshControls()
{
    // An empty address is a partial one - see IPv4Edit::address(). The port is checked through the
    // validator rather than by its own text, since the validator lets a port be typed a digit at a
    // time and the field can hold something it would never have accepted whole.
    const bool ready = !ipEdit->address().isEmpty() && portEdit->hasAcceptableInput();

    connectBtn->setEnabled(ready);

    // With the button rather than once at the start - see the note where Cancel's is set. A disabled
    // widget still shows whatever cursor it was given, so the hand has to come and go with what the
    // button will actually do about a click.
    connectBtn->setCursor(ready ? Qt::PointingHandCursor : Qt::ArrowCursor);

    // Each button is there only while its own field holds something else, so a dialog nobody has
    // touched yet carries neither - there is nothing to put back. Typing the fourth octet brings the
    // address's up, which is right: it is the field's state that has moved, and the button is what
    // offers to move it back.
    ipRevert->setVisible(!ipEdit->isDefault());
    portRevert->setVisible(portEdit->text() != defaultPort);
}
