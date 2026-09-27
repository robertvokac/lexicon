#pragma once

#include "Records.h"

#include <QDialog>

class QPushButton;
class QComboBox;
class QStackedWidget;
class QTextBrowser;
class QTextEdit;

// Named Markdown Boards: choose, add, rename or delete one, read it first,
// then switch to the source/live-preview editor and save without silently
// overwriting a newer edit.
class BoardDialog : public QDialog {
  Q_OBJECT

public:
  explicit BoardDialog(QWidget *parent = nullptr);

public slots:
  void reject() override;

private:
  void load(int preferredId = -1);
  void selectBoard(int index);
  void addBoard();
  void renameBoard();
  void deleteBoard();
  void beginEdit();
  void cancelEdit();
  void closeDialog();
  bool confirmDiscardChanges();
  void save();
  void showBoard();
  void updatePreview();
  void insertMarkdown(const QString &prefix, const QString &suffix = {},
                      const QString &sample = {});

  lexicon::BoardRecord board_;
  std::vector<lexicon::BoardRecord> boards_;
  QComboBox *boardBox_ = nullptr;
  QPushButton *newButton_ = nullptr;
  QPushButton *renameButton_ = nullptr;
  QPushButton *deleteButton_ = nullptr;
  QStackedWidget *pages_ = nullptr;
  QTextBrowser *view_ = nullptr;
  QTextEdit *source_ = nullptr;
  QTextBrowser *preview_ = nullptr;
  QPushButton *editButton_ = nullptr;
  QPushButton *saveButton_ = nullptr;
  QPushButton *cancelButton_ = nullptr;
};
