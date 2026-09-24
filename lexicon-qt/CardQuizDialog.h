#pragma once

#include "ApplicationContext.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QStackedWidget;

// A quiz over cards, one at a time: the question, the answer on request, and
// Yes or No - did you know it. Only Yes and No count, on the card and never
// on the item's review. Space shows the answer; Y and N answer.
class CardQuizDialog : public QDialog {
    Q_OBJECT

public:
    // How many items a neighbourhood quiz covers at most, as the graph draws.
    static constexpr int kMaxItems = 150;

    // depth 0 quizzes the item's own cards; 1 to 3 its neighbourhood.
    CardQuizDialog(int itemId, int depth, QWidget* parent = nullptr);
    // 0 for this item, else how many links away the quiz reaches.
    int depth() const;
    void setDepth(int depth);
    int cardCount() const { return static_cast<int>(m_quiz.cards.size()); }
    int yesCount() const { return m_yes; }
    int noCount() const { return m_no; }

    void showAnswer();
    // Records the answer to the current card and moves on.
    void answer(bool knew);

private:
    void load();
    void showCard();

    int m_itemId = -1;
    QRadioButton* m_thisItem = nullptr;
    QRadioButton* m_neighborhood = nullptr;
    QComboBox* m_depthCombo = nullptr;
    QLabel* m_scopeSummary = nullptr;
    QLabel* m_truncated = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_source = nullptr;
    QLabel* m_progress = nullptr;
    QLabel* m_question = nullptr;
    QPushButton* m_showButton = nullptr;
    QWidget* m_answerBox = nullptr;
    QLabel* m_answer = nullptr;
    QPushButton* m_yesButton = nullptr;
    QPushButton* m_noButton = nullptr;
    QLabel* m_message = nullptr;
    QPushButton* m_againButton = nullptr;

    lexicon::CardQuizSet m_quiz;
    std::size_t m_index = 0;
    bool m_revealed = false;
    bool m_busy = false;
    int m_yes = 0;
    int m_no = 0;
};
