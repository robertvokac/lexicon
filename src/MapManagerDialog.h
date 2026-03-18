#pragma once

#include "DatabaseManager.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QPushButton;
class QTextEdit;

class MapManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit MapManagerDialog(QWidget* parent = nullptr);

signals:
    void mapsChanged();

private slots:
    void loadMaps();
    void addMap();
    void editMap();
    void deleteMap();
    void selectionChanged();

private:
    void setupUi();
    bool promptForMap(MapRecord& map, bool isEdit);

    QList<MapRecord> m_maps;
    QListWidget* m_list = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
