#include "ReviewDialog.h"

#include "MainWindow.h" // CodeHighlighter
#include "MarkdownConverter.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
constexpr int kBatch = 20;

QString understandingName(UnderstandingLevel level) {
    switch (level) {
        case UnderstandingLevel::Recognized: return "Recognized";
        case UnderstandingLevel::Understood: return "Understood";
        case UnderstandingLevel::Practiced: return "Practiced";
        case UnderstandingLevel::Mastered: return "Mastered";
        default: return "Unknown";
    }
}

QString days(int count) {
    return count == 1 ? "1 day" : QString("%1 days").arg(count);
}
} // namespace

ReviewDialog::ReviewDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Review");
    resize(720, 560);
    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    top->addWidget(new QLabel("Group:", this));
    m_groupCombo = new QComboBox(this);
    m_groupCombo->addItem("All groups", -1);
    for (const auto& group : services().groups.loadGroups()) m_groupCombo->addItem(group.name, group.id);
    top->addWidget(m_groupCombo);
    top->addStretch();
    m_dueLabel = new QLabel(this);
    top->addWidget(m_dueLabel);
    root->addLayout(top);

    m_pages = new QStackedWidget(this);
    auto* card = new QWidget(m_pages);
    auto* cardLayout = new QVBoxLayout(card);
    m_titleLabel = new QLabel(card);
    m_titleLabel->setObjectName("reviewTitle");
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.8);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailLabel = new QLabel(card);
    m_detailLabel->setWordWrap(true);
    m_content = new QTextBrowser(card);
    m_content->setObjectName("reviewContent");
    m_content->setOpenExternalLinks(true);
    m_highlighter = new CodeHighlighter(m_content->document());
    cardLayout->addWidget(m_titleLabel);
    cardLayout->addWidget(m_detailLabel);
    cardLayout->addWidget(m_content, 1);

    auto* actions = new QHBoxLayout();
    m_showButton = new QPushButton("Show answer", card);
    m_showButton->setObjectName("reviewShow");
    m_showButton->setDefault(true);
    actions->addWidget(m_showButton);
    const std::pair<lexicon::ReviewRating, QString> ratings[] = {
        {lexicon::ReviewRating::Again, "Again"}, {lexicon::ReviewRating::Hard, "Hard"},
        {lexicon::ReviewRating::Good, "Good"}, {lexicon::ReviewRating::Easy, "Easy"}};
    int shortcut = 1;
    for (const auto& [rating, label] : ratings) {
        auto* button = new QPushButton(label, card);
        button->setProperty("rating", static_cast<int>(rating));
        button->setObjectName("review" + label);
        button->setShortcut(QKeySequence(QString::number(shortcut++)));
        connect(button, &QPushButton::clicked, this, [this, rating] { rate(rating); });
        actions->addWidget(button);
        m_ratingButtons.push_back(button);
    }
    actions->addStretch();
    m_skipButton = new QPushButton("Skip", card);
    actions->addWidget(m_skipButton);
    cardLayout->addLayout(actions);
    m_pages->addWidget(card);

    auto* done = new QWidget(m_pages);
    auto* doneLayout = new QVBoxLayout(done);
    m_doneLabel = new QLabel(done);
    m_doneLabel->setObjectName("reviewDone");
    m_doneLabel->setWordWrap(true);
    m_doneLabel->setAlignment(Qt::AlignCenter);
    m_continueButton = new QPushButton("Continue", done);
    doneLayout->addStretch();
    doneLayout->addWidget(m_doneLabel);
    doneLayout->addWidget(m_continueButton, 0, Qt::AlignCenter);
    doneLayout->addStretch();
    m_pages->addWidget(done);
    root->addWidget(m_pages, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_showButton, &QPushButton::clicked, this, &ReviewDialog::showAnswer);
    connect(m_skipButton, &QPushButton::clicked, this, &ReviewDialog::skip);
    connect(m_continueButton, &QPushButton::clicked, this, &ReviewDialog::loadQueue);
    connect(m_groupCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ReviewDialog::loadQueue);
    loadQueue();
}

void ReviewDialog::loadQueue() {
    const int groupId = m_groupCombo->currentData().toInt();
    auto items = services().core.review.queue(groupId, kBatch);
    auto due = services().core.review.countDue(groupId);
    if (!items || !due) {
        QMessageBox::critical(this, "Review", qtbridge::toQt((!items ? items.error() : due.error()).message));
        return;
    }
    m_queue = qtbridge::toQt(*items);
    m_dueLabel->setText(QString("%1 due").arg(*due));
    showCard();
}

void ReviewDialog::showCard() {
    if (m_queue.isEmpty()) {
        const int groupId = m_groupCombo->currentData().toInt();
        const int due = services().core.review.countDue(groupId).value_or(0);
        m_dueLabel->setText(QString("%1 due").arg(due));
        m_doneLabel->setText(due > 0
            ? QString("%1 reviewed. %2 more item(s) are due.").arg(m_reviewed).arg(due)
            : QString("%1 reviewed. Nothing else is due now.").arg(m_reviewed));
        m_continueButton->setVisible(due > 0);
        m_pages->setCurrentIndex(1);
        return;
    }
    m_pages->setCurrentIndex(0);
    const auto& item = m_queue.first();
    m_titleLabel->setText(item.disambiguation.isEmpty()
        ? item.title : QString("%1 [%2]").arg(item.title, item.disambiguation));
    QStringList details{item.groupName};
    if (!item.itemTypeName.isEmpty()) details << item.itemTypeName;
    if (!item.tags.isEmpty()) details << item.tags.join(", ");
    details << understandingName(item.understanding);
    details << (item.reviewedAt.isEmpty() ? "never reviewed" : "last reviewed " + item.reviewedAt.left(10));
    m_detailLabel->setText(details.join(" · "));
    m_content->clear();
    m_content->setVisible(false);
    m_showButton->setVisible(true);
    m_showButton->setFocus();
    for (auto* button : m_ratingButtons) {
        const auto rating = static_cast<lexicon::ReviewRating>(button->property("rating").toInt());
        const auto next = lexicon::levelAfterReview(static_cast<lexicon::UnderstandingLevel>(item.understanding), rating);
        const QString label = QStringList{"Again", "Hard", "Good", "Easy"}.at(static_cast<int>(rating));
        button->setText(QString("%1 (%2)").arg(label, days(lexicon::reviewIntervalDays(next))));
        button->setToolTip(QString("Understanding becomes %1").arg(understandingName(static_cast<UnderstandingLevel>(next))));
        button->setEnabled(false);
    }
}

void ReviewDialog::showAnswer() {
    if (m_queue.isEmpty()) return;
    ItemRecord item;
    QString error;
    if (!services().items.loadItem(m_queue.first().id, item, &error)) {
        QMessageBox::critical(this, "Review", error);
        return;
    }
    m_content->setHtml(item.content.isEmpty()
        ? QString("<p><i>This item has no content.</i></p>") : MarkdownConverter::toHtml(item.content));
    m_content->setVisible(true);
    m_showButton->setVisible(false);
    for (auto* button : m_ratingButtons) button->setEnabled(true);
    m_ratingButtons.at(2)->setFocus();
    services().items.logItemRead(item.id);
}

void ReviewDialog::rate(lexicon::ReviewRating rating) {
    if (m_queue.isEmpty() || !m_content->isVisible()) return;
    auto reviewed = services().core.review.review(m_queue.first().id, rating);
    if (!reviewed) {
        QMessageBox::critical(this, "Review", qtbridge::toQt(reviewed.error().message));
        return;
    }
    m_changed = true;
    ++m_reviewed;
    auto item = qtbridge::toQt(*reviewed);
    m_queue.removeFirst();
    // Forgotten items come back at the end of this sitting.
    if (rating == lexicon::ReviewRating::Again) m_queue.push_back(item);
    showCard();
}

void ReviewDialog::skip() {
    if (m_queue.isEmpty()) return;
    m_queue.removeFirst();
    showCard();
}
