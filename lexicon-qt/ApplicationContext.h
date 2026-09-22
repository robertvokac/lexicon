#pragma once
#include "Conversions.h"
#include "LexiconApplication.h"

namespace qtbridge {
inline bool success(lexicon::Result<void> result, QString *error) {
  if (result)
    return true;
  if (error)
    *error = toQt(result.error().message);
  return false;
}
template <class QtT, class T>
QtT value(lexicon::Result<T> result, QString *error, QtT fallback = {}) {
  if (result)
    return toQt(*result);
  if (error)
    *error = toQt(result.error().message);
  return fallback;
}
} // namespace qtbridge

class QtItemService {
public:
  explicit QtItemService(lexicon::ItemService &core) : core_(core) {}
  QList<ItemRecord>
  loadItems(int groupId, int typeId, const QList<ItemValueFilter> &valueFilters,
            const QString &searchText, const ItemColumnFilters &columnFilters,
            const QList<ItemPropertyFilter> &propertyFilters,
            const QString &tagFilter = QString(),
            const QString &flagFilter = QString(), int understandingFilter = -1,
            int statusFilter = -1, int pinnedFilter = -1, int limit = -1,
            int offset = 0, int sortColumn = 3,
            SortOrder sortOrder = SortOrder::Ascending,
            QString *errorMessage = nullptr) {
    return qtbridge::value<QList<ItemRecord>>(
        core_.loadItems(
            groupId, typeId, qtbridge::toCore(valueFilters),
            qtbridge::toCore(searchText), qtbridge::toCore(columnFilters),
            qtbridge::toCore(propertyFilters), qtbridge::toCore(tagFilter),
            qtbridge::toCore(flagFilter), understandingFilter, statusFilter,
            pinnedFilter, limit, offset, sortColumn,
            qtbridge::toCore(sortOrder)),
        errorMessage);
  }
  int countItems(int groupId, int typeId,
                 const QList<ItemValueFilter> &valueFilters,
                 const QString &searchText,
                 const ItemColumnFilters &columnFilters,
                 const QList<ItemPropertyFilter> &propertyFilters,
                 const QString &tagFilter = QString(),
                 const QString &flagFilter = QString(),
                 int understandingFilter = -1, int statusFilter = -1,
                 int pinnedFilter = -1, QString *errorMessage = nullptr) {
    return qtbridge::value<int>(
        core_.countItems(
            groupId, typeId, qtbridge::toCore(valueFilters),
            qtbridge::toCore(searchText), qtbridge::toCore(columnFilters),
            qtbridge::toCore(propertyFilters), qtbridge::toCore(tagFilter),
            qtbridge::toCore(flagFilter), understandingFilter, statusFilter,
            pinnedFilter),
        errorMessage);
  }
  bool loadItem(int itemId, ItemRecord &outItem,
                QString *errorMessage = nullptr) {
    auto result = core_.loadItem(itemId);
    if (!result) {
      if (errorMessage)
        *errorMessage = qtbridge::toQt(result.error().message);
      return false;
    }
    outItem = qtbridge::toQt(*result);
    return true;
  }
  int createItem(const ItemRecord &item, QString *errorMessage = nullptr) {
    return qtbridge::value<int>(core_.createItem(qtbridge::toCore(item)),
                                errorMessage, -1);
  }
  bool saveItem(const ItemRecord &item, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.saveItem(qtbridge::toCore(item)),
                             errorMessage);
  }
  bool deleteItem(int itemId, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.deleteItem(itemId), errorMessage);
  }
  bool logItemRead(int itemId, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.logItemRead(itemId), errorMessage);
  }
  // conflict is set when the item changed elsewhere after it was loaded.
  bool saveItemWithLinks(const ItemRecord &item, const QList<LinkRecord> &links,
                         const QList<LinkRecord> &backlinks,
                         int *savedId = nullptr,
                         QString *errorMessage = nullptr,
                         bool *conflict = nullptr) {
    auto result =
        core_.saveItemWithLinks(qtbridge::toCore(item), qtbridge::toCore(links),
                                qtbridge::toCore(backlinks));
    if (conflict)
      *conflict = !result &&
                  result.error().code == lexicon::Error::Code::Conflict;
    if (!result) {
      if (errorMessage)
        *errorMessage = qtbridge::toQt(result.error().message);
      return false;
    }
    if (savedId)
      *savedId = *result;
    return true;
  }

private:
  lexicon::ItemService &core_;
};

class QtTypeService {
public:
  explicit QtTypeService(lexicon::TypeService &core) : core_(core) {}
  QList<ItemTypeRecord> loadItemTypes(int groupId = -1,
                                      QString *errorMessage = nullptr) {
    return qtbridge::value<QList<ItemTypeRecord>>(core_.loadItemTypes(groupId),
                                                  errorMessage);
  }
  bool upsertItemType(const ItemTypeRecord &itemType,
                      QString *errorMessage = nullptr) {
    return qtbridge::success(core_.upsertItemType(qtbridge::toCore(itemType)),
                             errorMessage);
  }
  int countItemsForType(int itemTypeId, QString *errorMessage = nullptr) {
    return qtbridge::value<int>(core_.countItemsForType(itemTypeId),
                                errorMessage);
  }
  bool deleteItemType(int itemTypeId, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.deleteItemType(itemTypeId), errorMessage);
  }
  QList<ItemFieldRecord> loadItemFields(int itemTypeId,
                                        QString *errorMessage = nullptr) {
    return qtbridge::value<QList<ItemFieldRecord>>(
        core_.loadItemFields(itemTypeId), errorMessage);
  }
  bool upsertItemField(const ItemFieldRecord &field,
                       QString *errorMessage = nullptr) {
    return qtbridge::success(core_.upsertItemField(qtbridge::toCore(field)),
                             errorMessage);
  }
  int countFieldValues(int fieldId, QString *errorMessage = nullptr) {
    return qtbridge::value<int>(core_.countFieldValues(fieldId), errorMessage);
  }
  bool deleteItemField(int fieldId, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.deleteItemField(fieldId), errorMessage);
  }

private:
  lexicon::TypeService &core_;
};

class QtGroupService {
public:
  explicit QtGroupService(lexicon::GroupService &core) : core_(core) {}
  QList<GroupRecord> loadGroups(QString *errorMessage = nullptr) {
    return qtbridge::value<QList<GroupRecord>>(core_.loadGroups(),
                                               errorMessage);
  }
  int defaultGroupId(QString *errorMessage = nullptr) {
    return qtbridge::value<int>(core_.defaultGroupId(), errorMessage, -1);
  }
  bool upsertGroup(const GroupRecord &group, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.upsertGroup(qtbridge::toCore(group)),
                             errorMessage);
  }
  bool deleteGroup(int groupId, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.deleteGroup(groupId), errorMessage);
  }

private:
  lexicon::GroupService &core_;
};

class QtLinkService {
public:
  explicit QtLinkService(lexicon::LinkService &core) : core_(core) {}
  QList<LinkRecord> loadLinks(int itemId, QString *errorMessage = nullptr) {
    return qtbridge::value<QList<LinkRecord>>(core_.loadLinks(itemId),
                                              errorMessage);
  }
  QList<LinkRecord> loadBacklinks(int itemId, QString *errorMessage = nullptr) {
    return qtbridge::value<QList<LinkRecord>>(core_.loadBacklinks(itemId),
                                              errorMessage);
  }
  bool saveLink(const LinkRecord &link, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.saveLink(qtbridge::toCore(link)),
                             errorMessage);
  }
  bool deleteLink(int linkId, QString *errorMessage = nullptr) {
    return qtbridge::success(core_.deleteLink(linkId), errorMessage);
  }

private:
  lexicon::LinkService &core_;
};

class QtSearchService {
public:
  explicit QtSearchService(lexicon::SearchService &core) : core_(core) {}
  QStringList loadSuggestions(QString *errorMessage = nullptr) {
    return qtbridge::value<QStringList>(core_.loadSuggestions(), errorMessage);
  }
  QStringList loadItemTitles(QString *errorMessage = nullptr) {
    return qtbridge::value<QStringList>(core_.loadItemTitles(), errorMessage);
  }
  QList<UsageValueRecord> loadTagUsage(QString *errorMessage = nullptr) {
    return qtbridge::value<QList<UsageValueRecord>>(core_.loadTagUsage(),
                                                    errorMessage);
  }
  QList<UsageValueRecord> loadFlagUsage(QString *errorMessage = nullptr) {
    return qtbridge::value<QList<UsageValueRecord>>(core_.loadFlagUsage(),
                                                    errorMessage);
  }
  QList<UsageValueRecord> loadAliasUsage(QString *errorMessage = nullptr) {
    return qtbridge::value<QList<UsageValueRecord>>(core_.loadAliasUsage(),
                                                    errorMessage);
  }
  int findItemId(const QString &title, const QString &disambiguation = {},
                 QString *errorMessage = nullptr) {
    return qtbridge::value<int>(
        core_.findItemId(qtbridge::toCore(title),
                         qtbridge::toCore(disambiguation)),
        errorMessage, -1);
  }

private:
  lexicon::SearchService &core_;
};

class QtConfigurationService {
public:
  explicit QtConfigurationService(lexicon::ConfigurationService &core)
      : core_(core) {}
  QMap<QString, QString> loadConfiguration(QString *errorMessage = nullptr) {
    return qtbridge::value<QMap<QString, QString>>(core_.loadConfiguration(),
                                                   errorMessage);
  }
  bool saveConfiguration(const QMap<QString, QString> &values,
                         QString *errorMessage = nullptr) {
    return qtbridge::success(core_.saveConfiguration(qtbridge::toCore(values)),
                             errorMessage);
  }

private:
  lexicon::ConfigurationService &core_;
};

class QtBlobService {
public:
  explicit QtBlobService(lexicon::BlobService &core) : core_(core) {}
  lexicon::Result<lexicon::BlobMaintenanceReport> scanStorage(lexicon::BlobScanDepth depth) {
    return core_.scanStorage(depth);
  }
  lexicon::Result<lexicon::BlobGarbageCollectionResult> collectUnusedBlobs(
      const lexicon::BlobMaintenanceReport &scan) {
    return core_.collectUnusedBlobs(scan);
  }
  QString importFile(const QString &path, QString *error = nullptr) {
    return qtbridge::value<QString>(core_.importFile(qtbridge::toCore(path)),
                                    error);
  }
  bool exportFile(const QString &hash, const QString &path,
                  QString *error = nullptr) {
    return qtbridge::success(
        core_.exportFile(qtbridge::toCore(hash), qtbridge::toCore(path)),
        error);
  }

private:
  lexicon::BlobService &core_;
};

class QtApplicationFacade {
public:
  explicit QtApplicationFacade(lexicon::LexiconApplication &core)
      : core(core), items(core.items), types(core.types), groups(core.groups),
        links(core.links), search(core.search),
        configuration(core.configuration), blobs(core.blobs) {}
  // For the operations that take the whole application, such as export.
  lexicon::LexiconApplication &core;
  QtItemService items;
  QtTypeService types;
  QtGroupService groups;
  QtLinkService links;
  QtSearchService search;
  QtConfigurationService configuration;
  QtBlobService blobs;
};

void installApplication(QtApplicationFacade &application);
QtApplicationFacade &services();
