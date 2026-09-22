#include "AlarmNotifier.h"

#include "AlarmsDialog.h"

#include <algorithm>

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace {
// An alarm left ringing is announced again after this long.
constexpr qint64 kReminderSeconds = 5 * 60;

QString keyOf(const lexicon::AlarmRecord& alarm) {
    return QString::number(alarm.id) + "@" + qtbridge::toQt(alarm.firesAt);
}
} // namespace

AlarmRingDialog::AlarmRingDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Alarm");
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setWindowModality(Qt::ApplicationModal);
    resize(460, 240);
    auto* root = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    m_list = new QVBoxLayout(content);
    m_list->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
    m_error = new QLabel(this);
    m_error->setStyleSheet("color: #b3261e;");
    m_error->setWordWrap(true);
    m_error->hide();
    root->addWidget(m_error);
    auto* bottom = new QHBoxLayout();
    auto* dismissAll = new QPushButton("Dismiss all", this);
    dismissAll->setObjectName("alarmDismissAll");
    auto* later = new QPushButton("Later", this);
    later->setToolTip("Hide this window; the alarms keep ringing and come back in a few minutes.");
    connect(dismissAll, &QPushButton::clicked, this, &AlarmRingDialog::dismissAll);
    connect(later, &QPushButton::clicked, this, &QDialog::hide);
    bottom->addWidget(dismissAll);
    bottom->addStretch();
    bottom->addWidget(later);
    root->addLayout(bottom);
}

void AlarmRingDialog::setAlarms(const std::vector<lexicon::AlarmRecord>& alarms) {
    // Rebuilt only when something changed: the notifier asks every few
    // seconds, and a button replaced under the cursor loses its click.
    const auto same = [](const lexicon::AlarmRecord& a, const lexicon::AlarmRecord& b) {
        return a.id == b.id && a.title == b.title && a.description == b.description && a.firesAt == b.firesAt;
    };
    if (std::equal(alarms.begin(), alarms.end(), m_alarms.begin(), m_alarms.end(), same) && m_list->count() > 0) return;
    m_alarms = alarms;
    while (auto* item = m_list->takeAt(0)) {
        if (auto* widget = item->widget()) widget->deleteLater();
        delete item;
    }
    setWindowTitle(alarms.size() == 1 ? "Alarm" : QString("%1 alarms").arg(alarms.size()));
    for (const auto& alarm : alarms) {
        auto* card = new QFrame(this);
        card->setObjectName(QString("alarmCard_%1").arg(alarm.id));
        card->setFrameShape(QFrame::StyledPanel);
        auto* layout = new QVBoxLayout(card);
        auto* title = new QLabel(QString("<b>%1</b>").arg(qtbridge::toQt(alarm.title).toHtmlEscaped()), card);
        title->setTextFormat(Qt::RichText);
        layout->addWidget(title);
        auto* when = new QLabel(QLocale().toString(alarmtime::fromUtcText(alarm.firesAt), "ddd yyyy-MM-dd HH:mm"), card);
        when->setEnabled(false);
        layout->addWidget(when);
        if (!alarm.description.empty()) {
            auto* description = new QLabel(qtbridge::toQt(alarm.description), card);
            description->setWordWrap(true);
            description->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(description);
        }
        auto* buttons = new QHBoxLayout();
        auto* dismissButton = new QPushButton("Dismiss", card);
        dismissButton->setObjectName(QString("alarmDismiss_%1").arg(alarm.id));
        auto* snooze10 = new QPushButton("Snooze 10 min", card);
        snooze10->setObjectName(QString("alarmSnooze_%1").arg(alarm.id));
        auto* snooze60 = new QPushButton("Snooze 1 hour", card);
        connect(dismissButton, &QPushButton::clicked, this, [this, id = alarm.id] { dismiss(id); });
        connect(snooze10, &QPushButton::clicked, this, [this, id = alarm.id] { snooze(id, 10); });
        connect(snooze60, &QPushButton::clicked, this, [this, id = alarm.id] { snooze(id, 60); });
        buttons->addWidget(dismissButton);
        buttons->addWidget(snooze10);
        buttons->addWidget(snooze60);
        buttons->addStretch();
        layout->addLayout(buttons);
        m_list->addWidget(card);
    }
    m_list->addStretch();
}

void AlarmRingDialog::dismiss(int alarmId) {
    if (auto dismissed = services().core.alarms.dismissAlarm(alarmId); !dismissed) {
        m_error->setText(qtbridge::toQt(dismissed.error().message));
        m_error->show();
        return;
    }
    m_error->hide();
    emit alarmsChanged();
}

void AlarmRingDialog::snooze(int alarmId, int minutes) {
    if (auto snoozed = services().core.alarms.snoozeAlarm(alarmId, minutes); !snoozed) {
        m_error->setText(qtbridge::toQt(snoozed.error().message));
        m_error->show();
        return;
    }
    m_error->hide();
    emit alarmsChanged();
}

void AlarmRingDialog::dismissAll() {
    for (const auto& alarm : std::vector<lexicon::AlarmRecord>(m_alarms)) {
        if (auto dismissed = services().core.alarms.dismissAlarm(alarm.id);
            !dismissed && dismissed.error().code != lexicon::Error::Code::NotFound) {
            m_error->setText(qtbridge::toQt(dismissed.error().message));
            m_error->show();
        }
    }
    emit alarmsChanged();
}

AlarmNotifier::AlarmNotifier(QWidget* window, int intervalMs) : QObject(window), m_window(window) {
    m_timer.setInterval(intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &AlarmNotifier::check);
    m_timer.start();
    // The first look once the window is up, not while it is being built.
    QTimer::singleShot(0, this, &AlarmNotifier::check);
}

void AlarmNotifier::check() {
    auto due = services().core.alarms.loadDueAlarms();
    if (!due) return; // A busy or unreachable database is asked again next time.
    if (due->empty()) {
        if (m_dialog) m_dialog->hide();
        if (m_tray) m_tray->hide();
        m_announced.clear();
        return;
    }
    if (!m_dialog) {
        m_dialog = new AlarmRingDialog(m_window);
        m_dialog->setObjectName("alarmRing");
        connect(m_dialog, &AlarmRingDialog::alarmsChanged, this, &AlarmNotifier::check);
    }
    m_dialog->setAlarms(*due);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    std::vector<lexicon::AlarmRecord> fresh;
    QHash<QString, QDateTime> stillRinging;
    for (const auto& alarm : *due) {
        const QString key = keyOf(alarm);
        const auto seen = m_announced.constFind(key);
        if (seen == m_announced.cend() || seen->secsTo(now) >= kReminderSeconds) {
            fresh.push_back(alarm);
            stillRinging.insert(key, now);
        } else {
            stillRinging.insert(key, *seen);
        }
    }
    m_announced = stillRinging;
    if (!fresh.empty()) announce(fresh);
}

void AlarmNotifier::announce(const std::vector<lexicon::AlarmRecord>& fresh) {
    ++m_announcements;
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
    QApplication::alert(m_window);
    QApplication::beep();
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    if (!m_tray) {
        QIcon icon = qApp->windowIcon();
        if (icon.isNull()) icon = qApp->style()->standardIcon(QStyle::SP_MessageBoxInformation);
        m_tray = new QSystemTrayIcon(icon, this);
        m_tray->setToolTip("Lexicon alarms");
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
            if (m_dialog) { m_dialog->show(); m_dialog->raise(); m_dialog->activateWindow(); }
        });
        connect(m_tray, &QSystemTrayIcon::activated, this, [this] {
            if (m_dialog) { m_dialog->show(); m_dialog->raise(); m_dialog->activateWindow(); }
        });
    }
    m_tray->show();
    const auto& first = fresh.front();
    const QString title = fresh.size() == 1 ? "Alarm: " + qtbridge::toQt(first.title)
                                            : QString("%1 alarms").arg(fresh.size());
    QStringList lines;
    if (fresh.size() == 1) {
        lines << qtbridge::toQt(first.description).section('\n', 0, 2);
    } else {
        for (const auto& alarm : fresh) lines << qtbridge::toQt(alarm.title);
    }
    m_tray->showMessage(title, lines.join('\n').trimmed(), QSystemTrayIcon::Information, 60000);
}
