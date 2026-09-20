#include "PropertyFilterDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

PropertyFilterDialog::PropertyFilterDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Filter Properties");
    resize(500, 360);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        "All filters must match. Keys are exact; values contain the entered text. "
        "Leave a value empty to match any value for that key.", this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    m_table = new QTableWidget(this);
    m_table->setObjectName("propertyFilterTable");
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Key", "Value contains"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    layout->addWidget(m_table);

    auto* actions = new QHBoxLayout();
    auto* addButton = new QPushButton("Add", this);
    addButton->setObjectName("addPropertyFilterButton");
    m_editButton = new QPushButton("Edit", this);
    m_editButton->setObjectName("editPropertyFilterButton");
    m_removeButton = new QPushButton("Remove", this);
    m_removeButton->setObjectName("removePropertyFilterButton");
    m_clearButton = new QPushButton("Clear", this);
    m_clearButton->setObjectName("clearPropertyFiltersButton");
    actions->addWidget(addButton);
    actions->addWidget(m_editButton);
    actions->addWidget(m_removeButton);
    actions->addWidget(m_clearButton);
    actions->addStretch();
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(addButton, &QPushButton::clicked, this, &PropertyFilterDialog::addFilter);
    connect(m_editButton, &QPushButton::clicked, this, &PropertyFilterDialog::editFilter);
    connect(m_removeButton, &QPushButton::clicked, this, &PropertyFilterDialog::removeFilter);
    connect(m_clearButton, &QPushButton::clicked, this, &PropertyFilterDialog::clearFilters);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PropertyFilterDialog::updateButtons);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { editFilter(); });
    updateButtons();
}

void PropertyFilterDialog::setFilters(const QList<ItemPropertyFilter>& filters) {
    m_filters = filters;
    refreshTable();
}

QList<ItemPropertyFilter> PropertyFilterDialog::filters() const {
    return m_filters;
}

bool PropertyFilterDialog::promptForFilter(ItemPropertyFilter& filter, const QString& title) {
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* keyEdit = new QLineEdit(filter.key, &dialog);
    keyEdit->setObjectName("propertyFilterKeyEdit");
    auto* valueEdit = new QLineEdit(filter.value, &dialog);
    valueEdit->setObjectName("propertyFilterValueEdit");
    form->addRow("Key:", keyEdit);
    form->addRow("Value contains:", valueEdit);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (keyEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, "Filter Properties", "Key cannot be empty.");
            keyEdit->setFocus();
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    filter.key = keyEdit->text().trimmed();
    filter.value = valueEdit->text().trimmed();
    return true;
}

void PropertyFilterDialog::refreshTable(int selectedRow) {
    m_table->setRowCount(m_filters.size());
    for (int row = 0; row < m_filters.size(); ++row) {
        m_table->setItem(row, 0, new QTableWidgetItem(m_filters.at(row).key));
        m_table->setItem(row, 1, new QTableWidgetItem(m_filters.at(row).value));
    }
    if (selectedRow >= 0 && selectedRow < m_filters.size()) m_table->selectRow(selectedRow);
    else m_table->clearSelection();
    updateButtons();
}

void PropertyFilterDialog::updateButtons() {
    const bool selected = m_table->currentRow() >= 0 && !m_table->selectedItems().isEmpty();
    m_editButton->setEnabled(selected);
    m_removeButton->setEnabled(selected);
    m_clearButton->setEnabled(!m_filters.isEmpty());
}

void PropertyFilterDialog::addFilter() {
    ItemPropertyFilter filter;
    if (!promptForFilter(filter, "Add property filter")) return;
    m_filters.append(filter);
    refreshTable(m_filters.size() - 1);
}

void PropertyFilterDialog::editFilter() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_filters.size()) return;
    ItemPropertyFilter filter = m_filters.at(row);
    if (!promptForFilter(filter, "Edit property filter")) return;
    m_filters[row] = filter;
    refreshTable(row);
}

void PropertyFilterDialog::removeFilter() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_filters.size()) return;
    m_filters.removeAt(row);
    refreshTable(qMin(row, m_filters.size() - 1));
}

void PropertyFilterDialog::clearFilters() {
    m_filters.clear();
    refreshTable();
}
