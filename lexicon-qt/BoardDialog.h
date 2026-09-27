#pragma once

#include "Records.h"

#include <QDialog>

class QPushButton;
class QStackedWidget;
class QTextBrowser;
class QTextEdit;

// The singleton Markdown Board: read it first, switch to the same source and
// live-preview editor used for item content, then save without silently
// overwriting a newer edit.
class BoardDialog : public QDialog {
  Q_OBJECT

public:
  explicit BoardDialog(QWidget *parent = nullptr);

private:
  void load();
  void beginEdit();
  void cancelEdit();
  void save();
  void showBoard();
  void updatePreview();
  void insertMarkdown(const QString &prefix, const QString &suffix = {},
                      const QString &sample = {});

  lexicon::BoardRecord board_;
  QStackedWidget *pages_ = nullptr;
  QTextBrowser *view_ = nullptr;
  QTextEdit *source_ = nullptr;
  QTextBrowser *preview_ = nullptr;
  QPushButton *editButton_ = nullptr;
  QPushButton *saveButton_ = nullptr;
  QPushButton *cancelButton_ = nullptr;
};
