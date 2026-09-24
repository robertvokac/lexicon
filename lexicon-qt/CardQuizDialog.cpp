#include "CardQuizDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
// A heading over a rule, as "Question" and "Answer" stand over their text.
QWidget* heading(const QString& text, QWidget* parent) {
    auto* box = new QWidget(parent);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 6, 0, 0);
    auto* label = new QLabel(text, box);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    auto* rule = new QFrame(box);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(label);
    layout->addWidget(rule);
    return box;
}

// Card text is plain text, never markup, shown with its line breaks.
QLabel* cardText(const char* name, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    QFont larger = label->font();
    larger.setPointSizeF(larger.pointSizeF() * 1.25);
    label->setFont(larger);
    return label;
}
} // namespace

CardQuizDialog::CardQuizDialog(int itemId, int depth, QWidget* parent) : QDialog(parent), m_itemId(itemId) {
    setWindowTitle("Card quiz");
    resize(720, 560);
    auto* root = new QVBoxLayout(this);

    auto* scope = new QHBoxLayout();
    scope->addWidget(new QLabel("Cards of:", this));
    m_thisItem = new QRadioButton("This item", this);
    m_thisItem->setObjectName("quizThisItem");
    m_neighborhood = new QRadioButton("Neighborhood", this);
    m_neighborhood->setObjectName("quizNeighborhood");
    m_depthCombo = new QComboBox(this);
    m_depthCombo->setObjectName("quizDepth");
    m_depthCombo->addItem("1 link away", 1);
    m_depthCombo->addItem("2 links away", 2);
    m_depthCombo->addItem("3 links away", 3);
    // The graph's default depth.
    m_depthCombo->setCurrentIndex(1);
    scope->addWidget(m_thisItem);
    scope->addWidget(m_neighborhood);
    scope->addWidget(m_depthCombo);
    scope->addStretch();
    m_scopeSummary = new QLabel(this);
    m_scopeSummary->setObjectName("quizScopeSummary");
    scope->addWidget(m_scopeSummary);
    root->addLayout(scope);
    m_truncated = new QLabel(QString("The neighborhood has more items than a quiz covers; "
                                     "the %1 nearest are included.").arg(kMaxItems), this);
    m_truncated->setObjectName("quizTruncated");
    m_truncated->setWordWrap(true);
    m_truncated->hide();
    root->addWidget(m_truncated);

    m_pages = new QStackedWidget(this);
    auto* scroll = new QScrollArea(m_pages);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* card = new QWidget(scroll);
    auto* cardLayout = new QVBoxLayout(card);
    auto* top = new QHBoxLayout();
    m_source = new QLabel(card);
    m_source->setObjectName("quizSource");
    m_source->setTextFormat(Qt::PlainText);
    m_source->setWordWrap(true);
    m_progress = new QLabel(card);
    m_progress->setObjectName("quizProgress");
    top->addWidget(m_source, 1);
    top->addWidget(m_progress, 0, Qt::AlignTop);
    cardLayout->addLayout(top);
    cardLayout->addWidget(heading("Question", card));
    m_question = cardText("quizQuestion", card);
    cardLayout->addWidget(m_question);
    m_showButton = new QPushButton("Show answer", card);
    m_showButton->setObjectName("quizShow");
    m_showButton->setToolTip("Space");
    m_showButton->setAutoDefault(false);
    cardLayout->addWidget(m_showButton, 0, Qt::AlignLeft);
    m_answerBox = new QWidget(card);
    auto* answerLayout = new QVBoxLayout(m_answerBox);
    answerLayout->setContentsMargins(0, 0, 0, 0);
    answerLayout->addWidget(heading("Answer", m_answerBox));
    m_answer = cardText("quizAnswer", m_answerBox);
    answerLayout->addWidget(m_answer);
    auto* verdict = new QHBoxLayout();
    verdict->addWidget(new QLabel("Do you know?", m_answerBox));
    m_yesButton = new QPushButton("Yes", m_answerBox);
    m_yesButton->setObjectName("quizYes");
    m_yesButton->setToolTip("Y");
    m_noButton = new QPushButton("No", m_answerBox);
    m_noButton->setObjectName("quizNo");
    m_noButton->setToolTip("N");
    for (auto* button : {m_yesButton, m_noButton}) {
        button->setAutoDefault(false);
        verdict->addWidget(button);
    }
    verdict->addStretch();
    answerLayout->addLayout(verdict);
    cardLayout->addWidget(m_answerBox);
    cardLayout->addStretch();
    scroll->setWidget(card);
    m_pages->addWidget(scroll);

    auto* done = new QWidget(m_pages);
    auto* doneLayout = new QVBoxLayout(done);
    m_message = new QLabel(done);
    m_message->setObjectName("quizMessage");
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setAlignment(Qt::AlignCenter);
    m_againButton = new QPushButton("Start again", done);
    m_againButton->setObjectName("quizAgain");
    m_againButton->setAutoDefault(false);
    doneLayout->addStretch();
    doneLayout->addWidget(m_message);
    doneLayout->addWidget(m_againButton, 0, Qt::AlignCenter);
    doneLayout->addStretch();
    m_pages->addWidget(done);
    root->addWidget(m_pages, 1);

    auto* hint = new QLabel("Space shows the answer; Y and N answer. Only Yes and No count, on the card - "
                            "never on the item's review.", this);
    hint->setEnabled(false);
    hint->setWordWrap(true);
    root->addWidget(hint);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_showButton, &QPushButton::clicked, this, &CardQuizDialog::showAnswer);
    connect(m_yesButton, &QPushButton::clicked, this, [this] { answer(true); });
    connect(m_noButton, &QPushButton::clicked, this, [this] { answer(false); });
    connect(m_againButton, &QPushButton::clicked, this, &CardQuizDialog::load);
    // A key held down answers once: shortcuts here never repeat.
    const auto key = [this](Qt::Key code, auto action) {
        auto* shortcut = new QShortcut(QKeySequence(code), this);
        shortcut->setAutoRepeat(false);
        connect(shortcut, &QShortcut::activated, this, action);
    };
    key(Qt::Key_Space, [this] { showAnswer(); });
    key(Qt::Key_Y, [this] { answer(true); });
    key(Qt::Key_N, [this] { answer(false); });
    connect(m_thisItem, &QRadioButton::toggled, this, [this](bool on) { if (on) load(); });
    connect(m_neighborhood, &QRadioButton::toggled, this, [this](bool on) { if (on) load(); });
    connect(m_depthCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (m_neighborhood->isChecked()) load();
    });
    setDepth(depth);
}

int CardQuizDialog::depth() const {
    return m_thisItem->isChecked() ? 0 : m_depthCombo->currentData().toInt();
}

void CardQuizDialog::setDepth(int depth) {
    {
        const QSignalBlocker thisItem(m_thisItem);
        const QSignalBlocker neighborhood(m_neighborhood);
        const QSignalBlocker combo(m_depthCombo);
        if (depth > 0) {
            m_neighborhood->setChecked(true);
            const int index = m_depthCombo->findData(std::min(depth, lexicon::CardService::kMaxDepth));
            if (index >= 0) m_depthCombo->setCurrentIndex(index);
        } else {
            m_thisItem->setChecked(true);
        }
    }
    load();
}

void CardQuizDialog::load() {
    m_depthCombo->setEnabled(m_neighborhood->isChecked());
    m_index = 0;
    m_yes = 0;
    m_no = 0;
    auto quiz = services().core.cards.quizCards(m_itemId, depth(), kMaxItems);
    if (!quiz) {
        m_quiz = {};
        m_scopeSummary->clear();
        m_truncated->hide();
        m_message->setText(qtbridge::toQt(quiz.error().message));
        m_againButton->hide();
        m_pages->setCurrentIndex(1);
        return;
    }
    m_quiz = std::move(*quiz);
    m_scopeSummary->setText(QString("%1 card(s) from %2 item(s)").arg(m_quiz.cards.size()).arg(m_quiz.itemCount));
    m_truncated->setVisible(m_quiz.truncated);
    showCard();
}

void CardQuizDialog::showCard() {
    m_revealed = false;
    m_answer->clear();
    if (m_index >= m_quiz.cards.size()) {
        if (m_quiz.cards.empty()) {
            m_message->setText("No cards are available for this quiz.");
            m_againButton->hide();
        } else {
            m_message->setText(QString("Cards: %1\nYes: %2\nNo: %3").arg(m_quiz.cards.size()).arg(m_yes).arg(m_no));
            m_againButton->show();
            m_againButton->setFocus();
        }
        m_pages->setCurrentIndex(1);
        return;
    }
    const auto& entry = m_quiz.cards[m_index];
    m_source->setText("Item: " + qtbridge::toQt(entry.itemTitle));
    m_progress->setText(QString("%1 / %2").arg(m_index + 1).arg(m_quiz.cards.size()));
    m_question->setText(qtbridge::toQt(entry.card.question));
    m_answerBox->hide();
    m_showButton->show();
    m_pages->setCurrentIndex(0);
    m_showButton->setFocus();
}

void CardQuizDialog::showAnswer() {
    if (m_revealed || m_index >= m_quiz.cards.size()) return;
    // Seeing the answer records nothing; only Yes or No does.
    m_revealed = true;
    m_answer->setText(qtbridge::toQt(m_quiz.cards[m_index].card.answer));
    m_showButton->hide();
    m_answerBox->show();
    m_yesButton->setEnabled(true);
    m_noButton->setEnabled(true);
    m_yesButton->setFocus();
}

void CardQuizDialog::answer(bool knew) {
    if (!m_revealed || m_busy || m_index >= m_quiz.cards.size()) return;
    m_busy = true;
    m_yesButton->setEnabled(false);
    m_noButton->setEnabled(false);
    auto recorded = services().core.cards.recordAttempt(m_quiz.cards[m_index].card.id, knew);
    m_busy = false;
    if (!recorded) {
        QMessageBox::critical(this, "Card quiz", qtbridge::toQt(recorded.error().message));
        // A card deleted meanwhile cannot be answered; the quiz goes on.
        if (recorded.error().code == lexicon::Error::Code::NotFound) {
            ++m_index;
            showCard();
            return;
        }
        m_yesButton->setEnabled(true);
        m_noButton->setEnabled(true);
        return;
    }
    m_quiz.cards[m_index].card = std::move(*recorded);
    ++(knew ? m_yes : m_no);
    ++m_index;
    showCard();
}
