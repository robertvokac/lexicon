#pragma once

#include "LexiconApplication.h"

#include <QDialog>

class QPushButton;
class QTableWidget;

class PropertyFilterDialog : public QDialog {
    Q_OBJECT

public:
    explicit PropertyFilterDialog(QWidget* parent = nullptr);

    void setFilters(const QList<ItemPropertyFilter>& filters);
    QList<ItemPropertyFilter> filters() const;

private slots:
    void addFilter();
    void editFilter();
    void removeFilter();
    void clearFilters();

private:
    bool promptForFilter(ItemPropertyFilter& filter, const QString& title);
    void refreshTable(int selectedRow = -1);
    void updateButtons();

    QList<ItemPropertyFilter> m_filters;
    QTableWidget* m_table = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_clearButton = nullptr;
};
