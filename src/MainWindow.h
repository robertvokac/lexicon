#pragma once

#include "DatabaseManager.h"

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

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
    void setLightTheme();
    void setDarkTheme();

    void refreshAll();
    void refreshMaps();
    void refreshTags();
    void refreshFlags();
    void refreshTerms();
    void resetPaginationAndRefresh();
    void firstPage();
    void prevPage();
    void nextPage();
    void lastPage();
    void refreshSuggestions();

    void addTerm();
    void quickAdd();
    void editSelectedTerm();
    void deleteSelectedTerm();

    void openMapManager();
    void showTagsOverview();
    void showFlagsOverview();
    void showAliasesOverview();

    void updateActions();

private:
    void applySavedTheme();
    void applyTheme(const QString& themeName);
    void saveSettings();
    void loadSettings();

    void setupUi();
    void setupMenus();
    int selectedTermId() const;
    QList<MapRecord> maps() const;
    void showError(const QString& message);

    QComboBox* m_mapFilter = nullptr;
    QComboBox* m_tagFilter = nullptr;
    QComboBox* m_flagFilter = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QTableView* m_tableView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QCompleter* m_completer = nullptr;

    QPushButton* m_firstButton = nullptr;
    QPushButton* m_prevButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QPushButton* m_lastButton = nullptr;
    QLabel* m_pageLabel = nullptr;
    QComboBox* m_pageSizeCombo = nullptr;

    int m_currentPage = 0;
    int m_pageSize = 20;

    QList<MapRecord> m_maps;
};
