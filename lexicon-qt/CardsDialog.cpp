#include "CardsDialog.h"

#include "CardQuizDialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
// A table cell shows the text on one line; the tooltip has all of it.
QTableWidgetItem* textCell(const std::string& text) {
    const QString full = qtbridge::toQt(text);
    auto* cell = new QTableWidgetItem(QString(full).replace('\n', ' '));
    cell->setToolTip(full);
    return cell;
}

QTableWidgetItem* numberCell(std::int64_t number) {
    auto* cell = new QTableWidgetItem(QString::number(number));
    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return cell;
}
} // namespace

QString lastAttemptText(const std::string& utc) {
    if (utc.empty()) return "Never";
    const QDateTime when = QDateTime::fromString(qtbridge::toQt(utc), Qt::ISODate).toLocalTime();
    return when.isValid() ? QLocale().toString(when, "yyyy-MM-dd HH:mm") : qtbridge::toQt(utc);
}

CardEditDialog::CardEditDialog(int itemId, const lexicon::CardRecord* existing, QWidget* parent)
    : QDialog(parent), m_itemId(itemId), m_cardId(existing ? existing->id : -1) {
    setWindowTitle(existing ? "Edit card" : "Add card");
    resize(600, 460);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_question = new QPlainTextEdit(this);
    m_question->setObjectName("cardQuestion");
    m_question->setTabChangesFocus(true);
    m_question->setPlaceholderText("What do you want to be asked?");
    m_answer = new QPlainTextEdit(this);
    m_answer->setObjectName("cardAnswer");
    m_answer->setTabChangesFocus(true);
    form->addRow("Question:", m_question);
    form->addRow("Answer:", m_answer);
    if (existing) {
        m_question->setPlainText(qtbridge::toQt(existing->question));
        m_answer->setPlainText(qtbridge::toQt(existing->answer));
        // Counted by the quiz; not for a person to change.
        auto* statistics = new QLabel(QString("Known %1 time(s), not known %2 time(s). Last attempt: %3.")
                                          .arg(existing->successCount)
                                          .arg(existing->failureCount)
                                          .arg(lastAttemptText(existing->lastAttempt)),
                                      this);
        statistics->setObjectName("cardStatistics");
        statistics->setEnabled(false);
        statistics->setWordWrap(true);
        form->addRow("Statistics:", statistics);
    }
    root->addLayout(form, 1);
    m_error = new QLabel(this);
    m_error->setObjectName("cardError");
    m_error->setStyleSheet("color: #b3261e;");
    m_error->setWordWrap(true);
    m_error->hide();
    root->addWidget(m_error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName("cardSave");
    connect(buttons, &QDialogButtonBox::accepted, this, &CardEditDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_question, &QPlainTextEdit::textChanged, m_error, &QLabel::hide);
    connect(m_answer, &QPlainTextEdit::textChanged, m_error, &QLabel::hide);
    root->addWidget(buttons);
    m_question->setFocus();
}

void CardEditDialog::save() {
    const auto fail = [this](const QString& message) {
        m_error->setText(message);
        m_error->show();
    };
    if (m_question->toPlainText().trimmed().isEmpty()) {
        fail("Enter a question.");
        m_question->setFocus();
        return;
    }
    if (m_answer->toPlainText().trimmed().isEmpty()) {
        fail("Enter an answer.");
        m_answer->setFocus();
        return;
    }
    const auto question = qtbridge::toCore(m_question->toPlainText());
    const auto answer = qtbridge::toCore(m_answer->toPlainText());
    auto& cards = services().core.cards;
    // Everything typed stays when the card is refused, with the reason.
    auto saved = m_cardId > 0 ? cards.updateCard(m_cardId, question, answer)
                              : cards.createCard(m_itemId, question, answer);
    if (!saved) {
        fail(qtbridge::toQt(saved.error().message));
        return;
    }
    m_saved = std::move(*saved);
    accept();
}

CardsDialog::CardsDialog(int itemId, QWidget* parent) : QDialog(parent), m_itemId(itemId) {
    setWindowTitle("Cards");
    resize(860, 480);
    auto* root = new QVBoxLayout(this);
    m_title = new QLabel(this);
    m_title->setObjectName("cardsItemTitle");
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    root->addWidget(m_title);
    auto* hint = new QLabel("Questions to ask yourself about this item. Each change is saved at once; "
                            "the counts are kept by the quiz.", this);
    hint->setEnabled(false);
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_table = new QTableWidget(0, 5, this);
    m_table->setObjectName("cardsTable");
    m_table->setHorizontalHeaderLabels({"Question", "Answer", "Success", "Failure", "Last attempt"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int column = 2; column < 5; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    auto* actions = new QHBoxLayout();
    auto* addButton = new QPushButton("Add...", this);
    addButton->setObjectName("cardAdd");
    m_editButton = new QPushButton("Edit...", this);
    m_editButton->setObjectName("cardEdit");
    m_deleteButton = new QPushButton("Delete", this);
    m_deleteButton->setObjectName("cardDelete");
    m_quizButton = new QPushButton("Quiz...", this);
    m_quizButton->setObjectName("cardQuiz");
    m_quizButton->setToolTip("Go through this item's cards");
    for (auto* button : {addButton, m_editButton, m_deleteButton, m_quizButton}) {
        button->setAutoDefault(false);
        actions->addWidget(button);
    }
    actions->addStretch();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    actions->addWidget(buttons);
    root->addLayout(actions);

    connect(addButton, &QPushButton::clicked, this, &CardsDialog::addCard);
    connect(m_editButton, &QPushButton::clicked, this, &CardsDialog::editCard);
    connect(m_deleteButton, &QPushButton::clicked, this, &CardsDialog::deleteCard);
    connect(m_quizButton, &QPushButton::clicked, this, &CardsDialog::startQuiz);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &CardsDialog::updateButtons);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { editCard(); });

    ItemRecord item;
    QString error;
    if (services().items.loadItem(itemId, item, &error))
        m_title->setText(item.disambiguation.isEmpty() ? item.title
                                                       : QString("%1 [%2]").arg(item.title, item.disambiguation));
    reload();
    addButton->setFocus();
}

void CardsDialog::reload(int selectCardId) {
    auto cards = services().core.cards.loadCards(m_itemId);
    if (!cards) {
        QMessageBox::critical(this, "Cards", qtbridge::toQt(cards.error().message));
        return;
    }
    m_cards = std::move(*cards);
    m_table->setRowCount(static_cast<int>(m_cards.size()));
    int selected = -1;
    for (int row = 0; row < static_cast<int>(m_cards.size()); ++row) {
        const auto& card = m_cards[static_cast<std::size_t>(row)];
        m_table->setItem(row, 0, textCell(card.question));
        m_table->setItem(row, 1, textCell(card.answer));
        m_table->setItem(row, 2, numberCell(card.successCount));
        m_table->setItem(row, 3, numberCell(card.failureCount));
        m_table->setItem(row, 4, new QTableWidgetItem(lastAttemptText(card.lastAttempt)));
        if (card.id == selectCardId) selected = row;
    }
    if (selected >= 0) m_table->selectRow(selected);
    else m_table->clearSelection();
    updateButtons();
}

int CardsDialog::selectedRow() const {
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : rows.first().row();
}

void CardsDialog::updateButtons() {
    const bool selected = selectedRow() >= 0;
    m_editButton->setEnabled(selected);
    m_deleteButton->setEnabled(selected);
    m_quizButton->setEnabled(!m_cards.empty());
}

void CardsDialog::addCard() {
    CardEditDialog dialog(m_itemId, nullptr, this);
    if (dialog.exec() == QDialog::Accepted) reload(dialog.savedCard().id);
}

void CardsDialog::editCard() {
    const int row = selectedRow();
    if (row < 0) return;
    const auto card = m_cards[static_cast<std::size_t>(row)];
    CardEditDialog dialog(m_itemId, &card, this);
    if (dialog.exec() == QDialog::Accepted) reload(card.id);
}

void CardsDialog::deleteCard() {
    const int row = selectedRow();
    if (row < 0) return;
    const auto& card = m_cards[static_cast<std::size_t>(row)];
    QString question = qtbridge::toQt(card.question).replace('\n', ' ');
    if (question.size() > 80) question = question.left(79) + "…";
    if (QMessageBox::question(this, "Delete card", QString("Delete the card '%1'?").arg(question)) != QMessageBox::Yes)
        return;
    if (auto deleted = services().core.cards.deleteCard(card.id); !deleted) {
        QMessageBox::critical(this, "Cards", qtbridge::toQt(deleted.error().message));
        return;
    }
    reload();
}

void CardsDialog::startQuiz() {
    CardQuizDialog dialog(m_itemId, 0, this);
    dialog.exec();
    // The quiz moved the counts.
    const int row = selectedRow();
    reload(row >= 0 ? m_cards[static_cast<std::size_t>(row)].id : -1);
}
