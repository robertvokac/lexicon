#include "MainWindow.h"

#include "MapManagerDialog.h"
#include "TermEditDialog.h"
#include "ValueListDialog.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QPalette>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QItemSelectionModel>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    loadSettings();
    applySavedTheme();
    setupUi();
    setupMenus();
    refreshAll();
    updateActions();
}

void MainWindow::setupUi() {
    setWindowTitle("CoreLex");
    resize(1200, 720);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* filterLayout = new QHBoxLayout();
    m_mapFilter = new QComboBox(central);
    m_tagFilter = new QComboBox(central);
    m_flagFilter = new QComboBox(central);
    m_searchEdit = new QLineEdit(central);
    m_searchEdit->setPlaceholderText("Search title, disambiguation, alias, tag, or flag...");

    auto* quickAddButton = new QPushButton("Add", central);
    auto* addButton = new QPushButton("Add ...", central);
    auto* editButton = new QPushButton("Edit", central);
    auto* deleteButton = new QPushButton("Delete", central);
    editButton->setObjectName("editButton");
    deleteButton->setObjectName("deleteButton");

    filterLayout->addWidget(new QLabel("Map:", central));
    filterLayout->addWidget(m_mapFilter);
    filterLayout->addWidget(new QLabel("Tag:", central));
    filterLayout->addWidget(m_tagFilter);
    filterLayout->addWidget(new QLabel("Flag:", central));
    filterLayout->addWidget(m_flagFilter);
    filterLayout->addWidget(new QLabel("Search:", central));
    filterLayout->addWidget(m_searchEdit, 1);
    filterLayout->addWidget(quickAddButton);
    filterLayout->addWidget(addButton);
    filterLayout->addWidget(editButton);
    filterLayout->addWidget(deleteButton);

    rootLayout->addLayout(filterLayout);

    m_tableView = new QTableView(central);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Id", "Map", "Title", "Disambiguation", "Obsidian", "Aliases", "Tags", "Flags"});
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setSortingEnabled(true);
    m_tableView->horizontalHeader()->setSectionsClickable(true);
    m_tableView->horizontalHeader()->setSortIndicatorShown(true);
    rootLayout->addWidget(m_tableView, 1);

    auto* paginationLayout = new QHBoxLayout();
    m_prevButton = new QPushButton("< Prev", central);
    m_nextButton = new QPushButton("Next >", central);
    m_pageLabel = new QLabel("Page 1", central);
    m_pageSizeCombo = new QComboBox(central);
    m_pageSizeCombo->addItems({"10", "20", "50", "100"});
    int sizeIdx = m_pageSizeCombo->findText(QString::number(m_pageSize));
    if (sizeIdx >= 0) m_pageSizeCombo->setCurrentIndex(sizeIdx);

    paginationLayout->addWidget(m_prevButton);
    paginationLayout->addWidget(m_pageLabel);
    paginationLayout->addWidget(m_nextButton);
    paginationLayout->addStretch();
    paginationLayout->addWidget(new QLabel("Page size:", central));
    paginationLayout->addWidget(m_pageSizeCombo);

    rootLayout->addLayout(paginationLayout);

    setCentralWidget(central);

    auto* completerModel = new QStringListModel(this);
    m_completer = new QCompleter(completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_searchEdit->setCompleter(m_completer);

    connect(m_mapFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_tagFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_flagFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::resetPaginationAndRefresh);
    connect(m_prevButton, &QPushButton::clicked, this, &MainWindow::prevPage);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::nextPage);
    connect(m_pageSizeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_pageSize = m_pageSizeCombo->itemText(index).toInt();
        saveSettings();
        resetPaginationAndRefresh();
    });
    connect(quickAddButton, &QPushButton::clicked, this, &MainWindow::quickAdd);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addTerm);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editSelectedTerm);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedTerm);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    connect(m_tableView, &QTableView::doubleClicked, this, [this](const QModelIndex&) { editSelectedTerm(); });

}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("File");
    auto* refreshAction = fileMenu->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshAll);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* manageMenu = menuBar()->addMenu("Manage");
    auto* mapsAction = manageMenu->addAction("Maps...");
    connect(mapsAction, &QAction::triggered, this, &MainWindow::openMapManager);

    auto* viewMenu = menuBar()->addMenu("View");
    auto* tagsAction = viewMenu->addAction("All tags...");
    auto* flagsAction = viewMenu->addAction("All flags...");
    auto* aliasesAction = viewMenu->addAction("All aliases...");
    viewMenu->addSeparator();
    auto* lightThemeAction = viewMenu->addAction("Light mode");
    auto* darkThemeAction = viewMenu->addAction("Dark mode");
    connect(tagsAction, &QAction::triggered, this, &MainWindow::showTagsOverview);
    connect(flagsAction, &QAction::triggered, this, &MainWindow::showFlagsOverview);
    connect(aliasesAction, &QAction::triggered, this, &MainWindow::showAliasesOverview);
    connect(lightThemeAction, &QAction::triggered, this, &MainWindow::setLightTheme);
    connect(darkThemeAction, &QAction::triggered, this, &MainWindow::setDarkTheme);
}

void MainWindow::refreshAll() {
    refreshMaps();
    refreshTags();
    refreshFlags();
    refreshSuggestions();
    refreshTerms();
    updateActions();
}

void MainWindow::refreshMaps() {
    QString error;
    m_maps = DatabaseManager::loadMaps(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    const QVariant currentMap = m_mapFilter->currentData();

    m_mapFilter->blockSignals(true);
    m_mapFilter->clear();
    m_mapFilter->addItem("All maps", 0);
    for (const auto& map : m_maps) {
        m_mapFilter->addItem(map.name, map.id);
    }
    const int idx = m_mapFilter->findData(currentMap);
    if (idx >= 0) {
        m_mapFilter->setCurrentIndex(idx);
    }
    m_mapFilter->blockSignals(false);
}

void MainWindow::refreshTags() {
    QString error;
    auto tags = DatabaseManager::loadTagUsage(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    const QString currentTag = m_tagFilter->currentText();
    m_tagFilter->blockSignals(true);
    m_tagFilter->clear();
    m_tagFilter->addItem("All tags");
    for (const auto& tag : tags) {
        m_tagFilter->addItem(tag.value);
    }
    const int idx = m_tagFilter->findText(currentTag);
    if (idx >= 0) {
        m_tagFilter->setCurrentIndex(idx);
    }
    m_tagFilter->blockSignals(false);
}

void MainWindow::refreshFlags() {
    QString error;
    auto flags = DatabaseManager::loadFlagUsage(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    const QString currentFlag = m_flagFilter->currentText();
    m_flagFilter->blockSignals(true);
    m_flagFilter->clear();
    m_flagFilter->addItem("All flags");
    for (const auto& flag : flags) {
        m_flagFilter->addItem(flag.value);
    }
    const int idx = m_flagFilter->findText(currentFlag);
    if (idx >= 0) {
        m_flagFilter->setCurrentIndex(idx);
    }
    m_flagFilter->blockSignals(false);
}

void MainWindow::refreshTerms() {
    QString error;
    const int mapId = m_mapFilter->currentData().toInt();
    const QString tagFilter = m_tagFilter->currentIndex() > 0 ? m_tagFilter->currentText() : QString();
    const QString flagFilter = m_flagFilter->currentIndex() > 0 ? m_flagFilter->currentText() : QString();
    const QString searchText = m_searchEdit->text().trimmed();
    
    int totalCount = DatabaseManager::countTerms(mapId, searchText, tagFilter, flagFilter, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    int totalPages = std::max(1, (totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage >= totalPages) m_currentPage = totalPages - 1;
    if (m_currentPage < 0) m_currentPage = 0;

    const auto terms = DatabaseManager::loadTerms(mapId, searchText, tagFilter, flagFilter, m_pageSize, m_currentPage * m_pageSize, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    m_model->removeRows(0, m_model->rowCount());
    for (const auto& term : terms) {
        QList<QStandardItem*> row;
        auto* idItem = new QStandardItem();
        idItem->setData(term.id, Qt::DisplayRole);
        idItem->setData(term.id, Qt::UserRole);
        row << idItem
            << new QStandardItem(term.mapName)
            << new QStandardItem(term.title)
            << new QStandardItem(term.disambiguation)
            << new QStandardItem(term.obsidian ? "Yes" : "No")
            << new QStandardItem(term.aliases.join(", "))
            << new QStandardItem(term.tags.join(", "))
            << new QStandardItem(term.flags.join(", "));
        m_model->appendRow(row);
    }

    m_tableView->setColumnHidden(0, false);
    m_tableView->resizeColumnsToContents();
    const int sortSection = m_tableView->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_tableView->horizontalHeader()->sortIndicatorOrder();
    if (sortSection >= 0) {
        m_tableView->sortByColumn(sortSection, sortOrder);
    }

    m_pageLabel->setText(QString("Page %1 of %2 (%3 total)").arg(m_currentPage + 1).arg(totalPages).arg(totalCount));
    m_prevButton->setEnabled(m_currentPage > 0);
    m_nextButton->setEnabled(m_currentPage < totalPages - 1);

    updateActions();
}

void MainWindow::resetPaginationAndRefresh() {
    m_currentPage = 0;
    refreshTerms();
}

void MainWindow::prevPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        refreshTerms();
    }
}

void MainWindow::nextPage() {
    m_currentPage++;
    refreshTerms();
}

void MainWindow::refreshSuggestions() {
    QString error;
    const QStringList suggestions = DatabaseManager::loadSuggestions(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    auto* model = qobject_cast<QStringListModel*>(m_completer->model());
    if (model) {
        model->setStringList(suggestions);
    }
}

int MainWindow::selectedTermId() const {
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return -1;
    }
    const QModelIndex index = indexes.first();
    return m_model->item(index.row(), 0)->data(Qt::UserRole).toInt();
}

QList<MapRecord> MainWindow::maps() const {
    return m_maps;
}

void MainWindow::addTerm() {
    if (m_maps.isEmpty()) {
        QMessageBox::information(this, "Lexicon", "Create a map first.");
        openMapManager();
        if (m_maps.isEmpty()) {
            return;
        }
    }

    TermEditDialog dialog(this);
    dialog.setMaps(m_maps);
    TermRecord draft;
    if (m_mapFilter->currentData().toInt() > 0) {
        draft.mapId = m_mapFilter->currentData().toInt();
    }
    draft.title = m_searchEdit->text().trimmed();
    dialog.setTerm(draft);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString error;
    if (!DatabaseManager::saveTerm(dialog.term(), &error)) {
        showError(error);
        return;
    }
    refreshAll();
}

void MainWindow::quickAdd() {
    const QString text = m_searchEdit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    if (m_maps.isEmpty()) {
        QMessageBox::information(this, "Lexicon", "Create a map first.");
        openMapManager();
        if (m_maps.isEmpty()) {
            return;
        }
    }

    TermRecord term;
    term.title = text;
    
    // Choose map: current filter or first available
    int mapId = m_mapFilter->currentData().toInt();
    if (mapId <= 0 && !m_maps.isEmpty()) {
        mapId = m_maps.first().id;
    }
    
    if (mapId <= 0) {
         QMessageBox::warning(this, "Lexicon", "No map available for quick add.");
         return;
    }
    
    term.mapId = mapId;

    QString error;
    if (!DatabaseManager::saveTerm(term, &error)) {
        showError(error);
        return;
    }

    m_searchEdit->clear();
    refreshAll();
}

void MainWindow::editSelectedTerm() {
    const int termId = selectedTermId();
    if (termId < 0) {
        return;
    }

    TermRecord term;
    QString error;
    if (!DatabaseManager::loadTerm(termId, term, &error)) {
        showError(error);
        return;
    }

    TermEditDialog dialog(this);
    dialog.setMaps(m_maps);
    dialog.setTerm(term);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!DatabaseManager::saveTerm(dialog.term(), &error)) {
        showError(error);
        return;
    }
    refreshAll();
}

void MainWindow::deleteSelectedTerm() {
    const int termId = selectedTermId();
    if (termId < 0) {
        return;
    }

    const int row = m_tableView->selectionModel()->selectedRows().first().row();
    const QString title = m_model->item(row, 2)->text();
    const auto answer = QMessageBox::question(this, "Delete term", QString("Delete term '%1'?").arg(title));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!DatabaseManager::deleteTerm(termId, &error)) {
        showError(error);
        return;
    }
    refreshAll();
}

void MainWindow::openMapManager() {
    MapManagerDialog dialog(this);
    connect(&dialog, &MapManagerDialog::mapsChanged, this, &MainWindow::refreshAll);
    dialog.exec();
    refreshAll();
}

void MainWindow::showTagsOverview() {
    QString error;
    auto values = DatabaseManager::loadTagUsage(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    ValueListDialog dialog("All tags", this);
    dialog.setValues(values, "Tag");
    dialog.exec();
}

void MainWindow::showFlagsOverview() {
    QString error;
    auto values = DatabaseManager::loadFlagUsage(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    ValueListDialog dialog("All flags", this);
    dialog.setValues(values, "Flag");
    dialog.exec();
}

void MainWindow::showAliasesOverview() {
    QString error;
    auto values = DatabaseManager::loadAliasUsage(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    ValueListDialog dialog("All aliases", this);
    dialog.setValues(values, "Alias");
    dialog.exec();
}

void MainWindow::updateActions() {
    const bool hasSelection = selectedTermId() >= 0;
    const auto editButtons = findChildren<QPushButton*>(QString(), Qt::FindChildrenRecursively);
    for (auto* button : editButtons) {
        if (button->text() == "Edit") {
            button->setEnabled(hasSelection);
        }
        if (button->text() == "Delete") {
            button->setEnabled(hasSelection);
        }
    }
}


void MainWindow::applySavedTheme() {
    QSettings settings;
    applyTheme(settings.value("appearance/theme", "dark").toString());
}

void MainWindow::applyTheme(const QString& themeName) {
    auto* app = qApp;
    app->setStyleSheet(QString());
    if (themeName.compare("light", Qt::CaseInsensitive) == 0) {
        app->setPalette(app->style()->standardPalette());
    } else {
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(45, 45, 48));
        palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
        palette.setColor(QPalette::Base, QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        palette.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
        palette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
        palette.setColor(QPalette::Text, QColor(230, 230, 230));
        palette.setColor(QPalette::Button, QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        palette.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));
        app->setPalette(palette);
        app->setStyleSheet(
            "QToolTip { color: #e6e6e6; background-color: #2d2d30; border: 1px solid #555; }"
            "QTableView { gridline-color: #555; }"
            "QHeaderView::section { background-color: #353535; color: #e6e6e6; padding: 4px; border: 1px solid #555; }"
        );
    }
}

void MainWindow::setLightTheme() {
    QSettings settings;
    settings.setValue("appearance/theme", "light");
    applyTheme("light");
}

void MainWindow::setDarkTheme() {
    QSettings settings;
    settings.setValue("appearance/theme", "dark");
    applyTheme("dark");
}

void MainWindow::showError(const QString& message) {
    QMessageBox::critical(this, "Lexicon", message);
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.setValue("pagination/pageSize", m_pageSize);
}

void MainWindow::loadSettings() {
    QSettings settings;
    m_pageSize = settings.value("pagination/pageSize", 20).toInt();
}
