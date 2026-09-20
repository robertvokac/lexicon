#include "LexiconApplication.h"
#include "SqliteRepository.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace {
bool expect(bool condition, const char *message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
template <class T> bool success(const lexicon::Result<T> &result, const char *message) {
  if (!result) std::cerr << message << ": " << result.error().message << '\n';
  return result.has_value();
}
void write(const fs::path &path, const std::string &bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << bytes;
}
bool hasIssue(const lexicon::BlobMaintenanceReport &report, lexicon::BlobIssueType type) {
  for (const auto &issue : report.issues) if (issue.type == type) return true;
  return false;
}
}

int main() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = fs::temp_directory_path() / ("lexicon-blobs-" + std::to_string(stamp));
  fs::create_directories(directory);
  struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{directory};
  SqliteRepository repository;
  if (!success(repository.open((directory / "lexicon.db").string()), "Open database")) return 1;
  lexicon::LexiconApplication app(repository);
  auto group = app.groups.defaultGroupId();
  if (!success(group, "Get group")) return 1;
  lexicon::ItemTypeRecord type;
  type.name = "Média Žluťoučký";
  if (!success(app.types.upsertItemType(type), "Create type")) return 1;
  auto types = app.types.loadItemTypes();
  if (!success(types, "Load types") || !expect(types->size() == 1, "Expected one type")) return 1;
  lexicon::ItemFieldRecord field;
  field.itemTypeId = types->front().id;
  field.name = "Attachment";
  field.dataType = lexicon::FieldDataType::Blob;
  if (!success(app.types.upsertItemField(field), "Create Blob field")) return 1;
  auto fields = app.types.loadItemFields(field.itemTypeId);
  if (!success(fields, "Load fields") || !expect(fields->size() == 1, "Expected one field")) return 1;
  field.id = fields->front().id;

  const auto source = directory / "source";
  write(source, "shared bytes");
  auto hash = app.blobs.importFile(source.string());
  auto duplicate = app.blobs.importFile(source.string());
  if (!success(hash, "Import") || !success(duplicate, "Duplicate import") ||
      !expect(*hash == *duplicate, "Duplicate import changed the hash")) return 1;
  const auto canonical = directory / "blobs" / hash->substr(0, 2) / hash->substr(2);
  if (!expect(fs::is_regular_file(canonical), "Canonical Blob was not created")) return 1;
  auto verified = app.blobs.verifyBlob(*hash);
  if (!success(verified, "Verify imported Blob") ||
      !expect(verified->type == lexicon::BlobIssueType::Healthy, "Imported Blob failed verification")) return 1;
  auto invalidExport = app.blobs.exportFile("../bad", (directory / "export").string());
  if (!expect(!invalidExport && invalidExport.error().code == lexicon::Error::Code::Validation,
              "Invalid export hash was accepted")) return 1;
  for (const auto &entry : fs::directory_iterator(directory / "blobs"))
    if (!expect(entry.path().filename() == hash->substr(0, 2),
                "Temporary Blob survived import")) return 1;

  lexicon::ItemRecord first;
  first.groupId = *group; first.itemTypeId = field.itemTypeId;
  first.title = "A"; first.fieldValues[field.id] = *hash;
  auto firstId = app.items.createItem(first);
  lexicon::ItemRecord second = first;
  second.title = "B";
  auto secondId = app.items.createItem(second);
  if (!success(firstId, "Create first reference") || !success(secondId, "Create second reference")) return 1;
  auto scan = app.blobs.scanStorage();
  if (!success(scan, "Scan shared Blob") ||
      !expect(scan->referenceCount == 2 && scan->referencedBlobCount == 1 &&
              scan->physicalBlobCount == 1 && scan->orphanedBlobCount == 0,
              "Shared Blob counts are wrong")) return 1;
  first.id = *firstId; first.fieldValues.clear();
  if (!success(app.items.saveItem(first), "Remove first reference")) return 1;
  scan = app.blobs.scanStorage();
  if (!success(scan, "Scan one reference") ||
      !expect(scan->referenceCount == 1 && scan->orphanedBlobCount == 0 && fs::exists(canonical),
              "Removing one reference deleted or orphaned a shared Blob")) return 1;
  second.id = *secondId; second.fieldValues.clear();
  if (!success(app.items.saveItem(second), "Remove last reference")) return 1;
  scan = app.blobs.scanStorage();
  if (!success(scan, "Scan orphan") ||
      !expect(scan->orphanedBlobCount == 1 && scan->orphanedBytes == 12 && fs::exists(canonical),
              "Removing last reference did not retain an orphan")) return 1;

  // A stale scan is never authority for deletion. The Item now references X again.
  second.fieldValues[field.id] = *hash;
  if (!success(app.items.saveItem(second), "Restore reference")) return 1;
  auto staleGc = app.blobs.collectUnusedBlobs(*scan);
  if (!success(staleGc, "GC with stale scan") ||
      !expect(staleGc->deleted == 0 && staleGc->skipped == 1 && fs::exists(canonical),
              "Stale scan deleted a re-referenced Blob")) return 1;
  second.fieldValues.clear();
  if (!success(app.items.saveItem(second), "Remove restored reference")) return 1;
  scan = app.blobs.scanStorage();
  if (!success(scan, "Scan final orphan")) return 1;
  auto gc = app.blobs.collectUnusedBlobs(*scan);
  if (!success(gc, "Collect orphan") ||
      !expect(gc->deleted == 1 && gc->deletedBytes == 12 && !fs::exists(canonical),
              "Orphan GC failed")) return 1;
  scan = app.blobs.scanStorage();
  if (!success(scan, "Rescan after GC") ||
      !expect(scan->physicalBlobCount == 0 && scan->orphanedBlobCount == 0,
              "GC left a canonical Blob")) return 1;

  // Valid identifiers in Item values remain untouched when their bytes are absent.
  second.fieldValues[field.id] = *hash;
  if (!success(app.items.saveItem(second), "Create missing reference")) return 1;
  scan = app.blobs.scanStorage();
  if (!success(scan, "Scan missing Blob") ||
      !expect(scan->missingBlobCount == 1 && hasIssue(*scan, lexicon::BlobIssueType::Missing),
              "Missing Blob was not reported")) return 1;
  auto verifiedMissing = app.blobs.verifyBlob(*hash);
  if (!success(verifiedMissing, "Verify missing Blob") ||
      !expect(verifiedMissing->type == lexicon::BlobIssueType::Missing,
              "Verification missed absent bytes")) return 1;
  auto noGc = app.blobs.collectUnusedBlobs(*scan);
  auto stillReferenced = app.items.loadItem(*secondId);
  if (!success(noGc, "GC with missing Blob") || !success(stillReferenced, "Reload missing reference") ||
      !expect(stillReferenced->fieldValues.at(field.id) == *hash, "GC modified a missing reference")) return 1;
  auto missingExport = app.blobs.exportFile(*hash, (directory / "export").string());
  if (!expect(!missingExport && missingExport.error().code == lexicon::Error::Code::NotFound,
              "Missing Blob export did not return NotFound")) return 1;

  auto reimport = app.blobs.importFile(source.string());
  if (!success(reimport, "Restore Blob") || !expect(*reimport == *hash, "Restore changed hash")) return 1;
  write(canonical, "corrupt");
  auto full = app.blobs.scanStorage(lexicon::BlobScanDepth::FullIntegrity);
  if (!success(full, "Full integrity scan") ||
      !expect(full->corruptedBlobCount == 1 && hasIssue(*full, lexicon::BlobIssueType::HashMismatch),
              "Corrupted Blob was not detected")) return 1;
  auto verifiedCorrupt = app.blobs.verifyBlob(*hash);
  if (!success(verifiedCorrupt, "Verify corrupt Blob") ||
      !expect(verifiedCorrupt->type == lexicon::BlobIssueType::HashMismatch,
              "Verification missed corrupted bytes")) return 1;
  auto corruptExport = app.blobs.exportFile(*hash, (directory / "export").string());
  if (!expect(!corruptExport && corruptExport.error().code == lexicon::Error::Code::Storage,
              "Corrupted Blob export succeeded")) return 1;
  auto corruptGc = app.blobs.collectUnusedBlobs(*full);
  if (!success(corruptGc, "GC with corruption") ||
      !expect(corruptGc->deleted == 0 && fs::exists(canonical), "GC deleted corruption")) return 1;
  if (!expect(!app.blobs.importFile(source.string()), "Import silently reused corrupted Blob")) return 1;
  second.fieldValues.clear();
  if (!success(app.items.saveItem(second), "Remove corrupt reference")) return 1;
  auto corruptFast = app.blobs.scanStorage();
  if (!success(corruptFast, "Scan corrupt orphan")) return 1;
  auto corruptSweep = app.blobs.collectUnusedBlobs(*corruptFast);
  if (!success(corruptSweep, "GC corrupt orphan") ||
      !expect(corruptSweep->deleted == 0 && corruptSweep->skipped == 1 && fs::exists(canonical),
              "GC deleted a corrupt orphan")) return 1;

  // A real permission failure must be counted as failed, not as a deletion.
  write(source, "permission test");
  auto protectedHash = app.blobs.importFile(source.string());
  if (!success(protectedHash, "Import deletion failure candidate")) return 1;
  const auto protectedPath = directory / "blobs" / protectedHash->substr(0, 2) / protectedHash->substr(2);
  auto protectedScan = app.blobs.scanStorage();
  if (!success(protectedScan, "Scan deletion failure candidate")) return 1;
  std::error_code permissionsError;
  fs::permissions(protectedPath.parent_path(), fs::perms::owner_read | fs::perms::owner_exec,
                  fs::perm_options::replace, permissionsError);
  if (!permissionsError) {
    auto failedGc = app.blobs.collectUnusedBlobs(*protectedScan);
    fs::permissions(protectedPath.parent_path(), fs::perms::owner_all,
                    fs::perm_options::replace, permissionsError);
    if (!success(failedGc, "GC under read-only prefix")) return 1;
    if (fs::exists(protectedPath)) {
      if (!expect(failedGc->failed == 1 && failedGc->deleted == 0,
                  "Deletion failure was reported as success")) return 1;
    } else if (!expect(failedGc->deleted == 1 && failedGc->failed == 0,
                       "Privileged deletion was reported as failure")) return 1;
  }

  write(directory / "blobs" / "stray.tmp", "unexpected");
  write(canonical.parent_path() / "bad-name", "unexpected");
  fs::create_directories(directory / "blobs" / "unexpected-dir");
  std::error_code symlinkError;
  fs::create_symlink(source, canonical.parent_path() / "symlink", symlinkError);
  auto unexpected = app.blobs.scanStorage(lexicon::BlobScanDepth::FullIntegrity);
  if (!success(unexpected, "Scan unexpected entries") ||
      !expect(hasIssue(*unexpected, lexicon::BlobIssueType::UnexpectedFile),
              "Unexpected entries were not reported")) return 1;
  auto unexpectedGc = app.blobs.collectUnusedBlobs(*unexpected);
  if (!success(unexpectedGc, "GC unexpected entries") ||
      !expect(fs::exists(directory / "blobs" / "stray.tmp") &&
              fs::exists(canonical.parent_path() / "bad-name") &&
              fs::exists(canonical.parent_path() / "symlink") == !symlinkError,
              "GC deleted an unexpected entry")) return 1;
  std::cout << "Referenced blobs: " << unexpected->referencedBlobCount
            << " Physical blobs: " << unexpected->physicalBlobCount
            << " Orphaned: " << unexpected->orphanedBlobCount
            << " Missing: " << unexpected->missingBlobCount
            << " Corrupted: " << unexpected->corruptedBlobCount
            << " Reclaimable bytes: " << unexpected->orphanedBytes << '\n';
  return 0;
}
