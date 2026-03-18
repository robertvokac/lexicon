#pragma once

#include "DatabaseManager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

class TermEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit TermEditDialog(QWidget* parent = nullptr);

    void setMaps(const QList<MapRecord>& maps);
    void setTerm(const TermRecord& term);
    TermRecord term() const;

private slots:
    void addAlias();
    void editAlias();
    void removeAlias();

    void addTag();
    void editTag();
    void removeTag();

    void addFlag();
    void editFlag();
    void removeFlag();

    void validateAndAccept();

private:
    void setupUi();
    void connectSignals();
    void addValue(QListWidget* list, const QString& title);
    void editValue(QListWidget* list, const QString& title);
    void removeValue(QListWidget* list, const QString& title);
    static QStringList valuesFromList(QListWidget* list);
    static void setListValues(QListWidget* list, const QStringList& values);

    int m_termId = -1;

    QComboBox* m_mapCombo = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    QLineEdit* m_disambiguationEdit = nullptr;
    QCheckBox* m_obsidianCheck = nullptr;

    QListWidget* m_aliasList = nullptr;
    QListWidget* m_tagList = nullptr;
    QListWidget* m_flagList = nullptr;

    QPushButton* m_saveButton = nullptr;
};
