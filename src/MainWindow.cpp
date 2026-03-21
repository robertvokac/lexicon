#include "MainWindow.h"

#include "MapManagerDialog.h"
#include "TermEditDialog.h"
#include "ValueListDialog.h"

#include "MarkdownConverter.h"
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
#include <QCloseEvent>
#include <QSettings>
#include <QPalette>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyle>
#include <QTableView>
#include <QTextEdit>
#include <QTextBrowser>
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
    setWindowTitle("Lexicon");
    resize(1200, 720);

    auto* centralWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(centralWidget);

    auto* filterRowLayout = new QHBoxLayout();
    m_mapFilter = new QComboBox(centralWidget);
    m_tagFilter = new QComboBox(centralWidget);
    m_flagFilter = new QComboBox(centralWidget);
    m_statusFilter = new QComboBox(centralWidget);
    m_statusFilter->addItem("All Statuses", -1);
    m_statusFilter->addItem("None", static_cast<int>(TermStatus::None));
    m_statusFilter->addItem("Draft", static_cast<int>(TermStatus::Draft));
    m_statusFilter->addItem("Completed", static_cast<int>(TermStatus::Completed));

    m_understandingFilter = new QComboBox(centralWidget);
    m_understandingFilter->addItem("All Levels", -1);
    m_understandingFilter->addItem("Unknown", static_cast<int>(UnderstandingLevel::Unknown));
    m_understandingFilter->addItem("Recognized", static_cast<int>(UnderstandingLevel::Recognized));
    m_understandingFilter->addItem("Understood", static_cast<int>(UnderstandingLevel::Understood));
    m_understandingFilter->addItem("Practiced", static_cast<int>(UnderstandingLevel::Practiced));
    m_understandingFilter->addItem("Mastered", static_cast<int>(UnderstandingLevel::Mastered));

    m_pinnedFilter = new QComboBox(centralWidget);
    m_pinnedFilter->addItem("All Pinned", -1);
    m_pinnedFilter->addItem("Pinned", 1);
    m_pinnedFilter->addItem("Not Pinned", 0);

    m_understandingFilter->setItemData(1, "Never encountered", Qt::ToolTipRole);
    m_understandingFilter->setItemData(2, "Seen before, can identify", Qt::ToolTipRole);
    m_understandingFilter->setItemData(3, "Conceptually grasped", Qt::ToolTipRole);
    m_understandingFilter->setItemData(4, "Can apply in real situations", Qt::ToolTipRole);
    m_understandingFilter->setItemData(5, "Fully internalized, can teach or innovate", Qt::ToolTipRole);

    filterRowLayout->addWidget(new QLabel("Map:", centralWidget));
    filterRowLayout->addWidget(m_mapFilter);
    filterRowLayout->addWidget(new QLabel("Tag:", centralWidget));
    filterRowLayout->addWidget(m_tagFilter);
    filterRowLayout->addWidget(new QLabel("Flag:", centralWidget));
    filterRowLayout->addWidget(m_flagFilter);
    filterRowLayout->addWidget(new QLabel("Status:", centralWidget));
    filterRowLayout->addWidget(m_statusFilter);
    filterRowLayout->addWidget(new QLabel("Understanding:", centralWidget));
    filterRowLayout->addWidget(m_understandingFilter);
    filterRowLayout->addWidget(new QLabel("Pinned:", centralWidget));
    filterRowLayout->addWidget(m_pinnedFilter);
    filterRowLayout->addStretch(1);

    auto* searchRowLayout = new QHBoxLayout();
    m_searchEdit = new QLineEdit(centralWidget);
    m_searchEdit->setPlaceholderText("Search title, disambiguation, alias, tag, or flag...");

    auto* quickAddButton = new QPushButton("Add", centralWidget);
    auto* addButton = new QPushButton("Add ...", centralWidget);
    auto* editButton = new QPushButton("Edit", centralWidget);
    auto* deleteButton = new QPushButton("Delete", centralWidget);
    editButton->setObjectName("editButton");
    deleteButton->setObjectName("deleteButton");

    searchRowLayout->addWidget(new QLabel("Search:", centralWidget));
    searchRowLayout->addWidget(m_searchEdit);
    m_searchEdit->setMinimumWidth(400);
    m_searchEdit->setMaximumWidth(600);
    searchRowLayout->addWidget(quickAddButton);
    searchRowLayout->addWidget(addButton);
    searchRowLayout->addWidget(editButton);
    searchRowLayout->addWidget(deleteButton);
    searchRowLayout->addStretch(1);

    rootLayout->addLayout(filterRowLayout);
    rootLayout->addLayout(searchRowLayout);

    m_tableView = new QTableView(centralWidget);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Id", "Map", "Title", "Disambiguation", "Tags", "Flags", "Aliases", "Status", "Understanding", "Pinned"});
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
    connect(m_tableView->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, &MainWindow::resetPaginationAndRefresh);
    rootLayout->addWidget(m_tableView, 1);

    auto* paginationLayout = new QHBoxLayout();
    m_firstButton = new QPushButton("<< First", centralWidget);
    m_prevButton = new QPushButton("< Prev", centralWidget);
    m_nextButton = new QPushButton("Next >", centralWidget);
    m_lastButton = new QPushButton("Last >>", centralWidget);
    m_pageLabel = new QLabel("Page 1", centralWidget);
    m_pageSizeCombo = new QComboBox(centralWidget);
    m_pageSizeCombo->addItems({"10", "20", "50", "100"});
    int sizeIdx = m_pageSizeCombo->findText(QString::number(m_pageSize));
    if (sizeIdx >= 0) m_pageSizeCombo->setCurrentIndex(sizeIdx);

    paginationLayout->addWidget(m_firstButton);
    paginationLayout->addWidget(m_prevButton);
    paginationLayout->addWidget(m_pageLabel);
    paginationLayout->addWidget(m_nextButton);
    paginationLayout->addWidget(m_lastButton);
    paginationLayout->addStretch();
    paginationLayout->addWidget(new QLabel("Page size:", centralWidget));
    paginationLayout->addWidget(m_pageSizeCombo);

    rootLayout->addLayout(paginationLayout);

    m_termContentView = new QTextEdit(centralWidget);
    m_termContentView->setReadOnly(true);
    m_highlighter = new CodeHighlighter(m_termContentView->document());
    m_termContentView->setPlaceholderText("Select a term to view content...");
    m_termContentView->setMinimumHeight(150);

    m_linksView = new QTextBrowser(centralWidget);
    m_linksView->setReadOnly(true);
    m_linksView->setOpenLinks(false);
    m_linksView->setMaximumHeight(100);
    m_linksHighlighter = new CodeHighlighter(m_linksView->document());
    connect(m_linksView, &QTextBrowser::anchorClicked, this, &MainWindow::onLinkActivated);

    updateMarkdownStyles();

    rootLayout->addWidget(m_termContentView, 1);
    rootLayout->addWidget(m_linksView, 0);

    setCentralWidget(centralWidget);

    auto* completerModel = new QStringListModel(this);
    m_completer = new QCompleter(completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_searchEdit->setCompleter(m_completer);

    connect(m_mapFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_tagFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_flagFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_statusFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_understandingFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_pinnedFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::resetPaginationAndRefresh);
    connect(m_firstButton, &QPushButton::clicked, this, &MainWindow::firstPage);
    connect(m_prevButton, &QPushButton::clicked, this, &MainWindow::prevPage);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::nextPage);
    connect(m_lastButton, &QPushButton::clicked, this, &MainWindow::lastPage);
    connect(m_pageSizeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_pageSize = m_pageSizeCombo->itemText(index).toInt();
        saveSettings();
        resetPaginationAndRefresh();
    });
    connect(quickAddButton, &QPushButton::clicked, this, &MainWindow::quickAdd);
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addTerm);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editSelectedTerm);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedTerm);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection&, const QItemSelection&) {
        updateActions();
        auto indexes = m_tableView->selectionModel()->selectedRows();
        if (!indexes.isEmpty()) {
            showTermContent(indexes.first());
        } else {
            m_termContentView->clear();
        }
    });
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
    int mapId = m_mapFilter->currentData().toInt();
    QString tagFilter = m_tagFilter->currentIndex() > 0 ? m_tagFilter->currentText() : QString();
    QString flagFilter = m_flagFilter->currentIndex() > 0 ? m_flagFilter->currentText() : QString();
    int understandingFilter = m_understandingFilter->currentData().toInt();
    int statusFilter = m_statusFilter->currentData().toInt();
    int pinnedFilter = m_pinnedFilter->currentData().toInt();
    QString searchText = m_searchEdit->text().trimmed();

    // If we're restoring the last term at startup
    if (m_lastTermId != -1 && searchText.isEmpty()) {
        TermRecord lastTerm;
        if (DatabaseManager::loadTerm(m_lastTermId, lastTerm, &error)) {
            m_searchEdit->blockSignals(true);
            m_searchEdit->setText(lastTerm.title);
            m_searchEdit->blockSignals(false);
            searchText = lastTerm.title;
        }
    }
    
    int totalCount = DatabaseManager::countTerms(mapId, searchText, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    int totalPages = std::max(1, (totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage >= totalPages) m_currentPage = totalPages - 1;
    if (m_currentPage < 0) m_currentPage = 0;

    const int sortSection = m_tableView->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_tableView->horizontalHeader()->sortIndicatorOrder();

    const auto terms = DatabaseManager::loadTerms(mapId, searchText, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, m_pageSize, m_currentPage * m_pageSize, sortSection, sortOrder, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    m_model->removeRows(0, m_model->rowCount());
    m_model->setRowCount(0); // Ensure it's clean
    int rowToSelect = -1;
    for (int i = 0; i < terms.size(); ++i) {
        const auto& term = terms[i];
        if (m_lastTermId != -1 && term.id == m_lastTermId) {
            rowToSelect = i;
        }
        QList<QStandardItem*> row;
        auto* idItem = new QStandardItem();
        idItem->setData(term.id, Qt::DisplayRole);
        idItem->setData(term.id, Qt::UserRole);
        row << idItem
            << new QStandardItem(term.mapName)
            << new QStandardItem(term.title)
            << new QStandardItem(term.disambiguation)
            << new QStandardItem(term.tags.join(", "))
            << new QStandardItem(term.flags.join(", "))
            << new QStandardItem(term.aliases.join(", "));
        
        QString statusText;
        switch (term.status) {
            case TermStatus::Draft:     statusText = "Draft"; break;
            case TermStatus::Completed: statusText = "Completed"; break;
            default:                    statusText = "None"; break;
        }
        row << new QStandardItem(statusText);

        QString understandingText;
        switch (term.understanding) {
            case UnderstandingLevel::Recognized: understandingText = "Recognized"; break;
            case UnderstandingLevel::Understood: understandingText = "Understood"; break;
            case UnderstandingLevel::Practiced:  understandingText = "Practiced"; break;
            case UnderstandingLevel::Mastered:   understandingText = "Mastered"; break;
            default:                             understandingText = "Unknown"; break;
        }
        row << new QStandardItem(understandingText);
        row << new QStandardItem(term.pinned ? "Yes" : "No");
        m_model->appendRow(row);
    }

    m_tableView->setColumnHidden(0, false);
    m_tableView->resizeColumnsToContents();
    m_tableView->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Fixed);
    m_tableView->setColumnWidth(9, 60);

    m_pageLabel->setText(QString("Page %1 of %2 (%3 total)").arg(m_currentPage + 1).arg(totalPages).arg(totalCount));
    m_firstButton->setEnabled(m_currentPage > 0);
    m_prevButton->setEnabled(m_currentPage > 0);
    m_nextButton->setEnabled(m_currentPage < totalPages - 1);
    m_lastButton->setEnabled(m_currentPage < totalPages - 1);

    if (rowToSelect != -1) {
        m_tableView->selectRow(rowToSelect);
        m_lastTermId = -1; // Reset so it doesn't keep selecting on every refresh
    }

    updateActions();
}

void MainWindow::resetPaginationAndRefresh() {
    m_currentPage = 0;
    refreshTerms();
}

void MainWindow::firstPage() {
    if (m_currentPage > 0) {
        m_currentPage = 0;
        refreshTerms();
    }
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

void MainWindow::lastPage() {
    QString error;
    const int mapId = m_mapFilter->currentData().toInt();
    const QString tagFilter = m_tagFilter->currentIndex() > 0 ? m_tagFilter->currentText() : QString();
    const QString flagFilter = m_flagFilter->currentIndex() > 0 ? m_flagFilter->currentText() : QString();
    const int understandingFilter = m_understandingFilter->currentData().toInt();
    const int statusFilter = m_statusFilter->currentData().toInt();
    const int pinnedFilter = m_pinnedFilter->currentData().toInt();
    const QString searchText = m_searchEdit->text().trimmed();

    int totalCount = DatabaseManager::countTerms(mapId, searchText, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    int totalPages = std::max(1, (totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage < totalPages - 1) {
        m_currentPage = totalPages - 1;
        refreshTerms();
    }
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

void MainWindow::showTermContent(const QModelIndex& index) {
    if (!index.isValid()) {
        m_termContentView->clear();
        m_linksView->clear();
        return;
    }

    const int termId = m_model->data(m_model->index(index.row(), 0), Qt::UserRole).toInt();
    TermRecord term;
    QString error;
    if (DatabaseManager::loadTerm(termId, term, &error)) {
        m_termContentView->setHtml(MarkdownConverter::toHtml(term.content));
        updateLinksDisplay(termId);
        DatabaseManager::logTermRead(termId);
    } else {
        m_termContentView->setPlainText("Error loading content: " + error);
        m_linksView->clear();
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
        palette.setColor(QPalette::Window, QColor(53, 53, 53));
        palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
        palette.setColor(QPalette::Base, QColor(43, 43, 43));
        palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
        palette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
        palette.setColor(QPalette::Text, QColor(230, 230, 230));
        palette.setColor(QPalette::Button, QColor(63, 63, 63));
        palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(42, 130, 218));
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
        palette.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));
        palette.setColor(QPalette::Mid, QColor(100, 100, 100));
        palette.setColor(QPalette::Dark, QColor(120, 120, 120)); // Ensure it's lighter than Base in dark theme or at least visible
        app->setPalette(palette);
        app->setStyleSheet(
            "QToolTip { color: #e6e6e6; background-color: #353535; border: 1px solid #555; }"
            "QTableView { gridline-color: #555; }"
            "QHeaderView::section { background-color: #3f3f3f; color: #e6e6e6; padding: 4px; border: 1px solid #555; }"
        );
    }
    updateMarkdownStyles();
}

void MainWindow::updateMarkdownStyles() {
    if (!m_termContentView) return;

    QPalette pal = palette();
    QString bgColor = pal.color(QPalette::Base).name();
    QString textColor = pal.color(QPalette::Text).name();
    QString borderColor = pal.color(QPalette::Text).name(); // Use Text color for borders for maximum visibility
    QString headerBgColor = pal.color(QPalette::AlternateBase).name();
    QString codeBgColor = pal.color(QPalette::AlternateBase).name();
    QString linkColor = pal.color(QPalette::Link).name();
    if (pal.color(QPalette::Window).lightness() < 128) {
        // In dark theme, use pure black for code blocks
        codeBgColor = "#000000";
    }

    m_termContentView->setStyleSheet(QString("QTextEdit[readOnly=\"true\"] { background-color: %1; color: %2; }").arg(bgColor, textColor));
    m_linksView->setStyleSheet(QString("QTextEdit[readOnly=\"true\"] { background-color: %1; color: %2; }").arg(bgColor, textColor));
    
    QString style = QString(
        "table { border: 1px solid %2; margin-top: 10px; margin-bottom: 10px; border-collapse: collapse; } "
        "th { background-color: %3; color: %1; font-weight: bold; border: 1px solid %2; padding: 4px; text-align: left; }"
        "td { color: %1; border: 1px solid %2; padding: 4px; text-align: left; }"
        "pre { background-color: %4; border: 1px solid %2; padding: 8px; border-radius: 4px; font-family: 'Courier New', monospace; white-space: pre; } "
        "pre[syntax=\"cpp\"] { -qt-user-property-1: \"cpp\"; } "
        "code { background-color: %4; font-family: 'Courier New', monospace; }"
        "body { color: %1; }"
        "a { color: %5; }"
    ).arg(textColor, borderColor, headerBgColor, codeBgColor, linkColor);

    m_termContentView->document()->setDefaultStyleSheet(style);
    m_linksView->document()->setDefaultStyleSheet(style);
    
    // Force re-render of current content
    if (m_tableView->selectionModel()->hasSelection()) {
        showTermContent(m_tableView->currentIndex());
    }
}

void MainWindow::updateLinksDisplay(int termId) {
    if (termId == -1) {
        m_linksView->clear();
        return;
    }

    QString error;
    QList<LinkRecord> links = DatabaseManager::loadLinks(termId, &error);
    QList<LinkRecord> backlinks = DatabaseManager::loadBacklinks(termId, &error);

    auto linkTypeToString = [](LinkType type) -> QString {
        switch (type) {
            case LinkType::IsA: return "Is A";
            case LinkType::PartOf: return "Part Of";
            case LinkType::Uses: return "Uses";
            case LinkType::DependsOn: return "Depends On";
            case LinkType::Implements: return "Implements";
            case LinkType::Related: return "Related";
            case LinkType::Contrasts: return "Contrasts";
            case LinkType::AlternativeTo: return "Alternative To";
            default: return "Link";
        }
    };

    QString html = "<b>Links:</b> ";
    if (links.isEmpty()) {
        html += "None";
    } else {
        for (int i = 0; i < links.size(); ++i) {
            if (i > 0) html += ", ";
            html += QString("<a href=\"%1\">%2 (%3)</a>")
                        .arg(QUrl::toPercentEncoding(links[i].toTermTitle), links[i].toTermTitle, linkTypeToString(links[i].linkType));
        }
    }

    html += "<br><b>Backlinks:</b> ";
    if (backlinks.isEmpty()) {
        html += "None";
    } else {
        for (int i = 0; i < backlinks.size(); ++i) {
            if (i > 0) html += ", ";
            html += QString("<a href=\"%1\">%2 (%3)</a>")
                        .arg(QUrl::toPercentEncoding(backlinks[i].fromTermTitle), backlinks[i].fromTermTitle, linkTypeToString(backlinks[i].linkType));
        }
    }

    m_linksView->setHtml(html);
}

void MainWindow::onLinkActivated(const QUrl& link) {
    QString termTitle = QUrl::fromPercentEncoding(link.toString().toUtf8());

    m_mapFilter->blockSignals(true);
    m_tagFilter->blockSignals(true);
    m_flagFilter->blockSignals(true);
    m_statusFilter->blockSignals(true);
    m_understandingFilter->blockSignals(true);
    m_searchEdit->blockSignals(true);

    m_mapFilter->setCurrentIndex(0);
    m_tagFilter->setCurrentIndex(0);
    m_flagFilter->setCurrentIndex(0);
    m_statusFilter->setCurrentIndex(0);
    m_understandingFilter->setCurrentIndex(0);
    m_searchEdit->setText(termTitle);

    m_mapFilter->blockSignals(false);
    m_tagFilter->blockSignals(false);
    m_flagFilter->blockSignals(false);
    m_statusFilter->blockSignals(false);
    m_understandingFilter->blockSignals(false);
    m_searchEdit->blockSignals(false);

    resetPaginationAndRefresh();

    if (m_model->rowCount() > 0) {
        m_tableView->selectRow(0);
        m_tableView->setFocus();
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
    settings.setValue("state/lastTermId", selectedTermId());
}

void MainWindow::loadSettings() {
    QSettings settings;
    m_pageSize = settings.value("pagination/pageSize", 20).toInt();
    m_lastTermId = settings.value("state/lastTermId", -1).toInt();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}
