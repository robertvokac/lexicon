#include "GroupManagerDialog.h"

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
#include <QSpinBox>
#include <QVBoxLayout>

#include <limits>

GroupManagerDialog::GroupManagerDialog(QWidget* parent)
    : QDialog(parent) {
    setupUi();
    loadGroups();
}

void GroupManagerDialog::setupUi() {
    setWindowTitle("Manage groups");
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

    connect(addButton, &QPushButton::clicked, this, &GroupManagerDialog::addGroup);
    connect(m_editButton, &QPushButton::clicked, this, &GroupManagerDialog::editGroup);
    connect(m_deleteButton, &QPushButton::clicked, this, &GroupManagerDialog::deleteGroup);
    connect(closeButton, &QPushButton::clicked, this, &GroupManagerDialog::accept);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &GroupManagerDialog::selectionChanged);

    selectionChanged();
}

void GroupManagerDialog::loadGroups() {
    QString error;
    m_groups = services().groups.loadGroups(&error);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }

    m_list->clear();
    for (const auto& group : m_groups) {
        auto* item = new QListWidgetItem(QString("%1  %2").arg(group.position).arg(group.name), m_list);
        item->setData(Qt::UserRole, group.id);
        item->setToolTip(group.description);
    }
    selectionChanged();
}

bool GroupManagerDialog::promptForGroup(GroupRecord& group, bool isEdit) {
    QDialog dialog(this);
    dialog.setWindowTitle(isEdit ? "Edit group" : "Add group");
    auto* layout = new QVBoxLayout(&dialog);
    auto* formLayout = new QFormLayout();
    auto* nameEdit = new QLineEdit(group.name, &dialog);
    auto* descEdit = new QPlainTextEdit(&dialog);
    descEdit->setPlainText(group.description);
    descEdit->setMinimumHeight(120);
    auto* positionEdit = new QSpinBox(&dialog);
    positionEdit->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    positionEdit->setValue(group.position);
    positionEdit->setToolTip("Lower positions appear first. Groups with the same position are sorted by name.");
    formLayout->addRow("Name:", nameEdit);
    formLayout->addRow("Description:", descEdit);
    formLayout->addRow("Position:", positionEdit);
    layout->addLayout(formLayout);
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation", "Group name cannot be empty.");
        return false;
    }
    group.name = nameEdit->text().trimmed();
    group.description = descEdit->toPlainText().trimmed();
    group.position = positionEdit->value();
    return true;
}

void GroupManagerDialog::addGroup() {
    GroupRecord group;
    if (!m_groups.isEmpty()) {
        const int lastPosition = m_groups.last().position;
        group.position = lastPosition == std::numeric_limits<int>::max() ? lastPosition : lastPosition + 1;
    }
    if (!promptForGroup(group, false)) {
        return;
    }

    QString error;
    if (!services().groups.upsertGroup(group, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadGroups();
    emit groupsChanged();
}

void GroupManagerDialog::editGroup() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_groups.size()) {
        return;
    }

    GroupRecord group = m_groups.at(row);
    if (!promptForGroup(group, true)) {
        return;
    }

    QString error;
    if (!services().groups.upsertGroup(group, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadGroups();
    emit groupsChanged();
}

void GroupManagerDialog::deleteGroup() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_groups.size()) {
        return;
    }

    const auto& group = m_groups.at(row);
    const auto answer = QMessageBox::question(
        this,
        "Delete group",
        QString("Delete group '%1'? All items inside it will also be deleted.").arg(group.name));
    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!services().groups.deleteGroup(group.id, &error)) {
        QMessageBox::critical(this, "Database error", error);
        return;
    }
    loadGroups();
    emit groupsChanged();
}

void GroupManagerDialog::selectionChanged() {
    const bool hasSelection = m_list->currentRow() >= 0;
    m_editButton->setEnabled(hasSelection);
    m_deleteButton->setEnabled(hasSelection);
}
