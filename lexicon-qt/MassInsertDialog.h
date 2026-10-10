#pragma once

#include "ApplicationContext.h"

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>

class QLabel;
class QTableWidget;
class QTimer;

// Spreadsheet-like creation of many items. Unfinished rows are continuously
// backed up to QSettings and offered again the next time Mass Insert opens.
class MassInsertDialog : public QDialog {
    Q_OBJECT

  public:
    MassInsertDialog(int groupId, int typeId, const QList<ItemFieldRecord>& fields,
                     const QJsonArray& rows = {}, QWidget* parent = nullptr);

    // Shows the Group/Type choice (or resumes a saved draft), then the table.
    // Returns true when at least one item was inserted.
    static bool open(QWidget* parent = nullptr);
    int insertedCount() const { return m_insertedCount; }

  public slots:
    void reject() override;

  private slots:
    void addRow();
    void persistDraft();
    void insertItems();
    void discardDraft();

  private:
    void setRows(const QJsonArray& rows);
    void appendRow(QJsonObject row = QJsonObject{});
    void addRows(int count);
    QJsonObject rowData(int row) const;
    QJsonArray rowsData() const;
    bool rowMeaningful(const QJsonObject& row) const;
    void removeRow(int row);
    void scheduleDraft();
    void clearStoredDraft();

    int m_groupId = -1;
    int m_typeId = -1;
    QList<ItemFieldRecord> m_fields;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_draftTimer = nullptr;
    bool m_populating = false;
    int m_insertedCount = 0;
};
