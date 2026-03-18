#pragma once

#include "DatabaseManager.h"

#include <QDialog>

class QTableWidget;

class ValueListDialog : public QDialog {
    Q_OBJECT

public:
    explicit ValueListDialog(const QString& title, QWidget* parent = nullptr);
    void setValues(const QList<UsageValueRecord>& values, const QString& valueHeader);

private:
    QTableWidget* m_table = nullptr;
};
