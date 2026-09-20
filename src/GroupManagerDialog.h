#pragma once

#include "DatabaseManager.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;

class GroupManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit GroupManagerDialog(QWidget* parent = nullptr);

signals:
    void groupsChanged();

private slots:
    void loadGroups();
    void addGroup();
    void editGroup();
    void deleteGroup();
    void selectionChanged();

private:
    void setupUi();
    bool promptForGroup(GroupRecord& group, bool isEdit);

    QList<GroupRecord> m_groups;
    QListWidget* m_list = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
