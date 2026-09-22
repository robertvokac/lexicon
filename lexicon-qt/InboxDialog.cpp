#include "InboxDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

InboxDialog::InboxDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Inbox");
    resize(560, 380);
    auto* root = new QVBoxLayout(this);
    auto* hint = new QLabel("Saved to Default, without a type. Sort it out later.", this);
    hint->setEnabled(false);
    root->addWidget(hint);
    auto* form = new QFormLayout();
    m_title = new QLineEdit(this);
    m_title->setObjectName("inboxTitle");
    m_content = new QPlainTextEdit(this);
    m_content->setObjectName("inboxContent");
    m_content->setPlaceholderText("Plain text");
    m_content->setTabChangesFocus(true);
    form->addRow("Title:", m_title);
    form->addRow("Idea:", m_content);
    root->addLayout(form, 1);
    m_error = new QLabel(this);
    m_error->setObjectName("inboxError");
    m_error->setStyleSheet("color: #b3261e;");
    m_error->setWordWrap(true);
    m_error->hide();
    root->addWidget(m_error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName("inboxSave");
    connect(buttons, &QDialogButtonBox::accepted, this, &InboxDialog::save);
    connect(m_title, &QLineEdit::textChanged, m_error, &QLabel::hide);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
    m_title->setFocus();
}

void InboxDialog::save() {
    const QString title = m_title->text().trimmed();
    const auto fail = [this](const QString& message) {
        m_error->setText(message);
        m_error->show();
    };
    if (title.isEmpty()) {
        fail("Enter a title.");
        m_title->setFocus();
        return;
    }
    QString error;
    const int groupId = services().groups.defaultGroupId(&error);
    if (groupId <= 0) {
        fail(error.isEmpty() ? "Cannot find the Default group." : error);
        return;
    }
    ItemRecord item;
    item.groupId = groupId;
    item.title = title;
    item.content = m_content->toPlainText();
    // Everything typed stays when the save is refused, with the reason.
    const int id = services().items.createItem(item, &error);
    if (id <= 0) {
        fail(error);
        return;
    }
    m_savedItemId = id;
    accept();
}
