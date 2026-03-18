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
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QItemSelectionModel>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    setupMenus();
    refreshAll();
}

void MainWindow::setupUi() {
    setWindowTitle("CoreLex");
    resize(1200, 720);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* filterLayout = new QHBoxLayout();
    m_mapFilter = new QComboBox(central);
    m_searchEdit = new QLineEdit(central);
    m_searchEdit->setPlaceholderText("Search title, disambiguation, alias, tag, or flag...");

    auto* addButton = new QPushButton("New term", central);
    auto* editButton = new QPushButton("Edit", central);
    auto* deleteButton = new QPushButton("Delete", central);
    editButton->setObjectName("editButton");
    deleteButton->setObjectName("deleteButton");

    filterLayout->addWidget(new QLabel("Map:", central));
    filterLayout->addWidget(m_mapFilter);
    filterLayout->addWidget(new QLabel("Search:", central));
    filterLayout->addWidget(m_searchEdit, 1);
    filterLayout->addWidget(addButton);
    filterLayout->addWidget(editButton);
    filterLayout->addWidget(deleteButton);

    rootLayout->addLayout(filterLayout);

    m_tableView = new QTableView(central);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Id", "Title", "Disambiguation", "Map", "Obsidian", "Aliases", "Tags", "Flags"});
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setSortingEnabled(false);
    rootLayout->addWidget(m_tableView, 1);

    setCentralWidget(central);

    auto* completerModel = new QStringListModel(this);
    m_completer = new QCompleter(completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_searchEdit->setCompleter(m_completer);

    connect(m_mapFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::refreshTerms);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::refreshTerms);
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
    connect(tagsAction, &QAction::triggered, this, &MainWindow::showTagsOverview);
    connect(flagsAction, &QAction::triggered, this, &MainWindow::showFlagsOverview);
    connect(aliasesAction, &QAction::triggered, this, &MainWindow::showAliasesOverview);
}

void MainWindow::refreshAll() {
    refreshMaps();
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

void MainWindow::refreshTerms() {
    QString error;
    const int mapId = m_mapFilter->currentData().toInt();
    const auto terms = DatabaseManager::loadTerms(mapId, m_searchEdit->text(), &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    m_model->removeRows(0, m_model->rowCount());
    for (const auto& term : terms) {
        QList<QStandardItem*> row;
        auto* idItem = new QStandardItem(QString::number(term.id));
        idItem->setData(term.id, Qt::UserRole);
        row << idItem
            << new QStandardItem(term.title)
            << new QStandardItem(term.disambiguation)
            << new QStandardItem(term.mapName)
            << new QStandardItem(term.obsidian ? "Yes" : "No")
            << new QStandardItem(term.aliases.join(", "))
            << new QStandardItem(term.tags.join(", "))
            << new QStandardItem(term.flags.join(", "));
        m_model->appendRow(row);
    }

    m_tableView->setColumnHidden(0, true);
    m_tableView->resizeColumnsToContents();
    updateActions();
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
        QMessageBox::information(this, "CoreLex", "Create a map first.");
        openMapManager();
        if (m_maps.isEmpty()) {
            return;
        }
    }

    TermEditDialog dialog(this);
    dialog.setMaps(m_maps);
    if (m_mapFilter->currentData().toInt() > 0) {
        TermRecord draft;
        draft.mapId = m_mapFilter->currentData().toInt();
        dialog.setTerm(draft);
    }

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
    const QString title = m_model->item(row, 1)->text();
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

void MainWindow::showError(const QString& message) {
    QMessageBox::critical(this, "CoreLex", message);
}
