#include "BoardDialog.h"

#include "ApplicationContext.h"
#include "MarkdownConverter.h"

#include <QAction>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
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

  auto *timer = new QTimer(this);
  timer->setSingleShot(true);
  timer->setInterval(300);
  connect(source_, &QTextEdit::textChanged, timer,
          QOverload<>::of(&QTimer::start));
  connect(timer, &QTimer::timeout, this, &BoardDialog::updatePreview);
  load();
}

void BoardDialog::load() {
  auto loaded = services().core.board.load();
  if (!loaded) {
    QMessageBox::critical(this, "Board",
                          qtbridge::toQt(loaded.error().message));
    return;
  }
  board_ = std::move(*loaded);
  showBoard();
}

void BoardDialog::showBoard() {
  view_->setHtml(board_.content.empty()
                     ? QString("<p><i>The Board is empty.</i></p>")
                     : MarkdownConverter::toHtml(qtbridge::toQt(board_.content)));
  pages_->setCurrentIndex(0);
  editButton_->show();
  saveButton_->hide();
  cancelButton_->hide();
}

void BoardDialog::beginEdit() {
  source_->setPlainText(qtbridge::toQt(board_.content));
  updatePreview();
  pages_->setCurrentIndex(1);
  editButton_->hide();
  saveButton_->show();
  cancelButton_->show();
  source_->setFocus();
}

void BoardDialog::cancelEdit() { showBoard(); }

void BoardDialog::save() {
  lexicon::BoardRecord changed{qtbridge::toCore(source_->toPlainText()),
                               board_.revision};
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
      load();
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
