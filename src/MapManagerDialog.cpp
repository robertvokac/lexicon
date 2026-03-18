#include "MapManagerDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

MapManagerDialog::MapManagerDialog(QWidget* parent)
    : QDialog(parent) {
    setupUi();
    loadMaps();
}

void MapManagerDialog::setupUi() {
    setWindowTitle("Manage maps");
    resize(480, 420);

    auto* layout = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    layout->addWidget(m_list);

    auto* buttonsLayout = new QHBoxLayout();
    auto* addButton = new QPushButton("Add", this);
    m_editButton = new QPushButton("Edit", this);
    m_deleteButton = new QPushButton("Delete", this);
    auto* closeButton = new QPushButton("Close", this);

    buttonsLayout->addWidget(addButton);
    buttonsLayout->addWidget(m_editButton);
    buttonsLayout->addWidget(m_deleteButton);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(closeButton);

    layout->addLayout(buttonsLayout);

    connect(addButton, &QPushButton::clicked, this, &MapManagerDialog::addMap);
    connect(m_editButton, &QPushButton::clicked, this, &MapManagerDialog::editMap);
    connect(m_deleteButton, &QPushButton::clicked, this, &MapManagerDialog::deleteMap);
    connect(closeButton, &QPushButton::clicked, this, &MapManagerDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &MapManagerDialog::selectionChanged);

    selectionChanged();
}

void MapManagerDialog::loadMaps() {
    QString error;
    m_maps = DatabaseManager::loadMaps(&error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }

    m_list->clear();
    for (const auto& map : m_maps) {
        auto* item = new QListWidgetItem(map.name, m_list);
        item->setData(Qt::UserRole, map.id);
        item->setToolTip(map.description);
    }
    selectionChanged();
}

bool MapManagerDialog::promptForMap(MapRecord& map, bool isEdit) {
    QDialog dialog(this);
    dialog.setWindowTitle(isEdit ? "Edit map" : "Add map");
    auto* layout = new QVBoxLayout(&dialog);
    auto* formLayout = new QFormLayout();
    auto* nameEdit = new QLineEdit(map.name, &dialog);
    auto* descEdit = new QPlainTextEdit(&dialog);
    descEdit->setPlainText(map.description);
    descEdit->setMinimumHeight(120);
    formLayout->addRow("Name:", nameEdit);
    formLayout->addRow("Description:", descEdit);
    layout->addLayout(formLayout);
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation", "Map name cannot be empty.");
        return false;
    }
    map.name = nameEdit->text().trimmed();
    map.description = descEdit->toPlainText();
    return true;
}

void MapManagerDialog::addMap() {
    MapRecord map;
    if (!promptForMap(map, false)) {
        return;
    }

    QString error;
    if (!DatabaseManager::upsertMap(map, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadMaps();
    emit mapsChanged();
}

void MapManagerDialog::editMap() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_maps.size()) {
        return;
    }

    MapRecord map = m_maps.at(row);
    if (!promptForMap(map, true)) {
        return;
    }

    QString error;
    if (!DatabaseManager::upsertMap(map, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadMaps();
    emit mapsChanged();
}

void MapManagerDialog::deleteMap() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_maps.size()) {
        return;
    }

    const auto& map = m_maps.at(row);
    const auto answer = QMessageBox::question(
        this,
        "Delete map",
        QString("Delete map '%1'? All terms inside it will also be deleted.").arg(map.name));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!DatabaseManager::deleteMap(map.id, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadMaps();
    emit mapsChanged();
}

void MapManagerDialog::selectionChanged() {
    const bool hasSelection = m_list->currentRow() >= 0;
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}
