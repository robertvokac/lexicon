#pragma once

#include "ApplicationContext.h"

#include <QDialog>

class QListWidget;
class QPushButton;

class ItemTypeManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ItemTypeManagerDialog(QWidget* parent = nullptr);

signals:
    void typesChanged();

private slots:
    void addType();
    void editType();
    void deleteType();
    void selectionChanged();
    void addField();
    void editField();
    void deleteField();
    void fieldSelectionChanged();

private:
    void loadTypes();
    void loadFields();
    bool promptForType(ItemTypeRecord& type, bool isEdit);
    bool promptForField(ItemFieldRecord& field, bool isEdit);

    QList<GroupRecord> m_groups;
    QList<ItemTypeRecord> m_types;
    QList<ItemFieldRecord> m_fields;
    QListWidget* m_list = nullptr;
    QListWidget* m_fieldList = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_addFieldButton = nullptr;
    QPushButton* m_editFieldButton = nullptr;
    QPushButton* m_deleteFieldButton = nullptr;
};
