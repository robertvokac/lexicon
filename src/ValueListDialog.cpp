#include "ValueListDialog.h"

#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

ValueListDialog::ValueListDialog(const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(title);
    resize(420, 520);

    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table);

    auto* closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &ValueListDialog::accept);
    layout->addWidget(closeButton);
}

void ValueListDialog::setValues(const QList<UsageValueRecord>& values, const QString& valueHeader) {
    m_table->clear();
    m_table->setHorizontalHeaderLabels({valueHeader, "Usage count"});
    m_table->setRowCount(values.size());

    for (int row = 0; row < values.size(); ++row) {
        m_table->setItem(row, 0, new QTableWidgetItem(values.at(row).value));
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(values.at(row).usageCount)));
    }
    m_table->resizeColumnsToContents();
}
