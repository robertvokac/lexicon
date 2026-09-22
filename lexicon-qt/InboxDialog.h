#pragma once

#include "ApplicationContext.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;

// The Inbox: an idea caught quickly - a title and plain text - saved to the
// Default group without a type, whatever the main window's filters show.
class InboxDialog : public QDialog {
    Q_OBJECT

public:
    explicit InboxDialog(QWidget* parent = nullptr);
    // The saved item, or -1.
    int savedItemId() const { return m_savedItemId; }

private:
    void save();

    QLineEdit* m_title = nullptr;
    QPlainTextEdit* m_content = nullptr;
    QLabel* m_error = nullptr;
    int m_savedItemId = -1;
};
