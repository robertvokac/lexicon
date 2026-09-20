#include "BlobMaintenanceDialog.h"
#include "ApplicationContext.h"
#include "SqliteRepository.h"

#include <QApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
  QApplication qt(argc, argv);
  namespace fs = std::filesystem;
  const auto directory = fs::temp_directory_path() /
      ("lexicon-gui-blobs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(directory);
  struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{directory};
  SqliteRepository repository;
  if (!repository.open((directory / "lexicon.db").string())) return 1;
  lexicon::LexiconApplication application(repository);
  QtApplicationFacade facade(application);
  installApplication(facade);
  const auto source = directory / "source";
  { std::ofstream out(source); out << "gui smoke"; }
  auto hash = application.blobs.importFile(source.string());
  if (!hash) return 1;
  lexicon::ItemTypeRecord type;
  type.name = "Attachments";
  if (!application.types.upsertItemType(type)) return 1;
  auto types = application.types.loadItemTypes();
  if (!types || types->empty()) return 1;
  lexicon::ItemFieldRecord field;
  field.itemTypeId = types->front().id;
  field.name = "File";
  field.dataType = lexicon::FieldDataType::Blob;
  if (!application.types.upsertItemField(field)) return 1;
  auto fields = application.types.loadItemFields(field.itemTypeId);
  auto group = application.groups.defaultGroupId();
  if (!fields || fields->empty() || !group) return 1;
  lexicon::ItemRecord item;
  item.groupId = *group;
  item.itemTypeId = field.itemTypeId;
  item.title = "Blob Item";
  item.fieldValues[fields->front().id] = *hash;
  auto id = application.items.createItem(item);
  if (!id) return 1;

  BlobMaintenanceDialog dialog;
  auto *scan = dialog.findChild<QPushButton *>("blobScanButton");
  auto *full = dialog.findChild<QPushButton *>("blobFullCheckButton");
  auto *remove = dialog.findChild<QPushButton *>("blobDeleteButton");
  auto *summary = dialog.findChild<QLabel *>("blobMaintenanceSummary");
  auto *issues = dialog.findChild<QTableWidget *>("blobMaintenanceIssues");
  if (!scan || !full || !remove || !summary || !issues || remove->isEnabled()) return 1;
  scan->click();
  if (!summary->text().contains("Referenced blobs: 1") ||
      !summary->text().contains("Physical blobs: 1") || remove->isEnabled()) {
    std::cerr << summary->text().toStdString() << '\n'; return 1;
  }
  item.id = *id;
  item.fieldValues.clear();
  if (!application.items.saveItem(item)) return 1;
  full->click();
  if (!summary->text().contains("Unused: 1") || !remove->isEnabled() ||
      issues->rowCount() != 1 || issues->item(0, 0)->text() != "Unused") {
    std::cerr << summary->text().toStdString() << '\n'; return 1;
  }
  bool sawConfirmation = false;
  QTimer::singleShot(0, [&] {
    auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
    if (message && message->text().contains("permanently deleted")) {
      sawConfirmation = true;
      message->done(QMessageBox::Cancel);
    }
  });
  remove->click();
  const auto canonical = directory / "blobs" / hash->substr(0, 2) / hash->substr(2);
  if (!sawConfirmation || !fs::exists(canonical)) return 1;
  return 0;
}
