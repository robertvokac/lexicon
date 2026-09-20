#include "BlobMaintenanceDialog.h"
#include "ApplicationContext.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
QString issueName(lexicon::BlobIssueType type) {
  switch (type) {
  case lexicon::BlobIssueType::Healthy: return "Healthy";
  case lexicon::BlobIssueType::Orphaned: return "Unused";
  case lexicon::BlobIssueType::Missing: return "Missing";
  case lexicon::BlobIssueType::HashMismatch: return "Hash mismatch";
  case lexicon::BlobIssueType::InvalidReference: return "Invalid reference";
  case lexicon::BlobIssueType::UnexpectedFile: return "Unexpected entry";
  }
  return "Unknown";
}
QString byteText(std::uint64_t bytes) {
  return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}
} // namespace

BlobMaintenanceDialog::BlobMaintenanceDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle("Blob storage maintenance");
  resize(850, 520);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel("The database and Blob directory together form the complete Lexicon data set. "
                           "Scan before deleting unused files.", this);
  intro->setWordWrap(true);
  layout->addWidget(intro);
  summary_ = new QLabel("No scan completed.", this);
  summary_->setObjectName("blobMaintenanceSummary");
  layout->addWidget(summary_);
  issues_ = new QTableWidget(this);
  issues_->setObjectName("blobMaintenanceIssues");
  issues_->setColumnCount(4);
  issues_->setHorizontalHeaderLabels({"State", "Hash / path", "Size", "Details"});
  issues_->horizontalHeader()->setStretchLastSection(true);
  issues_->setSelectionBehavior(QAbstractItemView::SelectRows);
  issues_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  layout->addWidget(issues_);
  auto *actions = new QHBoxLayout;
  auto *scanButton = new QPushButton("Scan", this);
  scanButton->setObjectName("blobScanButton");
  auto *fullButton = new QPushButton("Full integrity check", this);
  fullButton->setObjectName("blobFullCheckButton");
  deleteButton_ = new QPushButton("Delete unused blobs...", this);
  deleteButton_->setObjectName("blobDeleteButton");
  deleteButton_->setEnabled(false);
  auto *closeButton = new QPushButton("Close", this);
  actions->addWidget(scanButton);
  actions->addWidget(fullButton);
  actions->addWidget(deleteButton_);
  actions->addStretch();
  actions->addWidget(closeButton);
  layout->addLayout(actions);
  connect(scanButton, &QPushButton::clicked, this, [this] { scan(lexicon::BlobScanDepth::Structural); });
  connect(fullButton, &QPushButton::clicked, this, [this] { scan(lexicon::BlobScanDepth::FullIntegrity); });
  connect(deleteButton_, &QPushButton::clicked, this, [this] { collect(); });
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void BlobMaintenanceDialog::scan(lexicon::BlobScanDepth depth) {
  deleteButton_->setEnabled(false);
  report_.reset();
  auto result = services().blobs.scanStorage(depth);
  if (!result) {
    QMessageBox::critical(this, "Blob maintenance", qtbridge::toQt(result.error().message));
    return;
  }
  report_ = std::move(*result);
  showReport(*report_);
  deleteButton_->setEnabled(report_->orphanedBlobCount != 0);
}

void BlobMaintenanceDialog::showReport(const lexicon::BlobMaintenanceReport &report) {
  summary_->setText(QString("%1 scan\nReferences: %2    Referenced blobs: %3    Physical blobs: %4\n"
                            "Unused: %5    Missing: %6    Corrupted: %7\n"
                            "Total storage: %8    Reclaimable: %9")
                        .arg(report.depth == lexicon::BlobScanDepth::FullIntegrity ? "Full integrity" : "Structural")
                        .arg(report.referenceCount).arg(report.referencedBlobCount)
                        .arg(report.physicalBlobCount).arg(report.orphanedBlobCount)
                        .arg(report.missingBlobCount)
                        .arg(report.depth == lexicon::BlobScanDepth::FullIntegrity
                                 ? QString::number(report.corruptedBlobCount)
                                 : "Not checked")
                        .arg(byteText(report.totalBytes)).arg(byteText(report.orphanedBytes)));
  issues_->setRowCount(static_cast<int>(report.issues.size()));
  for (int row = 0; row < static_cast<int>(report.issues.size()); ++row) {
    const auto &issue = report.issues[static_cast<std::size_t>(row)];
    const QString values[] = {issueName(issue.type),
                              qtbridge::toQt(issue.relativePath.empty() ? issue.hash : issue.relativePath),
                              byteText(issue.size), qtbridge::toQt(issue.detail)};
    for (int column = 0; column < 4; ++column)
      issues_->setItem(row, column, new QTableWidgetItem(values[column]));
  }
  issues_->resizeColumnsToContents();
}

void BlobMaintenanceDialog::collect() {
  if (!report_ || report_->orphanedBlobCount == 0) return;
  const auto question = QString("%1 unused Blob files will be permanently deleted.\n"
                                "Approximately %2 will be reclaimed.\n\n"
                                "This cannot be undone. Continue?")
                            .arg(report_->orphanedBlobCount).arg(byteText(report_->orphanedBytes));
  QMessageBox confirmation(QMessageBox::Warning, "Delete unused Blob files", question,
                           QMessageBox::NoButton, this);
  auto *cancelButton = confirmation.addButton(QMessageBox::Cancel);
  auto *confirmButton = confirmation.addButton("Delete", QMessageBox::DestructiveRole);
  confirmation.setDefaultButton(qobject_cast<QPushButton *>(cancelButton));
  confirmation.exec();
  if (confirmation.clickedButton() != confirmButton)
    return;
  deleteButton_->setEnabled(false);
  auto result = services().blobs.collectUnusedBlobs(*report_);
  if (!result) {
    QMessageBox::critical(this, "Blob maintenance", qtbridge::toQt(result.error().message));
  } else {
    QMessageBox::information(this, "Blob maintenance",
                             QString("Deleted: %1 (%2)\nSkipped or re-referenced: %3\nFailed: %4")
                                 .arg(result->deleted).arg(byteText(result->deletedBytes))
                                 .arg(result->skipped).arg(result->failed));
  }
  scan(report_->depth);
}
