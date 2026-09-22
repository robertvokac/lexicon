#pragma once

#include "ApplicationContext.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QTextBrowser;
class QStackedWidget;
class CodeHighlighter;

// Goes through the items due for review, one card at a time: the title first,
// the content on request, then a rating that moves the understanding and sets
// the next review.
class ReviewDialog : public QDialog {
    Q_OBJECT

public:
    explicit ReviewDialog(QWidget* parent = nullptr);
    // Whether any review was recorded, so the item list is refreshed.
    bool changedItems() const { return m_changed; }

private:
    void loadQueue();
    void showCard();
    void showAnswer();
    void rate(lexicon::ReviewRating rating);
    void skip();

    QComboBox* m_groupCombo = nullptr;
    QLabel* m_dueLabel = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_detailLabel = nullptr;
    QTextBrowser* m_content = nullptr;
    CodeHighlighter* m_highlighter = nullptr;
    QPushButton* m_showButton = nullptr;
    QList<QPushButton*> m_ratingButtons;
    QPushButton* m_skipButton = nullptr;
    QLabel* m_doneLabel = nullptr;
    QPushButton* m_continueButton = nullptr;

    QList<ItemRecord> m_queue;
    int m_reviewed = 0;
    bool m_changed = false;
};
