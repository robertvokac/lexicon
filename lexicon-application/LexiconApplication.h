#pragma once
#include "Repository.h"
#include "Validation.h"

class ItemService {
public:
    explicit ItemService(Repository& repository) : repository_(repository) {}
    QList<ItemRecord> loadItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, int limit = -1, int offset = 0, int sortColumn = 3, SortOrder sortOrder = SortOrder::Ascending, QString* errorMessage = nullptr) { return repository_.loadItems(groupId, typeId, valueFilters, searchText, columnFilters, propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, limit, offset, sortColumn, sortOrder, errorMessage); }
    int countItems(int groupId, int typeId, const QList<ItemValueFilter>& valueFilters, const QString& searchText, const ItemColumnFilters& columnFilters, const QList<ItemPropertyFilter>& propertyFilters, const QString& tagFilter = QString(), const QString& flagFilter = QString(), int understandingFilter = -1, int statusFilter = -1, int pinnedFilter = -1, QString* errorMessage = nullptr) { return repository_.countItems(groupId, typeId, valueFilters, searchText, columnFilters, propertyFilters, tagFilter, flagFilter, understandingFilter, statusFilter, pinnedFilter, errorMessage); }
    bool loadItem(int itemId, ItemRecord& outItem, QString* errorMessage = nullptr) { return repository_.loadItem(itemId, outItem, errorMessage); }
    int createItem(const ItemRecord& item, QString* errorMessage = nullptr);
    bool saveItem(const ItemRecord& item, QString* errorMessage = nullptr);
    bool deleteItem(int itemId, QString* errorMessage = nullptr) { return repository_.deleteItem(itemId, errorMessage); }
    bool logItemRead(int itemId, QString* errorMessage = nullptr) { return repository_.logItemRead(itemId, errorMessage); }
    bool saveItemWithLinks(const ItemRecord& item, const QList<LinkRecord>& links, const QList<LinkRecord>& backlinks, int* savedId = nullptr, QString* errorMessage = nullptr);
private:
    Repository& repository_;
};

class TypeService {
public:
    explicit TypeService(Repository& repository) : repository_(repository) {}
    QList<ItemTypeRecord> loadItemTypes(int groupId = -1, QString* errorMessage = nullptr) { return repository_.loadItemTypes(groupId, errorMessage); }
    bool upsertItemType(const ItemTypeRecord& itemType, QString* errorMessage = nullptr) { return lexicon::validateType(itemType, errorMessage) && repository_.upsertItemType(itemType, errorMessage); }
    int countItemsForType(int itemTypeId, QString* errorMessage = nullptr) { return repository_.countItemsForType(itemTypeId, errorMessage); }
    bool deleteItemType(int itemTypeId, QString* errorMessage = nullptr) { return repository_.deleteItemType(itemTypeId, errorMessage); }
    QList<ItemFieldRecord> loadItemFields(int itemTypeId, QString* errorMessage = nullptr) { return repository_.loadItemFields(itemTypeId, errorMessage); }
    bool upsertItemField(const ItemFieldRecord& field, QString* errorMessage = nullptr) { return lexicon::validateField(field, errorMessage) && repository_.upsertItemField(field, errorMessage); }
    int countFieldValues(int fieldId, QString* errorMessage = nullptr) { return repository_.countFieldValues(fieldId, errorMessage); }
    bool deleteItemField(int fieldId, QString* errorMessage = nullptr) { return repository_.deleteItemField(fieldId, errorMessage); }
private:
    Repository& repository_;
};

class GroupService {
public:
    explicit GroupService(Repository& repository) : repository_(repository) {}
    QList<GroupRecord> loadGroups(QString* errorMessage = nullptr) { return repository_.loadGroups(errorMessage); }
    int defaultGroupId(QString* errorMessage = nullptr) { return repository_.defaultGroupId(errorMessage); }
    bool upsertGroup(const GroupRecord& group, QString* errorMessage = nullptr) { return lexicon::validateGroup(group, errorMessage) && repository_.upsertGroup(group, errorMessage); }
    bool deleteGroup(int groupId, QString* errorMessage = nullptr) { return repository_.deleteGroup(groupId, errorMessage); }
private:
    Repository& repository_;
};

class LinkService {
public:
    explicit LinkService(Repository& repository) : repository_(repository) {}
    QList<LinkRecord> loadLinks(int itemId, QString* errorMessage = nullptr) { return repository_.loadLinks(itemId, errorMessage); }
    QList<LinkRecord> loadBacklinks(int itemId, QString* errorMessage = nullptr) { return repository_.loadBacklinks(itemId, errorMessage); }
    bool saveLink(const LinkRecord& link, QString* errorMessage = nullptr) { return lexicon::validateLink(link, errorMessage) && repository_.saveLink(link, errorMessage); }
    bool deleteLink(int linkId, QString* errorMessage = nullptr) { return repository_.deleteLink(linkId, errorMessage); }
private:
    Repository& repository_;
};

class SearchService {
public:
    explicit SearchService(Repository& repository) : repository_(repository) {}
    QStringList loadSuggestions(QString* errorMessage = nullptr) { return repository_.loadSuggestions(errorMessage); }
    QStringList loadItemTitles(QString* errorMessage = nullptr) { return repository_.loadItemTitles(errorMessage); }
    QList<UsageValueRecord> loadTagUsage(QString* errorMessage = nullptr) { return repository_.loadTagUsage(errorMessage); }
    QList<UsageValueRecord> loadFlagUsage(QString* errorMessage = nullptr) { return repository_.loadFlagUsage(errorMessage); }
    QList<UsageValueRecord> loadAliasUsage(QString* errorMessage = nullptr) { return repository_.loadAliasUsage(errorMessage); }
    int findItemId(const QString& title, const QString& disambiguation = {}, QString* errorMessage = nullptr) { return repository_.findItemId(title, disambiguation, errorMessage); }
private:
    Repository& repository_;
};

class ConfigurationService {
public:
    explicit ConfigurationService(Repository& repository) : repository_(repository) {}
    QMap<QString, QString> loadConfiguration(QString* errorMessage = nullptr) { return repository_.loadConfiguration(errorMessage); }
    bool saveConfiguration(const QMap<QString, QString>& values, QString* errorMessage = nullptr) { return repository_.saveConfiguration(values, errorMessage); }
private:
    Repository& repository_;
};

class LexiconApplication {
public:
    explicit LexiconApplication(Repository& repository) : items(repository), types(repository), groups(repository), links(repository), search(repository), configuration(repository), blobs(repository) {}
    ItemService items;
    TypeService types;
    GroupService groups;
    LinkService links;
    SearchService search;
    ConfigurationService configuration;
    class BlobService {
    public:
        explicit BlobService(Repository& repository) : repository_(repository) {}
        QString importFile(const QString& path, QString* error = nullptr) { return repository_.importBlob(path, error); }
        bool exportFile(const QString& hash, const QString& path, QString* error = nullptr) { return repository_.exportBlob(hash, path, error); }
    private:
        Repository& repository_;
    } blobs;
};

// The desktop composition root installs one application instance for existing dialogs.
void installApplication(LexiconApplication& application);
LexiconApplication& services();
