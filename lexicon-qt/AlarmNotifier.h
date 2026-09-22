#pragma once

#include "ApplicationContext.h"

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include <vector>

class QSystemTrayIcon;
class QVBoxLayout;
class QLabel;

// The alarms that have gone off, each with Dismiss and Snooze. It stays on
// top and is modal to the application: shown while another modal dialog is
// open - the item editor, the alarm list - it would otherwise be blocked by
// that dialog and cover it, and the whole program would seem frozen. As the
// most recently shown modal window it takes the input; Later hides it.
class AlarmRingDialog : public QDialog {
    Q_OBJECT

public:
    explicit AlarmRingDialog(QWidget* parent = nullptr);
    void setAlarms(const std::vector<lexicon::AlarmRecord>& alarms);
    int alarmCount() const { return static_cast<int>(m_alarms.size()); }

signals:
    // An alarm was dismissed or snoozed here.
    void alarmsChanged();

private:
    void dismiss(int alarmId);
    void snooze(int alarmId, int minutes);
    void dismissAll();

    QVBoxLayout* m_list = nullptr;
    QLabel* m_error = nullptr;
    std::vector<lexicon::AlarmRecord> m_alarms;
};

// Rings alarms while the desktop client runs. Every few seconds it asks the
// database which alarms have gone off and are not dismissed - a dismissal in
// the web client or the Android app counts too - and shows them in an
// AlarmRingDialog, with a notification in the system tray where there is one.
class AlarmNotifier : public QObject {
    Q_OBJECT

public:
    // [window] is alerted and owns the dialog.
    explicit AlarmNotifier(QWidget* window, int intervalMs = 15000);
    // Looks now; the timer calls it too.
    void check();
    AlarmRingDialog* dialog() const { return m_dialog; }
    // How many notifications were announced, for tests.
    int announcements() const { return m_announcements; }

private:
    void announce(const std::vector<lexicon::AlarmRecord>& fresh);

    QWidget* m_window;
    QTimer m_timer;
    QPointer<AlarmRingDialog> m_dialog;
    QSystemTrayIcon* m_tray = nullptr;
    // "id@firesAt" of alarms already announced, with when: a snoozed alarm
    // rings again, and one left open is announced again after a while.
    QHash<QString, QDateTime> m_announced;
    int m_announcements = 0;
};
