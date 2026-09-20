#include "MainWindow.h"

#include "GroupManagerDialog.h"
#include "ItemTypeManagerDialog.h"
#include "ItemEditDialog.h"
#include "ValueListDialog.h"

#include "MarkdownConverter.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QCloseEvent>
#include <QSettings>
#include <QScrollArea>
#include <QSignalBlocker>
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
    m_groupFilter = new QComboBox(centralWidget);
    m_typeFilter = new QComboBox(centralWidget);
    m_typeFilter->addItem("All types", 0);
    m_tagFilter = new QComboBox(centralWidget);
    m_flagFilter = new QComboBox(centralWidget);
    m_statusFilter = new QComboBox(centralWidget);
    m_statusFilter->addItem("All Statuses", -1);
    m_statusFilter->addItem("None", static_cast<int>(ItemStatus::None));
    m_statusFilter->addItem("Draft", static_cast<int>(ItemStatus::Draft));
    m_statusFilter->addItem("Completed", static_cast<int>(ItemStatus::Completed));

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

    filterRowLayout->addWidget(new QLabel("Group:", centralWidget));
    filterRowLayout->addWidget(m_groupFilter);
    filterRowLayout->addWidget(new QLabel("Type:", centralWidget));
    filterRowLayout->addWidget(m_typeFilter);
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

    m_valueFilterScroll = new QScrollArea(centralWidget);
    m_valueFilterScroll->setWidgetResizable(true);
    m_valueFilterScroll->setMaximumHeight(120);
    auto* valueFilterPanel = new QWidget(m_valueFilterScroll);
    m_valueFilterLayout = new QGridLayout(valueFilterPanel);
    m_valueFilterScroll->setWidget(valueFilterPanel);
    m_valueFilterScroll->hide();

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
    rootLayout->addWidget(m_valueFilterScroll);
    rootLayout->addLayout(searchRowLayout);

    m_tableView = new QTableView(centralWidget);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Id", "Group", "Type", "Title", "Disambiguation", "Tags", "Flags", "Aliases", "Status", "Understanding", "Pinned"});
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

    m_itemContentView = new QTextEdit(centralWidget);
    m_itemContentView->setReadOnly(true);
    m_highlighter = new CodeHighlighter(m_itemContentView->document());
    m_itemContentView->setPlaceholderText("Select an item to view content...");
    m_itemContentView->setMinimumHeight(150);

    m_linksView = new QTextBrowser(centralWidget);
    m_linksView->setReadOnly(true);
    m_linksView->setOpenLinks(false);
    m_linksView->setMaximumHeight(100);
    m_linksHighlighter = new CodeHighlighter(m_linksView->document());
    connect(m_linksView, &QTextBrowser::anchorClicked, this, &MainWindow::onLinkActivated);

    updateMarkdownStyles();

    rootLayout->addWidget(m_itemContentView, 1);
    rootLayout->addWidget(m_linksView, 0);

    setCentralWidget(centralWidget);

    auto* completerModel = new QStringListModel(this);
    m_completer = new QCompleter(completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);
    m_searchEdit->setCompleter(m_completer);

    connect(m_groupFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        refreshTypes();
        refreshValueFilters();
        resetPaginationAndRefresh();
    });
    connect(m_typeFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        refreshValueFilters();
        resetPaginationAndRefresh();
    });
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
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addItem);
    connect(editButton, &QPushButton::clicked, this, &MainWindow::editSelectedItem);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedItem);
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection&, const QItemSelection&) {
        updateActions();
        auto indexes = m_tableView->selectionModel()->selectedRows();
        if (!indexes.isEmpty()) {
            showItemContent(indexes.first());
        } else {
            m_itemContentView->clear();
        }
    });
    connect(m_tableView, &QTableView::doubleClicked, this, [this](const QModelIndex&) { editSelectedItem(); });

}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("File");
    auto* refreshAction = fileMenu->addAction("Refresh");
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refreshAll);
    fileMenu->addSeparator();
    auto* quitAction = fileMenu->addAction("Quit");
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto* manageMenu = menuBar()->addMenu("Manage");
    auto* groupsAction = manageMenu->addAction("Groups...");
    auto* typesAction = manageMenu->addAction("Types...");
    connect(groupsAction, &QAction::triggered, this, &MainWindow::openGroupManager);
    connect(typesAction, &QAction::triggered, this, &MainWindow::openTypeManager);

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
    refreshGroups();
    refreshTypes();
    refreshValueFilters();
    refreshTags();
    refreshFlags();
    refreshSuggestions();
    refreshItems();
    updateActions();
}

void MainWindow::refreshGroups() {
    QString error;
    m_groups = DatabaseManager::loadGroups(&error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    const QVariant currentGroup = m_groupFilter->currentData();

    m_groupFilter->blockSignals(true);
    m_groupFilter->clear();
    m_groupFilter->addItem("All groups", 0);
    for (const auto& group : m_groups) {
        m_groupFilter->addItem(group.name, group.id);
    }
    const int idx = m_groupFilter->findData(currentGroup);
    if (idx >= 0) {
        m_groupFilter->setCurrentIndex(idx);
    }
    m_groupFilter->blockSignals(false);
}

void MainWindow::refreshTypes() {
    QString error;
    const auto types = DatabaseManager::loadItemTypes(m_groupFilter->currentData().toInt(), &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    const int currentTypeId = m_typeFilter->currentData().toInt();
    const QSignalBlocker blocker(m_typeFilter);
    m_typeFilter->clear();
    m_typeFilter->addItem("All types", 0);
    for (const auto& type : types) {
        const QString scope = type.groupId < 0 ? "All groups" : type.groupName;
        m_typeFilter->addItem(QString("%1 (%2)").arg(type.name, scope), type.id);
        m_typeFilter->setItemData(m_typeFilter->count() - 1, type.description, Qt::ToolTipRole);
    }
    const int index = m_typeFilter->findData(currentTypeId);
    if (index >= 0) {
        m_typeFilter->setCurrentIndex(index);
    }
}

void MainWindow::refreshValueFilters() {
    const int typeId = m_typeFilter->currentData().toInt();
    QMap<int, QString> previousValues;
    if (typeId == m_valueFilterTypeId) {
        for (auto it = m_valueFilterEditors.cbegin(); it != m_valueFilterEditors.cend(); ++it) {
            if (auto* line = qobject_cast<QLineEdit*>(it.value())) {
                previousValues.insert(it.key(), line->text());
            } else if (auto* combo = qobject_cast<QComboBox*>(it.value())) {
                previousValues.insert(it.key(), combo->currentData().toString());
            }
        }
    }

    QString error;
    const auto fields = typeId > 0 ? DatabaseManager::loadItemFields(typeId, &error) : QList<ItemFieldRecord>();
    if (!error.isEmpty()) {
        showError(error);
        return;
    }
    m_selectedTypeFields = fields;
    m_valueFilterTypeId = typeId;
    m_valueFilterEditors.clear();
    while (auto* entry = m_valueFilterLayout->takeAt(0)) {
        delete entry->widget();
        delete entry;
    }
    for (int index = 0; index < fields.size(); ++index) {
        const auto& field = fields.at(index);
        auto* label = new QLabel(field.name + ":", m_valueFilterScroll);
        QWidget* editor = nullptr;
        if (field.dataType == FieldDataType::Boolean || field.dataType == FieldDataType::Enum) {
            auto* combo = new QComboBox(m_valueFilterScroll);
            combo->addItem("Any", QString());
            if (field.dataType == FieldDataType::Boolean) {
                combo->addItem("False", "false");
                combo->addItem("True", "true");
            } else {
                for (const auto& option : field.enumOptions) combo->addItem(option, option);
            }
            const int previous = combo->findData(previousValues.value(field.id));
            if (previous >= 0) combo->setCurrentIndex(previous);
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::resetPaginationAndRefresh);
            editor = combo;
        } else {
            auto* line = new QLineEdit(previousValues.value(field.id), m_valueFilterScroll);
            line->setPlaceholderText("Filter value...");
            line->setMinimumWidth(130);
            connect(line, &QLineEdit::textChanged, this, &MainWindow::resetPaginationAndRefresh);
            editor = line;
        }
        m_valueFilterLayout->addWidget(label, index / 3, (index % 3) * 2);
        m_valueFilterLayout->addWidget(editor, index / 3, (index % 3) * 2 + 1);
        m_valueFilterEditors.insert(field.id, editor);
    }
    m_valueFilterScroll->setVisible(typeId > 0 && !fields.isEmpty());
}

QList<ItemValueFilter> MainWindow::valueFilters() const {
    QList<ItemValueFilter> filters;
    for (const auto& field : m_selectedTypeFields) {
        const QWidget* editor = m_valueFilterEditors.value(field.id);
        QString value;
        if (auto* line = qobject_cast<const QLineEdit*>(editor)) value = line->text().trimmed();
        else if (auto* combo = qobject_cast<const QComboBox*>(editor)) value = combo->currentData().toString();
        if (value.isEmpty()) continue;
        const bool exact = field.dataType != FieldDataType::Text && field.dataType != FieldDataType::Other;
        filters.push_back({field.id, value, exact});
    }
    return filters;
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

void MainWindow::refreshItems() {
    QString error;
    int groupId = m_groupFilter->currentData().toInt();
    const int typeId = m_typeFilter->currentData().toInt();
    const auto fieldFilters = valueFilters();
    QString tagFilter = m_tagFilter->currentIndex() > 0 ? m_tagFilter->currentText() : QString();
    QString flagFilter = m_flagFilter->currentIndex() > 0 ? m_flagFilter->currentText() : QString();
    int understandingFilter = m_understandingFilter->currentData().toInt();
    int statusFilter = m_statusFilter->currentData().toInt();
    int pinnedFilter = m_pinnedFilter->currentData().toInt();
    QString searchText = m_searchEdit->text().trimmed();

    // If we're restoring the last item at startup
    if (m_lastItemId != -1 && searchText.isEmpty()) {
        ItemRecord lastItem;
        if (DatabaseManager::loadItem(m_lastItemId, lastItem, &error)) {
            m_searchEdit->blockSignals(true);
            m_searchEdit->setText(lastItem.title);
            m_searchEdit->blockSignals(false);
            searchText = lastItem.title;
        }
    }
    
    int totalCount = DatabaseManager::countItems(groupId, typeId, fieldFilters, searchText, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    int totalPages = std::max(1, (totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage >= totalPages) m_currentPage = totalPages - 1;
    if (m_currentPage < 0) m_currentPage = 0;

    const int sortSection = m_tableView->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_tableView->horizontalHeader()->sortIndicatorOrder();

    const auto items = DatabaseManager::loadItems(groupId, typeId, fieldFilters, searchText, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, m_pageSize, m_currentPage * m_pageSize, sortSection, sortOrder, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    m_model->removeRows(0, m_model->rowCount());
    m_model->setRowCount(0); // Ensure it's clean
    QStringList headers = {"Id", "Group", "Type", "Title", "Disambiguation", "Tags", "Flags", "Aliases", "Status", "Understanding", "Pinned"};
    if (typeId > 0) {
        for (const auto& field : m_selectedTypeFields) headers.push_back(field.name);
    }
    m_model->setColumnCount(headers.size());
    m_model->setHorizontalHeaderLabels(headers);
    int rowToSelect = -1;
    for (int i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (m_lastItemId != -1 && item.id == m_lastItemId) {
            rowToSelect = i;
        }
        QList<QStandardItem*> row;
        auto* idItem = new QStandardItem();
        idItem->setData(item.id, Qt::DisplayRole);
        idItem->setData(item.id, Qt::UserRole);
        row << idItem
            << new QStandardItem(item.groupName)
            << new QStandardItem(item.itemTypeName)
            << new QStandardItem(item.title)
            << new QStandardItem(item.disambiguation)
            << new QStandardItem(item.tags.join(", "))
            << new QStandardItem(item.flags.join(", "))
            << new QStandardItem(item.aliases.join(", "));
        
        QString statusText;
        switch (item.status) {
            case ItemStatus::Draft:     statusText = "Draft"; break;
            case ItemStatus::Completed: statusText = "Completed"; break;
            default:                    statusText = "None"; break;
        }
        row << new QStandardItem(statusText);

        QString understandingText;
        switch (item.understanding) {
            case UnderstandingLevel::Recognized: understandingText = "Recognized"; break;
            case UnderstandingLevel::Understood: understandingText = "Understood"; break;
            case UnderstandingLevel::Practiced:  understandingText = "Practiced"; break;
            case UnderstandingLevel::Mastered:   understandingText = "Mastered"; break;
            default:                             understandingText = "Unknown"; break;
        }
        row << new QStandardItem(understandingText);
        row << new QStandardItem(item.pinned ? "Yes" : "No");
        if (typeId > 0) {
            for (const auto& field : m_selectedTypeFields) {
                row << new QStandardItem(item.fieldValues.value(field.id));
            }
        }
        m_model->appendRow(row);
    }

    m_tableView->setColumnHidden(0, false);
    m_tableView->resizeColumnsToContents();
    m_tableView->horizontalHeader()->setSectionResizeMode(10, QHeaderView::Fixed);
    m_tableView->setColumnWidth(10, 60);

    m_pageLabel->setText(QString("Page %1 of %2 (%3 total)").arg(m_currentPage + 1).arg(totalPages).arg(totalCount));
    m_firstButton->setEnabled(m_currentPage > 0);
    m_prevButton->setEnabled(m_currentPage > 0);
    m_nextButton->setEnabled(m_currentPage < totalPages - 1);
    m_lastButton->setEnabled(m_currentPage < totalPages - 1);

    if (rowToSelect != -1) {
        m_tableView->selectRow(rowToSelect);
        m_lastItemId = -1; // Reset so it doesn't keep selecting on every refresh
    }

    updateActions();
}

void MainWindow::resetPaginationAndRefresh() {
    m_currentPage = 0;
    refreshItems();
}

void MainWindow::firstPage() {
    if (m_currentPage > 0) {
        m_currentPage = 0;
        refreshItems();
    }
}

void MainWindow::prevPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        refreshItems();
    }
}

void MainWindow::nextPage() {
    m_currentPage++;
    refreshItems();
}

void MainWindow::lastPage() {
    QString error;
    const int groupId = m_groupFilter->currentData().toInt();
    const int typeId = m_typeFilter->currentData().toInt();
    const auto fieldFilters = valueFilters();
    const QString tagFilter = m_tagFilter->currentIndex() > 0 ? m_tagFilter->currentText() : QString();
    const QString flagFilter = m_flagFilter->currentIndex() > 0 ? m_flagFilter->currentText() : QString();
    const int understandingFilter = m_understandingFilter->currentData().toInt();
    const int statusFilter = m_statusFilter->currentData().toInt();
    const int pinnedFilter = m_pinnedFilter->currentData().toInt();
    const QString searchText = m_searchEdit->text().trimmed();

    int totalCount = DatabaseManager::countItems(groupId, typeId, fieldFilters, searchText, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, &error);
    if (!error.isEmpty()) {
        showError(error);
        return;
    }

    int totalPages = std::max(1, (totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage < totalPages - 1) {
        m_currentPage = totalPages - 1;
        refreshItems();
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

int MainWindow::selectedItemId() const {
    const auto indexes = m_tableView->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return -1;
    }
    const QModelIndex index = indexes.first();
    return m_model->item(index.row(), 0)->data(Qt::UserRole).toInt();
}

QList<GroupRecord> MainWindow::groups() const {
    return m_groups;
}

int MainWindow::groupIdForNewItem() {
    const int selectedGroupId = m_groupFilter->currentData().toInt();
    if (selectedGroupId > 0) {
        return selectedGroupId;
    }

    QString error;
    const int groupId = DatabaseManager::defaultGroupId(&error);
    if (groupId <= 0) {
        showError(error.isEmpty() ? "Cannot find the Default group." : error);
        return -1;
    }
    refreshGroups();
    return groupId;
}

void MainWindow::addItem() {
    const int groupId = groupIdForNewItem();
    if (groupId <= 0) {
        return;
    }
    ItemEditDialog dialog(this);
    dialog.setGroups(m_groups);
    ItemRecord draft;
    draft.groupId = groupId;
    draft.title = m_searchEdit->text().trimmed();
    dialog.setItem(draft);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    refreshAll();
}

void MainWindow::quickAdd() {
    const QString text = m_searchEdit->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    ItemRecord item;
    item.title = text;
    const int groupId = groupIdForNewItem();
    if (groupId <= 0) {
        return;
    }
    item.groupId = groupId;

    QString error;
    if (!DatabaseManager::saveItem(item, &error)) {
        showError(error);
        return;
    }

    m_searchEdit->clear();
    refreshAll();
}

void MainWindow::editSelectedItem() {
    const int itemId = selectedItemId();
    if (itemId < 0) {
        return;
    }

    ItemRecord item;
    QString error;
    if (!DatabaseManager::loadItem(itemId, item, &error)) {
        showError(error);
        return;
    }

    ItemEditDialog dialog(this);
    dialog.setGroups(m_groups);
    dialog.setItem(item);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    refreshAll();
}

void MainWindow::deleteSelectedItem() {
    const int itemId = selectedItemId();
    if (itemId < 0) {
        return;
    }

    const int row = m_tableView->selectionModel()->selectedRows().first().row();
    const QString title = m_model->item(row, 3)->text();
    const auto answer = QMessageBox::question(this, "Delete item", QString("Delete item '%1'?").arg(title));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!DatabaseManager::deleteItem(itemId, &error)) {
        showError(error);
        return;
    }
    refreshAll();
}

void MainWindow::openGroupManager() {
    GroupManagerDialog dialog(this);
    connect(&dialog, &GroupManagerDialog::groupsChanged, this, &MainWindow::refreshAll);
    dialog.exec();
    refreshAll();
}

void MainWindow::openTypeManager() {
    ItemTypeManagerDialog dialog(this);
    connect(&dialog, &ItemTypeManagerDialog::typesChanged, this, &MainWindow::refreshAll);
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
    const bool hasSelection = selectedItemId() >= 0;
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

void MainWindow::showItemContent(const QModelIndex& index) {
    if (!index.isValid()) {
        m_itemContentView->clear();
        m_linksView->clear();
        return;
    }

    const int itemId = m_model->data(m_model->index(index.row(), 0), Qt::UserRole).toInt();
    ItemRecord item;
    QString error;
    if (DatabaseManager::loadItem(itemId, item, &error)) {
        m_itemContentView->setHtml(MarkdownConverter::toHtml(item.content));
        updateLinksDisplay(itemId);
        DatabaseManager::logItemRead(itemId);
    } else {
        m_itemContentView->setPlainText("Error loading content: " + error);
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
    if (!m_itemContentView) return;

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

    m_itemContentView->setStyleSheet(QString("QTextEdit[readOnly=\"true\"] { background-color: %1; color: %2; }").arg(bgColor, textColor));
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

    m_itemContentView->document()->setDefaultStyleSheet(style);
    m_linksView->document()->setDefaultStyleSheet(style);
    
    // Force re-render of current content
    if (m_tableView->selectionModel()->hasSelection()) {
        showItemContent(m_tableView->currentIndex());
    }
}

void MainWindow::updateLinksDisplay(int itemId) {
    if (itemId == -1) {
        m_linksView->clear();
        return;
    }

    QString error;
    QList<LinkRecord> links = DatabaseManager::loadLinks(itemId, &error);
    QList<LinkRecord> backlinks = DatabaseManager::loadBacklinks(itemId, &error);

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
            case LinkType::ParentOf: return "Parent Of";
            case LinkType::Custom: return "Custom";
            default: return "Link";
        }
    };

    QString html = "<b>Links:</b> ";
    if (links.isEmpty()) {
        html += "None";
    } else {
        for (int i = 0; i < links.size(); ++i) {
            if (i > 0) html += ", ";
            const QString type = links[i].linkType == LinkType::Custom && !links[i].customValue.isEmpty()
                ? QString("Custom: %1").arg(links[i].customValue.toHtmlEscaped())
                : linkTypeToString(links[i].linkType);
            html += QString("<a href=\"%1\">%2 (%3)</a>")
                        .arg(QUrl::toPercentEncoding(links[i].toItemTitle), links[i].toItemTitle.toHtmlEscaped(), type);
        }
    }

    html += "<br><b>Backlinks:</b> ";
    if (backlinks.isEmpty()) {
        html += "None";
    } else {
        for (int i = 0; i < backlinks.size(); ++i) {
            if (i > 0) html += ", ";
            const QString type = backlinks[i].linkType == LinkType::Custom && !backlinks[i].customValue.isEmpty()
                ? QString("Custom: %1").arg(backlinks[i].customValue.toHtmlEscaped())
                : linkTypeToString(backlinks[i].linkType);
            html += QString("<a href=\"%1\">%2 (%3)</a>")
                        .arg(QUrl::toPercentEncoding(backlinks[i].fromItemTitle), backlinks[i].fromItemTitle.toHtmlEscaped(), type);
        }
    }

    m_linksView->setHtml(html);
}

void MainWindow::onLinkActivated(const QUrl& link) {
    QString itemTitle = QUrl::fromPercentEncoding(link.toString().toUtf8());

    m_groupFilter->blockSignals(true);
    m_tagFilter->blockSignals(true);
    m_flagFilter->blockSignals(true);
    m_statusFilter->blockSignals(true);
    m_understandingFilter->blockSignals(true);
    m_searchEdit->blockSignals(true);

    m_groupFilter->setCurrentIndex(0);
    m_tagFilter->setCurrentIndex(0);
    m_flagFilter->setCurrentIndex(0);
    m_statusFilter->setCurrentIndex(0);
    m_understandingFilter->setCurrentIndex(0);
    m_searchEdit->setText(itemTitle);

    m_groupFilter->blockSignals(false);
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
    settings.setValue("state/lastItemId", selectedItemId());
}

void MainWindow::loadSettings() {
    QSettings settings;
    m_pageSize = settings.value("pagination/pageSize", 20).toInt();
    m_lastItemId = settings.value("state/lastItemId", settings.value("state/lastTermId", -1)).toInt();
    if (settings.contains("state/lastTermId")) {
        settings.setValue("state/lastItemId", m_lastItemId);
        settings.remove("state/lastTermId");
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}
