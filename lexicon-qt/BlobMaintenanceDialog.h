#pragma once

#include "BlobMaintenance.h"
#include <QDialog>
#include <optional>

class QLabel;
class QPushButton;
class QTableWidget;

class BlobMaintenanceDialog : public QDialog {
public:
  explicit BlobMaintenanceDialog(QWidget *parent = nullptr);

private:
  void scan(lexicon::BlobScanDepth depth);
  void collect();
  void showReport(const lexicon::BlobMaintenanceReport &report);

  std::optional<lexicon::BlobMaintenanceReport> report_;
  QLabel *summary_ = nullptr;
  QTableWidget *issues_ = nullptr;
  QPushButton *deleteButton_ = nullptr;
};
