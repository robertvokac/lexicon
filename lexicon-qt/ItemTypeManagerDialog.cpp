#include "ItemTypeManagerDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <limits>

namespace {
QString dataTypeName(FieldDataType type) {
    switch (type) {
        case FieldDataType::Integer: return "Integer";
        case FieldDataType::Float: return "Float";
        case FieldDataType::Text: return "Text";
        case FieldDataType::Date: return "Date";
        case FieldDataType::Time: return "Time";
        case FieldDataType::Timestamp: return "Timestamp";
        case FieldDataType::Boolean: return "Boolean";
        case FieldDataType::Enum: return "Enum";
        case FieldDataType::Blob: return "Blob";
        case FieldDataType::Other: return "Other";
    }
    return "Unknown";
}
}

ItemTypeManagerDialog::ItemTypeManagerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Manage types");
    resize(560, 600);

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

    auto* fieldsBox = new QGroupBox("Fields of selected type", this);
    auto* fieldsLayout = new QVBoxLayout(fieldsBox);
    m_fieldList = new QListWidget(fieldsBox);
    fieldsLayout->addWidget(m_fieldList);
    auto* fieldButtons = new QHBoxLayout();
    m_addFieldButton = new QPushButton("Add field", fieldsBox);
    m_editFieldButton = new QPushButton("Edit field", fieldsBox);
    m_deleteFieldButton = new QPushButton("Delete field", fieldsBox);
    fieldButtons->addWidget(m_addFieldButton);
    fieldButtons->addWidget(m_editFieldButton);
    fieldButtons->addWidget(m_deleteFieldButton);
    fieldButtons->addStretch();
    fieldsLayout->addLayout(fieldButtons);
    layout->addWidget(fieldsBox);

    connect(addButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::addType);
    connect(m_editButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::editType);
    connect(m_deleteButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::deleteType);
    connect(closeButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &ItemTypeManagerDialog::selectionChanged);
    connect(m_fieldList, &QListWidget::itemSelectionChanged, this, &ItemTypeManagerDialog::fieldSelectionChanged);
    connect(m_addFieldButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::addField);
    connect(m_editFieldButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::editField);
    connect(m_deleteFieldButton, &QPushButton::clicked, this, &ItemTypeManagerDialog::deleteField);

    loadTypes();
}

void ItemTypeManagerDialog::loadTypes() {
    const int selectedTypeId = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toInt() : -1;
    QString error;
    m_groups = services().groups.loadGroups(&error);
    if (error.isEmpty()) {
        m_types = services().types.loadItemTypes(-1, &error);
    }
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }

    m_list->clear();
    for (const auto& type : m_types) {
        const QString scope = type.groupId < 0 ? "All groups" : type.groupName;
        auto* item = new QListWidgetItem(QString("%1 — %2").arg(type.name, scope), m_list);
        item->setData(Qt::UserRole, type.id);
        item->setToolTip(type.description);
    }
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toInt() == selectedTypeId) {
            m_list->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
            break;
        }
    }
    selectionChanged();
}

void ItemTypeManagerDialog::loadFields() {
    m_fields.clear();
    m_fieldList->clear();
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_types.size()) {
        fieldSelectionChanged();
        return;
    }
    QString error;
    m_fields = services().types.loadItemFields(m_types.at(row).id, &error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    for (const auto& field : m_fields) {
        auto* item = new QListWidgetItem(QString("%1  %2 — %3")
            .arg(field.position).arg(field.name, dataTypeName(field.dataType)), m_fieldList);
        item->setData(Qt::UserRole, field.id);
    }
    fieldSelectionChanged();
}

bool ItemTypeManagerDialog::promptForType(ItemTypeRecord& type, bool isEdit) {
    QDialog dialog(this);
    dialog.setWindowTitle(isEdit ? "Edit type" : "Add type");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* nameEdit = new QLineEdit(type.name, &dialog);
    auto* descriptionEdit = new QPlainTextEdit(&dialog);
    descriptionEdit->setPlainText(type.description);
    descriptionEdit->setMinimumHeight(100);
    auto* groupCombo = new QComboBox(&dialog);
    groupCombo->addItem("All groups", -1);
    for (const auto& group : m_groups) {
        groupCombo->addItem(group.name, group.id);
    }
    const int groupIndex = groupCombo->findData(type.groupId);
    if (groupIndex >= 0) {
        groupCombo->setCurrentIndex(groupIndex);
    }
    form->addRow("Name:", nameEdit);
    form->addRow("Description:", descriptionEdit);
    form->addRow("Available in:", groupCombo);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation", "Type name cannot be empty.");
        return false;
    }
    type.name = nameEdit->text().trimmed();
    type.description = descriptionEdit->toPlainText().trimmed();
    type.groupId = groupCombo->currentData().toInt();
    return true;
}

void ItemTypeManagerDialog::addType() {
    ItemTypeRecord type;
    if (!promptForType(type, false)) {
        return;
    }
    QString error;
    if (!services().types.upsertItemType(type, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadTypes();
    for (int row = 0; row < m_types.size(); ++row) {
        if (m_types.at(row).name == type.name && m_types.at(row).groupId == type.groupId) {
            m_list->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
            break;
        }
    }
    emit typesChanged();
    selectionChanged();
}

void ItemTypeManagerDialog::editType() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_types.size()) {
        return;
    }
    ItemTypeRecord type = m_types.at(row);
    if (!promptForType(type, true)) {
        return;
    }
    QString error;
    if (!services().types.upsertItemType(type, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadTypes();
    emit typesChanged();
}

void ItemTypeManagerDialog::deleteType() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_types.size()) {
        return;
    }
    const auto& type = m_types.at(row);
    QString error;
    const int affectedItems = services().types.countItemsForType(type.id, &error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    const auto answer = QMessageBox::question(
        this, "Delete type",
        QString("Delete type '%1'? The Type field will be set to None for all %2 item(s) using it, and their custom field values will be deleted. This data cannot be restored automatically.")
            .arg(type.name).arg(affectedItems),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (!services().types.deleteItemType(type.id, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadTypes();
    emit typesChanged();
}

void ItemTypeManagerDialog::selectionChanged() {
    const bool hasSelection = m_list->currentRow() >= 0;
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
    m_addFieldButton->setEnabled(hasSelection);
    loadFields();
}

bool ItemTypeManagerDialog::promptForField(ItemFieldRecord& field, bool isEdit) {
    QDialog dialog(this);
    dialog.setWindowTitle(isEdit ? "Edit field" : "Add field");
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* nameEdit = new QLineEdit(field.name, &dialog);
    auto* dataTypeCombo = new QComboBox(&dialog);
    for (int value = 0; value <= static_cast<int>(FieldDataType::Other); ++value) {
        dataTypeCombo->addItem(dataTypeName(static_cast<FieldDataType>(value)), value);
    }
    dataTypeCombo->setCurrentIndex(dataTypeCombo->findData(static_cast<int>(field.dataType)));
    auto* positionEdit = new QSpinBox(&dialog);
    positionEdit->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    positionEdit->setValue(field.position);
    auto* optionsEdit = new QPlainTextEdit(&dialog);
    optionsEdit->setPlaceholderText("One enum option per line");
    optionsEdit->setPlainText(field.enumOptions.join("\n"));
    optionsEdit->setMaximumHeight(100);
    optionsEdit->setEnabled(field.dataType == FieldDataType::Enum);
    connect(dataTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [dataTypeCombo, optionsEdit] {
        optionsEdit->setEnabled(dataTypeCombo->currentData().toInt() == static_cast<int>(FieldDataType::Enum));
    });
    form->addRow("Name:", nameEdit);
    form->addRow("Data type:", dataTypeCombo);
    form->addRow("Position:", positionEdit);
    form->addRow("Enum options:", optionsEdit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation", "Field name cannot be empty.");
        return false;
    }
    field.name = nameEdit->text().trimmed();
    field.dataType = static_cast<FieldDataType>(dataTypeCombo->currentData().toInt());
    field.position = positionEdit->value();
    field.enumOptions = field.dataType == FieldDataType::Enum
        ? optionsEdit->toPlainText().split('\n', Qt::SkipEmptyParts) : QStringList();
    return true;
}

void ItemTypeManagerDialog::addField() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_types.size()) return;
    ItemFieldRecord field;
    field.itemTypeId = m_types.at(row).id;
    if (!m_fields.isEmpty()) {
        const int last = m_fields.last().position;
        field.position = last == std::numeric_limits<int>::max() ? last : last + 1;
    }
    if (!promptForField(field, false)) return;
    QString error;
    if (!services().types.upsertItemField(field, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadFields();
    emit typesChanged();
}

void ItemTypeManagerDialog::editField() {
    const int row = m_fieldList->currentRow();
    if (row < 0 || row >= m_fields.size()) return;
    const ItemFieldRecord original = m_fields.at(row);
    ItemFieldRecord field = original;
    if (!promptForField(field, true)) return;
    QString error;
    if (field.dataType != original.dataType || field.enumOptions != original.enumOptions) {
        const int affected = services().types.countFieldValues(field.id, &error);
        if (!error.isEmpty()) {
            QMessageBox::critical(this, "Database error", error);
            return;
        }
        if (affected > 0 && QMessageBox::question(this, "Change field data type",
            QString("Changing the data type or enum options will clear %1 stored value(s). Continue?").arg(affected),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    }
    if (!services().types.upsertItemField(field, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadFields();
    emit typesChanged();
}

void ItemTypeManagerDialog::deleteField() {
    const int row = m_fieldList->currentRow();
    if (row < 0 || row >= m_fields.size()) return;
    const auto& field = m_fields.at(row);
    QString error;
    const int affected = services().types.countFieldValues(field.id, &error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    if (QMessageBox::question(this, "Delete field",
        QString("Delete field '%1'? This will remove its value from %2 item(s). Continue?").arg(field.name).arg(affected),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (!services().types.deleteItemField(field.id, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadFields();
    emit typesChanged();
}

void ItemTypeManagerDialog::fieldSelectionChanged() {
    const bool selected = m_fieldList->currentRow() >= 0;
    m_editFieldButton->setEnabled(selected);
    m_deleteFieldButton->setEnabled(selected);
}
