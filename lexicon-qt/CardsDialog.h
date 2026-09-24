#pragma once

#include "ApplicationContext.h"

#include <QDialog>

#include <vector>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// A card's question and answer, in an editor of their own. The statistics
// are shown, never edited. Save stores the card at once; a card that is
// refused keeps the dialog open with the reason.
class CardEditDialog : public QDialog {
    Q_OBJECT

public:
    // Adds a card to the item, or edits `existing` when one is given.
    CardEditDialog(int itemId, const lexicon::CardRecord* existing, QWidget* parent = nullptr);
    // The card as stored, once saved.
    const lexicon::CardRecord& savedCard() const { return m_saved; }

private:
    void save();

    int m_itemId = -1;
    int m_cardId = -1;
    QPlainTextEdit* m_question = nullptr;
    QPlainTextEdit* m_answer = nullptr;
    QLabel* m_error = nullptr;
    lexicon::CardRecord m_saved;
};

// The cards of one item: add, edit and delete them, and see how often each
// was known. Every change is saved at once - it is not part of the item
// editor's Save or Cancel.
class CardsDialog : public QDialog {
    Q_OBJECT

public:
    explicit CardsDialog(int itemId, QWidget* parent = nullptr);
    int cardCount() const { return static_cast<int>(m_cards.size()); }

    void addCard();
    void editCard();
    void deleteCard();
    void startQuiz();

private:
    void reload(int selectCardId = -1);
    int selectedRow() const;
    void updateButtons();

    int m_itemId = -1;
    QLabel* m_title = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_quizButton = nullptr;
    std::vector<lexicon::CardRecord> m_cards;
};

// "Never", or when the card was last answered, in local time.
QString lastAttemptText(const std::string& utc);
