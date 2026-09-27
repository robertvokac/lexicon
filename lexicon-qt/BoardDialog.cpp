#include "BoardDialog.h"

#include "ApplicationContext.h"
#include "MarkdownConverter.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

BoardDialog::BoardDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Board");
  resize(1000, 700);

  auto *layout = new QVBoxLayout(this);
  auto *boards = new QHBoxLayout();
  boardBox_ = new QComboBox(this);
  boardBox_->setObjectName("boardSelector");
  boardBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  newButton_ = new QPushButton("New", this);
  renameButton_ = new QPushButton("Rename", this);
  deleteButton_ = new QPushButton("Delete", this);
  boards->addWidget(boardBox_, 1);
  boards->addWidget(newButton_);
  boards->addWidget(renameButton_);
  boards->addWidget(deleteButton_);
  layout->addLayout(boards);
  pages_ = new QStackedWidget(this);

  view_ = new QTextBrowser(pages_);
  view_->setObjectName("boardView");
  view_->setOpenExternalLinks(false);
  connect(view_, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
    if (url.scheme() != MarkdownConverter::kItemScheme)
      QDesktopServices::openUrl(url);
  });
  pages_->addWidget(view_);

  auto *editor = new QWidget(pages_);
  auto *editorLayout = new QVBoxLayout(editor);
  auto *toolbar = new QToolBar(editor);
  toolbar->setIconSize(QSize(16, 16));
  const auto action = [&](const QString &label, const QString &tip,
                          const QString &prefix, const QString &suffix = {},
                          const QString &sample = {}) {
    auto *created = toolbar->addAction(label);
    created->setToolTip(tip);
    connect(created, &QAction::triggered, this, [this, prefix, suffix, sample] {
      insertMarkdown(prefix, suffix, sample);
    });
  };
  action("B", "Bold (**)", "**", "**", "bold text");
  action("I", "Italic (*)", "*", "*", "italic text");
  toolbar->addSeparator();
  action("H2", "Header 2 (##)", "\n## ", {}, "Header 2");
  action("H3", "Header 3 (###)", "\n### ", {}, "Header 3");
  action("H4", "Header 4 (####)", "\n#### ", {}, "Header 4");
  toolbar->addSeparator();
  action("List", "Unordered list (-)", "\n- ", {}, "list item");
  action("1.", "Ordered list (1.)", "\n1. ", {}, "list item");
  action("\"", "Quote (>)", "\n> ", {}, "quote");
  action("---", "Horizontal line", "\n---\n");
  toolbar->addSeparator();
  action("Code", "Inline code (`)", "`", "`", "code");
  auto *block = toolbar->addAction("Block");
  block->setToolTip("Code block (```)");
  connect(block, &QAction::triggered, this, [this] {
    const QString language = QInputDialog::getText(
        this, "Code block", "Language (e.g. cpp, python, sql):");
    insertMarkdown("\n```" + language.trimmed() + "\n", "\n```\n",
                   "code block");
  });
  toolbar->addSeparator();
  action("Link", "Insert link ([])", "[", "](https://)", "link text");
  auto *itemLink = toolbar->addAction("[[ ]]");
  itemLink->setToolTip("Link to an item ([[Title]])");
  connect(itemLink, &QAction::triggered, this, [this] {
    const QString selected = source_->textCursor().selectedText().trimmed();
    const QString target = QInputDialog::getText(this, "Link to an item", "Item:",
                                                 QLineEdit::Normal, selected);
    if (target.trimmed().isEmpty())
      return;
    const QString text = "[[" + target.trimmed() +
                         (!selected.isEmpty() && selected != target.trimmed()
                              ? "|" + selected
                              : QString()) +
                         "]]";
    source_->textCursor().insertText(text);
    source_->setFocus();
  });
  action("Table", "Insert table (|)",
         "\n| Header 1 | Header 2 |\n| --- | --- |\n| Cell 1 | Cell 2 |\n");

  source_ = new QTextEdit(editor);
  source_->setObjectName("boardSource");
  source_->setAcceptRichText(false);
  source_->setPlaceholderText("Markdown content...");
  preview_ = new QTextBrowser(editor);
  preview_->setObjectName("boardPreview");
  preview_->setOpenExternalLinks(true);
  auto *splitter = new QSplitter(editor);
  splitter->addWidget(source_);
  splitter->addWidget(preview_);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  editorLayout->addWidget(toolbar);
  editorLayout->addWidget(splitter, 1);
  pages_->addWidget(editor);
  layout->addWidget(pages_, 1);

  auto *buttons = new QHBoxLayout();
  buttons->addStretch();
  editButton_ = new QPushButton("Edit", this);
  saveButton_ = new QPushButton("Save", this);
  cancelButton_ = new QPushButton("Cancel", this);
  auto *close = new QPushButton("Close", this);
  buttons->addWidget(editButton_);
  buttons->addWidget(saveButton_);
  buttons->addWidget(cancelButton_);
  buttons->addWidget(close);
  layout->addLayout(buttons);
  connect(editButton_, &QPushButton::clicked, this, &BoardDialog::beginEdit);
  connect(saveButton_, &QPushButton::clicked, this, &BoardDialog::save);
  connect(cancelButton_, &QPushButton::clicked, this, &BoardDialog::cancelEdit);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  connect(boardBox_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &BoardDialog::selectBoard);
  connect(newButton_, &QPushButton::clicked, this, &BoardDialog::addBoard);
  connect(renameButton_, &QPushButton::clicked, this,
          &BoardDialog::renameBoard);
  connect(deleteButton_, &QPushButton::clicked, this,
          &BoardDialog::deleteBoard);

  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  timer->setInterval(300);
  connect(source_, &QTextEdit::textChanged, timer,
          QOverload<>::of(&QTimer::start));
  connect(timer, &QTimer::timeout, this, &BoardDialog::updatePreview);
  load();
}

void BoardDialog::load(int preferredId) {
  auto loaded = services().core.board.loadAll();
  if (!loaded) {
    QMessageBox::critical(this, "Board",
                          qtbridge::toQt(loaded.error().message));
    return;
  }
  if (loaded->empty()) {
    auto created = services().core.board.create({-1, "Main", {}, 0});
    if (!created) {
      QMessageBox::critical(this, "Board",
                            qtbridge::toQt(created.error().message));
      return;
    }
    loaded->push_back(std::move(*created));
  }
  boards_ = std::move(*loaded);
  const QSignalBlocker blocker(boardBox_);
  boardBox_->clear();
  int selected = 0;
  for (int index = 0; index < static_cast<int>(boards_.size()); ++index) {
    boardBox_->addItem(qtbridge::toQt(boards_[index].name), boards_[index].id);
    if (boards_[index].id == preferredId)
      selected = index;
  }
  boardBox_->setCurrentIndex(selected);
  selectBoard(selected);
}

void BoardDialog::selectBoard(int index) {
  if (index < 0 || index >= static_cast<int>(boards_.size()))
    return;
  board_ = boards_[index];
  showBoard();
}

void BoardDialog::addBoard() {
  bool accepted = false;
  const QString suggested = boards_.empty() ? "Main" : "New Board";
  const QString name = QInputDialog::getText(
      this, "New Board", "Name:", QLineEdit::Normal, suggested, &accepted);
  if (!accepted)
    return;
  auto created = services().core.board.create(
      {-1, qtbridge::toCore(name), {}, 0});
  if (!created) {
    QMessageBox::critical(this, "Board",
                          qtbridge::toQt(created.error().message));
    return;
  }
  load(created->id);
}

void BoardDialog::renameBoard() {
  bool accepted = false;
  const QString name = QInputDialog::getText(
      this, "Rename Board", "Name:", QLineEdit::Normal,
      qtbridge::toQt(board_.name), &accepted);
  if (!accepted)
    return;
  auto changed = board_;
  changed.name = qtbridge::toCore(name);
  auto stored = services().core.board.save(changed);
  if (!stored) {
    QMessageBox::critical(this, "Board",
                          qtbridge::toQt(stored.error().message));
    return;
  }
  load(stored->id);
}

void BoardDialog::deleteBoard() {
  if (boards_.size() <= 1) {
    QMessageBox::information(this, "Board", "At least one Board must remain.");
    return;
  }
  if (QMessageBox::question(
          this, "Delete Board",
          QString("Delete Board '%1'? Its Markdown content cannot be restored.")
              .arg(qtbridge::toQt(board_.name))) != QMessageBox::Yes)
    return;
  auto removed = services().core.board.remove(board_.id);
  if (!removed) {
    QMessageBox::critical(this, "Board",
                          qtbridge::toQt(removed.error().message));
    return;
  }
  load();
}

void BoardDialog::showBoard() {
  setWindowTitle("Board — " + qtbridge::toQt(board_.name));
  view_->setHtml(board_.content.empty()
                     ? QString("<p><i>The Board is empty.</i></p>")
                     : MarkdownConverter::toHtml(qtbridge::toQt(board_.content)));
  pages_->setCurrentIndex(0);
  editButton_->show();
  saveButton_->hide();
  cancelButton_->hide();
  boardBox_->setEnabled(true);
  newButton_->setEnabled(true);
  renameButton_->setEnabled(true);
  deleteButton_->setEnabled(boards_.size() > 1);
}

void BoardDialog::beginEdit() {
  source_->setPlainText(qtbridge::toQt(board_.content));
  updatePreview();
  pages_->setCurrentIndex(1);
  editButton_->hide();
  saveButton_->show();
  cancelButton_->show();
  boardBox_->setEnabled(false);
  newButton_->setEnabled(false);
  renameButton_->setEnabled(false);
  deleteButton_->setEnabled(false);
  source_->setFocus();
}

void BoardDialog::cancelEdit() { showBoard(); }

void BoardDialog::save() {
  auto changed = board_;
  changed.content = qtbridge::toCore(source_->toPlainText());
  auto stored = services().core.board.save(changed);
  if (!stored && stored.error().code == lexicon::Error::Code::Conflict) {
    QMessageBox conflict(this);
    conflict.setWindowTitle("Board changed elsewhere");
    conflict.setText("The Board was changed elsewhere after you opened it.");
    conflict.setInformativeText(
        "Reload discards your changes. Overwrite replaces the newer Board.");
    auto *reload = conflict.addButton("Reload", QMessageBox::AcceptRole);
    auto *overwrite = conflict.addButton("Overwrite", QMessageBox::DestructiveRole);
    conflict.addButton("Keep editing", QMessageBox::RejectRole);
    conflict.exec();
    if (conflict.clickedButton() == reload) {
      load(board_.id);
      return;
    }
    if (conflict.clickedButton() == overwrite) {
      changed.revision = 0;
      stored = services().core.board.save(changed);
    } else {
      return;
    }
  }
  if (!stored) {
    QMessageBox::critical(this, "Board",
                          qtbridge::toQt(stored.error().message));
    return;
  }
  board_ = std::move(*stored);
  for (auto &candidate : boards_)
    if (candidate.id == board_.id)
      candidate = board_;
  showBoard();
}

void BoardDialog::updatePreview() {
  preview_->setHtml(MarkdownConverter::toHtml(source_->toPlainText()));
}

void BoardDialog::insertMarkdown(const QString &prefix, const QString &suffix,
                                 const QString &sample) {
  auto cursor = source_->textCursor();
  const QString selected = cursor.selectedText();
  const QString inserted = selected.isEmpty() ? sample : selected;
  cursor.insertText(prefix + inserted + suffix);
  if (selected.isEmpty() && !inserted.isEmpty()) {
    cursor.setPosition(cursor.position() - suffix.size() - inserted.size());
    cursor.setPosition(cursor.position() + inserted.size(),
                       QTextCursor::KeepAnchor);
    source_->setTextCursor(cursor);
  }
  source_->setFocus();
}
