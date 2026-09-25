#include "AlarmsDialog.h"

#include <QDateTimeEdit>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

namespace alarmtime {
QDateTime fromUtcText(const std::string& text) {
    return QDateTime::fromString(qtbridge::toQt(text), Qt::ISODate).toLocalTime();
}
std::string toUtcText(const QDateTime& local) {
    QDateTime utc = local.toUTC();
    utc.setTime(QTime(utc.time().hour(), utc.time().minute(), utc.time().second()));
    return qtbridge::toCore(utc.toString(Qt::ISODate));
}
} // namespace alarmtime

AlarmEditDialog::AlarmEditDialog(const lexicon::AlarmRecord& alarm, QWidget* parent)
    : QDialog(parent), m_alarm(alarm) {
    setWindowTitle(alarm.id < 0 ? "Add alarm" : "Edit alarm");
    resize(480, 340);
    auto* root = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    m_title = new QLineEdit(qtbridge::toQt(alarm.title), this);
    m_title->setObjectName("alarmTitle");
    m_firesAt = new QDateTimeEdit(this);
    m_firesAt->setObjectName("alarmFiresAt");
    m_firesAt->setCalendarPopup(true);
    m_firesAt->setDisplayFormat("yyyy-MM-dd HH:mm");
    if (alarm.firesAt.empty()) {
        // A new alarm starts at the next full hour.
        const QDateTime now = QDateTime::currentDateTime();
        m_firesAt->setDateTime(QDateTime(now.date(), QTime(now.time().hour(), 0)).addSecs(3600));
    } else {
        m_firesAt->setDateTime(alarmtime::fromUtcText(alarm.firesAt));
    }
    m_description = new QPlainTextEdit(qtbridge::toQt(alarm.description), this);
    m_description->setObjectName("alarmDescription");
    m_description->setTabChangesFocus(true);
    form->addRow("Title:", m_title);
    form->addRow("Goes off:", m_firesAt);
    m_repeatDays = new QSpinBox(this);
    m_repeatDays->setRange(0, 365);
    m_repeatDays->setSpecialValueText("One time");
    m_repeatDays->setSuffix(" days");
    m_repeatDays->setValue(alarm.repeatDays);
    form->addRow("Repeat every:", m_repeatDays);
    m_item = new QComboBox(this);
    m_item->addItem("No linked item", -1);
    auto items = services().core.items.loadItems(-1, -1, {}, {}, {}, {}, {}, {}, -1, -1, -1,
                                                -1, 0, 3, lexicon::SortOrder::Ascending);
    if (items) {
        for (const auto& item : *items)
            m_item->addItem(qtbridge::toQt(item.title), item.id);
    }
    if (alarm.itemId > 0) {
        const int index = m_item->findData(alarm.itemId);
        if (index >= 0) m_item->setCurrentIndex(index);
        else {
            m_item->addItem(QString("Item #%1").arg(alarm.itemId), alarm.itemId);
            m_item->setCurrentIndex(m_item->count() - 1);
        }
    }
    form->addRow("Item:", m_item);
    form->addRow("Description:", m_description);
    root->addLayout(form, 1);
    m_error = new QLabel(this);
    m_error->setObjectName("alarmError");
    m_error->setStyleSheet("color: #b3261e;");
    m_error->setWordWrap(true);
    m_error->hide();
    root->addWidget(m_error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setObjectName("alarmSave");
    connect(buttons, &QDialogButtonBox::accepted, this, &AlarmEditDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_title, &QLineEdit::textChanged, m_error, &QLabel::hide);
    root->addWidget(buttons);
    m_title->setFocus();
}

void AlarmEditDialog::save() {
    lexicon::AlarmRecord alarm = m_alarm;
    alarm.title = qtbridge::toCore(m_title->text().trimmed());
    alarm.description = qtbridge::toCore(m_description->toPlainText());
    alarm.firesAt = alarmtime::toUtcText(m_firesAt->dateTime());
    alarm.repeatDays = m_repeatDays->value();
    alarm.itemId = m_item->currentData().toInt();
    if (alarm.title.empty()) {
        m_error->setText("Enter a title.");
        m_error->show();
        m_title->setFocus();
        return;
    }
    // What was typed stays when the save is refused, with the reason.
    auto saved = services().core.alarms.saveAlarm(alarm);
    if (!saved) {
        m_error->setText(qtbridge::toQt(saved.error().message));
        m_error->show();
        return;
    }
    m_alarm = *saved;
    accept();
}

AlarmsDialog::AlarmsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Alarms");
    resize(760, 460);
    auto* root = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 5, this);
    m_table->setObjectName("alarmTable");
    m_table->setHorizontalHeaderLabels({"Goes off", "Title", "Repeats", "Item", "Description"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(1, 220);
    root->addWidget(m_table, 1);
    m_summary = new QLabel(this);
    m_summary->setObjectName("alarmSummary");
    m_summary->setEnabled(false);
    root->addWidget(m_summary);

    auto* buttons = new QHBoxLayout();
    auto* addButton = new QPushButton("Add...", this);
    addButton->setObjectName("alarmAdd");
    m_editButton = new QPushButton("Edit...", this);
    m_deleteButton = new QPushButton("Delete", this);
    m_deleteButton->setObjectName("alarmDelete");
    auto* closeButton = new QPushButton("Close", this);
    buttons->addWidget(addButton);
    buttons->addWidget(m_editButton);
    buttons->addWidget(m_deleteButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, &AlarmsDialog::addAlarm);
    connect(m_editButton, &QPushButton::clicked, this, &AlarmsDialog::editSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, [this] { deleteSelected(); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &AlarmsDialog::selectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &AlarmsDialog::editSelected);
    reload();
}

void AlarmsDialog::reload(int selectId) {
    auto alarms = services().core.alarms.loadAlarms();
    if (!alarms) {
        QMessageBox::critical(this, "Alarms", qtbridge::toQt(alarms.error().message));
        return;
    }
    m_alarms = std::move(*alarms);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QColor past = palette().color(QPalette::PlaceholderText);
    int upcoming = 0;
    m_table->setRowCount(static_cast<int>(m_alarms.size()));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto& alarm = m_alarms[static_cast<std::size_t>(row)];
        const QDateTime when = alarmtime::fromUtcText(alarm.firesAt);
        const bool gone = when.toUTC() <= now;
        if (!gone) ++upcoming;
        const QString description = qtbridge::toQt(alarm.description);
        QString itemTitle;
        if (alarm.itemId > 0) {
            auto item = services().core.items.loadItem(alarm.itemId);
            itemTitle = item ? qtbridge::toQt(item->title) : QString("#%1").arg(alarm.itemId);
        }
        const QStringList cells{QLocale().toString(when, "ddd yyyy-MM-dd HH:mm"), qtbridge::toQt(alarm.title),
                                alarm.repeatDays > 0 ? QString("Every %1 day(s)").arg(alarm.repeatDays) : QString("Once"),
                                itemTitle, description.section('\n', 0, 0)};
        for (int column = 0; column < cells.size(); ++column) {
            auto* cell = new QTableWidgetItem(cells[column]);
            cell->setData(Qt::UserRole, alarm.id);
            if (gone && alarm.dismissedAt.empty()) {
                // Gone off and nobody has dismissed it: it rings until then.
                QFont bold = cell->font();
                bold.setBold(true);
                cell->setFont(bold);
                cell->setToolTip("Ringing");
            } else if (gone) {
                cell->setForeground(past);
                cell->setToolTip("Already gone off");
            } else if (column == 4 && !description.isEmpty()) {
                cell->setToolTip(description);
            }
            m_table->setItem(row, column, cell);
        }
    }
    m_summary->setText(m_alarms.empty() ? QString("No alarms yet.")
                                        : QString("%1 alarm(s), %2 still to go off").arg(m_alarms.size()).arg(upcoming));
    if (selectId > 0) selectAlarm(selectId);
    selectionChanged();
}

void AlarmsDialog::selectAlarm(int alarmId) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->data(Qt::UserRole).toInt() == alarmId) {
            m_table->selectRow(row);
            return;
        }
    }
}

int AlarmsDialog::selectedId() const {
    const auto rows = m_table->selectionModel()->selectedRows();
    return rows.isEmpty() ? -1 : m_table->item(rows.first().row(), 0)->data(Qt::UserRole).toInt();
}

void AlarmsDialog::selectionChanged() {
    const bool selected = selectedId() > 0;
    m_editButton->setEnabled(selected);
    m_deleteButton->setEnabled(selected);
}

void AlarmsDialog::addAlarm() {
    AlarmEditDialog dialog(lexicon::AlarmRecord{}, this);
    if (dialog.exec() == QDialog::Accepted) reload(dialog.alarm().id);
}

void AlarmsDialog::editSelected() {
    const int id = selectedId();
    const auto found = std::find_if(m_alarms.begin(), m_alarms.end(), [id](const auto& alarm) { return alarm.id == id; });
    if (found == m_alarms.end()) return;
    AlarmEditDialog dialog(*found, this);
    if (dialog.exec() == QDialog::Accepted) reload(id);
}

void AlarmsDialog::deleteSelected(bool confirm) {
    const int id = selectedId();
    const auto found = std::find_if(m_alarms.begin(), m_alarms.end(), [id](const auto& alarm) { return alarm.id == id; });
    if (found == m_alarms.end()) return;
    if (confirm && QMessageBox::question(this, "Delete alarm",
                                         QString("Delete the alarm \"%1\"?").arg(qtbridge::toQt(found->title))) !=
                       QMessageBox::Yes)
        return;
    if (auto deleted = services().core.alarms.deleteAlarm(id); !deleted) {
        QMessageBox::critical(this, "Delete alarm", qtbridge::toQt(deleted.error().message));
        return;
    }
    reload();
}
