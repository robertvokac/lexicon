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
    auto* hint = new QLabel("Saved to Default, with the type Inbox. Sort it out later.", this);
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
    // Default, and the Inbox type - made the first time - in one step.
    auto saved = services().core.inbox.capture(qtbridge::toCore(title), qtbridge::toCore(m_content->toPlainText()));
    // Everything typed stays when the save is refused, with the reason.
    if (!saved) {
        fail(qtbridge::toQt(saved.error().message));
        return;
    }
    m_savedItemId = saved->id;
    accept();
}
