#include "settingspage.h"

#include "localnode.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSysInfo>
#include <QVBoxLayout>

// The page's inset from the window's edges, and what sets a label apart from the field beside it.
// The rows below the first one that this project has yet to grow line up under it by themselves -
// that is what the form layout is here for.
#define SETTINGS_MARGIN 18
#define SETTINGS_LABEL_GAP 14
#define SETTINGS_ROW_GAP 12

// A name has to fit in a row of a window that cannot be resized, at both ends of the connection.
// Past this it is elided there anyway - see ElidedLabel in devicelist.cpp - so the field is the
// better place to say so, while it can still be seen what is being cut.
#define MACHINE_NAME_MAX 63

SettingsPage::SettingsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("settingsPage");

    QLabel *nameLbl = new QLabel(tr("This PC's name"), this);
    nameLbl->setObjectName("settingsLabel");

    nameEdit = new QLineEdit(LocalNode::machineName(), this);
    nameEdit->setObjectName("settingsInput");
    nameEdit->setMaxLength(MACHINE_NAME_MAX);

    // The field always holds the name peers are actually given, so the placeholder is only seen
    // while the field is being emptied - which is exactly when it is worth seeing: it is the name
    // that will be announced instead, in front of the user before they leave the field.
    nameEdit->setPlaceholderText(QSysInfo::machineHostName());

    // What puts the label's text on the same line as the field's. The field is the taller of the
    // two - its stylesheet border and padding are worth 14px that a label has none of - and neither
    // the form layout's vertical alignment nor its default puts a short label on the line the text
    // beside it sits on: left alone the label's baseline lands 2px above the field's, and asking
    // for AlignVCenter moves it a further pixel the wrong way. Giving the label the field's own
    // height instead makes the two boxes identical, and each centres its one line of text in its
    // box the same way, so the baselines coincide by construction.
    //
    // Read off the field rather than written down here, so that a change to the padding in the
    // stylesheet is followed rather than having to be answered with a number in this file.
    nameLbl->setFixedHeight(nameEdit->sizeHint().height());

    // Return, and the field losing focus. hideEvent() covers the ways of leaving it that do
    // neither.
    connect(nameEdit, &QLineEdit::editingFinished, this, &SettingsPage::commitMachineName);

    QFormLayout *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(SETTINGS_LABEL_GAP);
    form->setVerticalSpacing(SETTINGS_ROW_GAP);

    // The field takes what is left of the row rather than the label being stretched to meet it: the
    // labels are a column of their own on the left, which is what makes a form of the rows.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->addRow(nameLbl, nameEdit);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(SETTINGS_MARGIN, SETTINGS_MARGIN, SETTINGS_MARGIN, SETTINGS_MARGIN);
    layout->setSpacing(0);
    layout->addLayout(form);

    // Holds the rows at the top of the page. Without it the form would be spread down a page with
    // one row on it.
    layout->addStretch(1);
}

void SettingsPage::hideEvent(QHideEvent *event)
{
    commitMachineName();

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
