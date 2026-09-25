#pragma once

#include "ApplicationContext.h"

#include <QDateTime>
#include <QDialog>

#include <vector>

class QDateTimeEdit;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QSpinBox;

namespace alarmtime {
// An alarm's UTC "YYYY-MM-DDTHH:MM:SSZ" as a local date and time, and back.
QDateTime fromUtcText(const std::string& text);
std::string toUtcText(const QDateTime& local);
} // namespace alarmtime

// One alarm: a title, what it is about, and when it goes off, in local time.
class AlarmEditDialog : public QDialog {
    Q_OBJECT

public:
    // A new alarm when its id is -1.
    explicit AlarmEditDialog(const lexicon::AlarmRecord& alarm, QWidget* parent = nullptr);
    // The alarm as saved.
    const lexicon::AlarmRecord& alarm() const { return m_alarm; }

private:
    void save();

    lexicon::AlarmRecord m_alarm;
    QLineEdit* m_title = nullptr;
    QPlainTextEdit* m_description = nullptr;
    QDateTimeEdit* m_firesAt = nullptr;
    QSpinBox* m_repeatDays = nullptr;
    QComboBox* m_item = nullptr;
    QLabel* m_error = nullptr;
};

// Every alarm, the soonest first, to add, change and delete.
class AlarmsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AlarmsDialog(QWidget* parent = nullptr);
    int alarmCount() const { return static_cast<int>(m_alarms.size()); }
    void selectAlarm(int alarmId);
    // Deletes the selected alarm, asking first unless told not to.
    void deleteSelected(bool confirm = true);

private:
    void reload(int selectId = -1);
    void addAlarm();
    void editSelected();
    void selectionChanged();
    int selectedId() const;

    QTableWidget* m_table = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    std::vector<lexicon::AlarmRecord> m_alarms;
};
