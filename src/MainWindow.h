#pragma once

#include "DatabaseManager.h"

#include <QMainWindow>

class QComboBox;
class QCompleter;
class QLineEdit;
class QStandardItemModel;
class QTableView;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void refreshAll();
    void refreshMaps();
    void refreshTerms();
    void refreshSuggestions();

    void addTerm();
    void editSelectedTerm();
    void deleteSelectedTerm();

    void openMapManager();
    void showTagsOverview();
    void showFlagsOverview();
    void showAliasesOverview();

    void updateActions();

private:
    void setupUi();
    void setupMenus();
    int selectedTermId() const;
    QList<MapRecord> maps() const;
    void showError(const QString& message);

    QComboBox* m_mapFilter = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTableView* m_tableView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QCompleter* m_completer = nullptr;


    QList<MapRecord> m_maps;
};
